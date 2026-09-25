#!/usr/bin/env python3
"""Read the hardware-counter profile the PAPI build writes (acic_prof.h).

    papi_profile_report.py PROFDIR BINARY [--run-output FILE] [--top 40]

PROFDIR holds pe<N>.txt per PE and maps.<N>.txt per process. BINARY is the
executable that wrote them (non-PIE, with -g). With --run-output, counters are
also given per update and per graph edge, from the run's own "Updates noted"
and "Actual edges" lines.

Samples are symbolized with addr2line -i, so an address inside inlined code
reports its whole inline chain. Four tables come out: by function as linked,
by innermost inlined function, by inline chain, and by source line. Shared
library addresses are resolved to the nearest dynamic symbol.

Hardware samples use cycle overflow without precise IP; timer samples use
POSIX thread CPU timers. Read individual source lines as approximate. Timer
delivery can be coalesced or limited by clock resolution: the requested period
is not a calibrated conversion from sample count to CPU time.
"""
import argparse
import bisect
import collections
import glob
import json
import os
import re
import subprocess
import sys

# The system addr2line on Anvil (binutils 2.30) loses the names of inlined
# member functions; spack's 2.37 keeps them.
ADDR2LINE = os.environ.get('ADDR2LINE') or next(
    (p for p in sorted(glob.glob(
        '/apps/spack/anvil/apps/binutils/2.37-gcc-11.2.0-*/bin/addr2line'))),
    'addr2line')


def read_profiles(root):
    counters = collections.Counter()
    samples = collections.Counter()
    period = None
    pes = 0
    dropped = 0
    per_pe = []
    for path in sorted(glob.glob(os.path.join(root, 'pe*.txt'))):
        pes += 1
        mine = {}
        with open(path) as f:
            for line in f:
                parts = line.split()
                if parts[0] == 'S':
                    samples[int(parts[1], 16)] += int(parts[2])
                elif parts[0] == 'counter':
                    v = int(parts[2])
                    if v >= 0:
                        counters[parts[1]] += v
                        mine[parts[1]] = v
                elif parts[0] == 'period':
                    period = int(parts[1])
                elif parts[0] == 'dropped':
                    dropped += int(parts[1])
        per_pe.append(mine)
    return pes, period, dropped, counters, samples, per_pe


def read_maps(root):
    """(start, end, offset, path) for executable mappings, from any process."""
    maps = set()
    for path in glob.glob(os.path.join(root, 'maps.*.txt')):
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) < 6 or 'x' not in parts[1]:
                    continue
                start, end = (int(x, 16) for x in parts[0].split('-'))
                maps.add((start, end, int(parts[2], 16), parts[5]))
    return sorted(maps)


def addr2line(binary, addrs):
    """addr -> list of (function, file:line), innermost first."""
    out = {}
    if not addrs:
        return out
    addrs = sorted(addrs)
    # addr2line prints a sentinel-free stream; ask for each address with -a so
    # the address header separates the inline chains.
    proc = subprocess.run([ADDR2LINE, '-e', binary, '-a', '-f', '-i', '-C'],
                          input='\n'.join('%x' % a for a in addrs),
                          capture_output=True, text=True, check=True)
    cur = None
    lines = proc.stdout.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith('0x'):
            cur = int(line, 16)
            out[cur] = []
            i += 1
            continue
        func = line
        loc = lines[i + 1] if i + 1 < len(lines) else '??:0'
        loc = re.sub(r' \(discriminator \d+\)', '', loc)
        out[cur].append((func, short_path(loc)))
        i += 2
    return out


def short_path(loc):
    for prefix in ('/home/x-rrao/', os.path.expanduser('~') + '/'):
        if loc.startswith(prefix):
            loc = loc[len(prefix):]
    loc = re.sub(r'^/apps/spack/.*/include/c\+\+/[\d.]+/', 'libstdc++/', loc)
    return loc


def dynsyms(path, cache={}):
    if path not in cache:
        syms = []
        try:
            for flag in ('--dynamic', ''):
                cmd = ['nm', '--defined-only', '-C'] + ([flag] if flag else []) + [path]
                p = subprocess.run(cmd, capture_output=True, text=True)
                for line in p.stdout.splitlines():
                    parts = line.split(None, 2)
                    if len(parts) == 3 and parts[1] in 'tTwWiI':
                        syms.append((int(parts[0], 16), parts[2]))
        except OSError:
            pass
        syms.sort()
        cache[path] = ([a for a, _ in syms], [n for _, n in syms])
    return cache[path]


