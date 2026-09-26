#!/usr/bin/env python3
"""Fetch the dated Europe extract once, with resumable ranges and its MD5 gate."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
from pathlib import Path
import socket
import time
import urllib.request


URL = 'https://download.geofabrik.de/europe-260101.osm.pbf'
SIZE = 33624798294
MD5 = 'b87b2c99294ffbc71f2de9bc5f0a04eb'
CHUNK = 128 << 20


def digest(path):
    h = hashlib.md5()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--connections', type=int, default=4, choices=range(1, 17))
    ap.add_argument('--server-ip', help='Optional public server address; retain the URL host for TLS and HTTP')
    args = ap.parse_args()
    if args.server_ip:
        original_resolver = socket.getaddrinfo
        def resolve(host, *rest, **kwargs):
            return original_resolver(args.server_ip if host == 'download.geofabrik.de' else host,
                                     *rest, **kwargs)
        socket.getaddrinfo = resolve
    print('SOURCE', URL, 'server', args.server_ip or 'DNS', 'connections', args.connections, flush=True)
    root = args.directory
    root.mkdir(parents=True, exist_ok=True)
    lock = (root / 'download.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    target = root / 'europe-260101.osm.pbf'
    if target.exists():
        assert target.stat().st_size == SIZE and digest(target) == MD5
        (root / 'download.complete').touch()
        print('ALREADY COMPLETE', target, flush=True)
        return
    parts = root / 'europe-260101.parts'
    parts.mkdir(exist_ok=True)

    def fetch(index):
        start = index * CHUNK
        end = min(SIZE, start + CHUNK) - 1
        length = end - start + 1
        path = parts / f'{index:04d}'
        if path.exists():
            assert path.stat().st_size == length
            return path
        partial = path.with_suffix('.part')
        for attempt in range(5):
            offset = partial.stat().st_size if partial.exists() else 0
            assert 0 <= offset <= length
            if offset == length:
                partial.rename(path)
                return path
            request = urllib.request.Request(URL, headers={
                'Range': f'bytes={start + offset}-{end}',
                'User-Agent': 'ACIC-reproducible-input/1.0'})
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    assert response.status == 206, response.status
                    assert response.headers['Content-Range'] == f'bytes {start + offset}-{end}/{SIZE}'
                    with partial.open('ab') as output:
                        for block in iter(lambda: response.read(1 << 20), b''):
                            if output.tell() + len(block) > length:
                                raise ValueError('range response exceeds requested length')
                            output.write(block)
                assert partial.stat().st_size == length
                partial.rename(path)
                print('RANGE COMPLETE', index, length, flush=True)
                return path
            except Exception as error:
                print('RANGE RETRY', index, attempt + 1, repr(error), flush=True)
                if attempt == 4:
                    raise
                time.sleep(5)

    count = (SIZE + CHUNK - 1) // CHUNK
    with ThreadPoolExecutor(max_workers=args.connections) as pool:
        paths = list(pool.map(fetch, range(count)))
    assembled = target.with_suffix('.pbf.assembling')
    h = hashlib.md5()
    with assembled.open('wb') as output:
        for path in paths:
            with path.open('rb') as source:
                for block in iter(lambda: source.read(4 << 20), b''):
                    output.write(block)
                    h.update(block)
    assert assembled.stat().st_size == SIZE and h.hexdigest() == MD5
    assembled.rename(target)
    (root / 'download.complete').write_text(f'{MD5}  {target.name}\n')
    print('COMPLETE', target, SIZE, MD5, flush=True)


if __name__ == '__main__':
    main()
