#!/usr/bin/env python3
"""Per-machine CPU layout: which cores a job may use, and how ranks split them.

Delta and Anvil: 128 cores, eight 16-core NUMA domains, all usable. A process
gets a 128/rpn-core stride and runs one fewer worker than that, leaving a core
to the OS: 8 x 15 is the layout measured since step 7.5.

Frontier: 64 cores in eight 8-core L3 regions (two per NUMA domain). Slurm's
core specialization reserves the first core of each region -- PUs 0, 8, ...,
56 -- so 56 are usable and SMT is off. The reserved core is the OS core Delta
left free by hand, so a Frontier process uses every usable core of its
regions: 8 x 7 is Delta's 8 x 15 (one process per region, one OS core each).

Python 3.6 compatible: launch_acic.sh calls it on compute nodes.

    machine.py pemap RANK RPN WORKERS_PER_RANK   # prints a +pemap value
    machine.py workers RPN                       # default ACIC workers per node
    machine.py cpus RPN                          # srun -c per rank
"""
import os
import socket
import sys


def name():
    forced = os.environ.get('ACIC_MACHINE')
    if forced:
        return forced
    if os.environ.get('LMOD_SYSTEM_NAME') == 'frontier' or socket.gethostname().startswith('frontier'):
        return 'frontier'
    return 'delta'


MACHINE = name()
if MACHINE == 'frontier':
    REGION = 8                                   # cores per L3 region
    USABLE = [p for p in range(64) if p % REGION]  # PUs 8k are reserved
    OS_CORE_RESERVED = True
else:
    REGION = 16                                  # cores per NUMA domain
    USABLE = list(range(128))
    OS_CORE_RESERVED = False
NODE_CPUS = len(USABLE)
REGION_CPUS = REGION - 1 if OS_CORE_RESERVED else REGION  # usable cores per region


def check_rpn(rpn):
    # A rank owns whole regions or an equal share of one; it never straddles
    # a region boundary. Frontier: 1, 2, 4, 8 or 56 ranks per node.
    size = NODE_CPUS // rpn if rpn >= 1 else 0
    if rpn < 1 or NODE_CPUS % rpn or (size % REGION_CPUS and REGION_CPUS % size):
        raise ValueError('%d ranks per node do not split %d cores into %d-core regions on %s'
                         % (rpn, NODE_CPUS, REGION_CPUS, MACHINE))


def cpus_per_rank(rpn):
    """Usable cores a rank owns (srun -c)."""
    check_rpn(rpn)
    return NODE_CPUS // rpn


def acic_workers_per_rank(rpn):
    """ACIC workers per process: every owned core, less the OS core if the
    machine does not already reserve one."""
    return cpus_per_rank(rpn) - (0 if OS_CORE_RESERVED else 1)


def acic_workers(rpn):
    return rpn * acic_workers_per_rank(rpn)


# RIKEN follows the same rule as ACIC: compute threads on every owned core but
# the OS core. Gluon keeps one owned core for its communication thread instead.
threads_per_rank = acic_workers_per_rank


def gluon_threads_per_rank(rpn):
    return cpus_per_rank(rpn) - 1


def layout_defaults():
    """7.6f1 search spaces (ranks per node; GAPBS threads) that fit this machine."""
    if MACHINE == 'frontier':
        return dict(acic='8,4', riken='2,4,8,56', gluon='1,8', gap='14,28,56')
    return dict(acic='8,16', riken='4,8,16', gluon='1,8', gap='16,64,127')


def rank_cores(rank, rpn):
    per = cpus_per_rank(rpn)
    return USABLE[rank * per:(rank + 1) * per]


def pemap(rank, rpn, workers):
    """+pemap for one process: the first `workers` of its cores, as ranges."""
    cores = rank_cores(rank, rpn)
    if workers > len(cores) or workers < 1:
        raise ValueError('%d workers do not fit rank %d of %d (%d cores) on %s'
                         % (workers, rank, rpn, len(cores), MACHINE))
    cores, runs = cores[:workers], []
    for core in cores:
        if runs and core == runs[-1][1] + 1:
            runs[-1][1] = core
        else:
            runs.append([core, core])
    return ','.join(str(a) if a == b else '%d-%d' % (a, b) for a, b in runs)


def acic_srun_extra(nodes):
    """LCI on Frontier needs a single-node VNI when a job stays on one node."""
    return ['--network=single_node_vni'] if MACHINE == 'frontier' and nodes == 1 else []


def gap_thread_candidates():
    if MACHINE == 'frontier':
        return [1, 2, 4, 7, 14, 28, 42, 49, 55, 56]
    return [1, 2, 4, 8, 16, 32, 64, 96, 120, 127, 128]


if __name__ == '__main__':
    command, args = sys.argv[1], [int(a) for a in sys.argv[2:]]
    if command == 'pemap':
        print(pemap(*args))
    elif command == 'workers':
        print(acic_workers(*args))
    elif command == 'cpus':
        print(cpus_per_rank(*args))
    else:
        raise SystemExit('unknown command ' + command)