# Pipeline stages, matched against the simplified inline chain (outermost
# first) in this order; the first rule that matches takes the sample.
STAGES = [
    ('heap: pop and sift', r'__adjust_heap|__push_heap|ComparePairs|pop_heap|priority_queue'),
    ('dest PE lookup', r'get_dest_proc'),
    ('hold release / flush', r'insertBucketsByDest|flushDest|flushIdle|releaseFull|insertBuckets\b|tflush|flushStale'),
    ('TRAM insert', r'sendItemPrioDeferredDest|insertValueWPs|insertValue\b'),
    ('bucket computation', r'get_histo_bucket|charge_new_update|bucket_of|charged_top_by_skew'),
    ('edge scan', r'generate_updates'),
    ('apply at destination', r'process_update|fold_batch'),
    ('receive sort / fan-out', r'HTramRecv::|receivePerPE|CProxy'),
    ('heap loop', r'process_heap'),
    ('malloc / free', r'malloc|_int_free|\bfree\b|unlink_chunk|memalign|CmiFree|CmiAlloc|consolidate|operator new|operator delete|sysmalloc|tcache'),
    ('memcpy', r'memcpy|memmove|memset'),
    ('network progress', r'lci::|LCI|CommBackend|comm_backend|pthread_spin|ibv_|mlx5|libfabric|fi_'),
    ('scheduler / runtime', r'Csd|Cmi|Ccd|moodycamel|CkLocRec|_processHandler|CkCallstack|CkIndex|idle_triggered|getCurrentTime|vdso|__tls|_init\b|CkDeliver|_call|Ck|pthread_mutex|sched_yield'),
]


def stage_of(chain):
    for name, pattern in STAGES:
        if re.search(pattern, chain):
            return name
    return 'other'


def simplify(func):
    """Drop template arguments and parameter lists, which make chains unreadable."""
    out, depth = [], 0
    for ch in func:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        elif depth == 0:
            out.append(ch)
    s = ''.join(out)
    s = s.replace('std::', '').replace('__gnu_cxx::', '')
    return s.strip() or func


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root')
    ap.add_argument('binary')
    ap.add_argument('--run-output')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--json', help='write full symbolized sample counts for auditing')
    args = ap.parse_args()

    pes, period, dropped, counters, samples, per_pe = read_profiles(args.root)
    if not pes:
        sys.exit('no pe*.txt under %s' % args.root)
    total = sum(samples.values())

    updates = edges = compute = None
    if args.run_output:
        text = open(args.run_output, errors='replace').read()
        m = re.search(r'^Updates noted: (\d+)', text, re.M)
        updates = int(m.group(1)) if m else None
        m = re.search(r'^Actual edges: (\d+)', text, re.M)
        edges = int(m.group(1)) if m else None
        m = re.search(r'^Compute time: ([\d.]+)', text, re.M)
        compute = float(m.group(1)) if m else None

    print('# PAPI profile: %s\n' % args.root)
    if period is not None and period < 0:
        print('%d PEs, %d samples (requested CPU-timer period %d us), %d dropped.' %
              (pes, total, -period, dropped))
    else:
        print('%d PEs, %d samples (one per %s cycles), %d dropped.' %
              (pes, total, period, dropped))
    if compute:
        print('Compute %.3f s; updates %s; graph edges %s.' %
              (compute, updates, edges))
    print()
    print('## Counters\n')
    cyc = counters.get('cycles', 0)
    ins = counters.get('instructions', 0)
    print('| counter | total | per update | per graph edge | per cycle |')
    print('|---|---:|---:|---:|---:|')
    for name in ('cycles', 'instructions', 'branch_misses', 'l2_data_misses',
                 'div_cycles', 'voluntary_switches', 'involuntary_switches'):
        if name not in counters:
            continue
        v = counters[name]
        pu = '%.2f' % (v / updates) if updates else ''
        pe_ = '%.2f' % (v / edges) if edges else ''
        pc = '%.4f' % (v / cyc) if cyc else ''
        print('| %s | %d | %s | %s | %s |' % (name, v, pu, pe_, pc))
    if cyc:
        print('\nInstructions per cycle: %.2f. Cycles per PE-second of compute: %s.' %
              (ins / cyc, '%.2e' % (cyc / (pes * compute)) if compute else 'n/a'))
    busy = sorted(p.get('cycles', 0) for p in per_pe)
    if busy and busy[len(busy) // 2]:
        print('Cycles per PE: min %.3g, median %.3g, max %.3g.' %
              (busy[0], busy[len(busy) // 2], busy[-1]))

    # Symbolize.
    maps = read_maps(args.root)
    exe = os.path.realpath(args.binary)
    in_exe, elsewhere = [], {}
    ambiguous_samples = unmapped_samples = 0
    for a in samples:
        matches = [(start, off, path) for start, end, off, path in maps
                   if start <= a < end]
        if not matches:
            unmapped_samples += samples[a]
        if len({(a-start+off, os.path.realpath(path))
                for start, off, path in matches}) > 1:
            ambiguous_samples += samples[a]
        m = matches[0] if matches else None
        if m is None or os.path.realpath(m[2]) == exe or \
                os.path.basename(m[2]) == os.path.basename(exe):
            in_exe.append(a)
        else:
            elsewhere[a] = m
    chains = addr2line(args.binary, in_exe)
    for a, (start, off, path) in elsewhere.items():
        addrs, names = dynsyms(path)
        rel = a - start + off
        i = bisect.bisect_right(addrs, rel) - 1
        name = names[i] if i >= 0 else '??'
        chains[a] = [(name, os.path.basename(path))]

    by_stage = collections.Counter()
    by_func = collections.Counter()      # outermost (as linked)
    by_inner = collections.Counter()     # innermost inlined
    by_chain = collections.Counter()
    by_line = collections.Counter()
    for a, n in samples.items():
        ch = chains.get(a) or [('??', '??')]
        by_func[simplify(ch[-1][0])] += n
        by_inner[simplify(ch[0][0])] += n
        chain = ' > '.join(simplify(f) for f, _ in reversed(ch))
        by_chain[chain] += n
        # Stage by the innermost frames first: a chain names its callers too.
        inner_first = ' < '.join(simplify(f) for f, _ in ch)
        by_stage[stage_of(inner_first)] += n
        by_line['%s (%s)' % (ch[0][1], simplify(ch[0][0]))] += n

    def table(title, counter, top):
        print('\n## %s\n' % title)
        print('| share | samples | %s |' % title.split(' by ')[-1])
        print('|---:|---:|---|')
        for k, v in counter.most_common(top):
            print('| %.2f%% | %d | `%s` |' % (100.0 * v / total, v, k))

    print('\n## Stages\n')
    # Timer delivery is not calibrated CPU time. In particular, sub-tick
    # periods can coalesce, so samples * requested period undercounts time.
    timer_mode = period is not None and period < 0
    if timer_mode:
        print('| stage | sample share |')
        print('|---|---:|')
    else:
        print('| stage | share | cycles per update |')
        print('|---|---:|---:|')
    for k, v in by_stage.most_common():
        share = 100.0 * v / total
        if timer_mode:
            print('| %s | %.1f%% |' % (k, share))
        else:
            cpu = '%.1f' % (cyc * v / total / updates) if updates and cyc and total else ''
            print('| %s | %.1f%% | %s |' % (k, share, cpu))
    if timer_mode:
        print('\nTimer shares are statistical attribution, not elapsed CPU-time '
              'measurements; delivery can be coalesced. No nanoseconds per '
              'update are inferred from the requested timer period.')
    print('\nShared-library names use the nearest available symbol; stripped '
          'internal functions may be grouped under a nearby name. Use broad '
          'cost groups rather than treating these labels as call counts.')

    table('Samples by function as linked', by_func, args.top)
    table('Samples by innermost inlined function', by_inner, args.top)
    table('Samples by inline chain', by_chain, args.top)
    table('Samples by source line', by_line, args.top * 2)
    if args.json:
        with open(args.json, 'w') as stream:
            json.dump(dict(root=args.root, binary=exe, pes=pes, samples=total,
                           dropped=dropped, requested_period=period,
                           ambiguous_mapping_samples=ambiguous_samples,
                           unmapped_samples=unmapped_samples,
                           mode='cpu_timer' if timer_mode else 'cycle_overflow',
                           counters=counters, stages=by_stage, functions=by_func,
                           innermost=by_inner, chains=by_chain, lines=by_line),
                      stream, indent=2)
            stream.write('\n')


if __name__ == '__main__':
    main()
