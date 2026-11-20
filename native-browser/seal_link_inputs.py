"""Seal the verified native archive paths for one isolated firmware link."""
from pathlib import Path
import hashlib
import json
import argparse
import subprocess

root = Path(__file__).resolve().parent
source = Path('D:/crossloop-netsurf-probe-20261114/source/netsurf-all-3.11')
parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build-esp32s3')
parser.add_argument('--audit', default='native-symbols.json')
parser.add_argument('--output', default='reader-link-inputs.json')
args = parser.parse_args()
build = root / args.build
cache = {}
for line in (build / 'CMakeCache.txt').read_text().splitlines():
    if ':' in line and '=' in line and not line.startswith(('#', '//')):
        key, value = line.split('=', 1)
        cache[key.split(':', 1)[0]] = value
reader = Path(cache['CROSSLOOP_SOURCE']).resolve()
frontend = (root / 'native-probe/frontend').resolve()
pending = subprocess.check_output([cache['CMAKE_MAKE_PROGRAM'], '-C', str(build), '-n'], text=True)
if 'ninja: no work to do.' not in pending:
    raise RuntimeError('Native build inputs changed; rebuild before sealing their header identities')
dependencies = subprocess.check_output([cache['CMAKE_MAKE_PROGRAM'], '-C', str(build), '-t', 'deps'], text=True)
header_paths = set()
for line in dependencies.splitlines():
    path = Path(line.strip())
    if path.is_absolute() and path.suffix.lower() in {'.h', '.hpp'} and path.is_file():
        path = path.resolve()
        if path.is_relative_to(reader) or path.is_relative_to(frontend):
            header_paths.add(path)
if not any(path.is_relative_to(reader) for path in header_paths):
    raise RuntimeError('Native build has no tracked headers from the selected reader checkout')
headers = [{'path': path.as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
           for path in sorted(header_paths)]
names = ['libnetsurf_frontend.a', 'libnetsurf_core.a', 'liblibcss.a', 'liblibdom.a',
         'liblibhubbub.a', 'liblibparserutils.a', 'liblibwapcaplet.a', 'liblibnsutils.a',
         'third_party/zlib-1.4/libz.a']
symbols = json.loads((root / args.audit).read_text())
if symbols['duplicate_strong_symbols']:
    raise RuntimeError('Unresolved duplicate strong symbols')
archives = []
for name in names:
    path = build / name
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    audited = next(item for item in symbols['archives'] if item['archive'] == path.name)
    if audited['sha256'] != digest:
        raise RuntimeError(f'Rerun symbol audit for {path.name}')
    archives.append({'path': path.as_posix(), 'bytes': len(data), 'sha256': digest})
includes = [root / 'native-probe/frontend', source / 'netsurf', source / 'netsurf/include',
            source / 'inst-monkey/include']
receipt = {'archives': archives, 'reader_root': reader.as_posix(), 'interface_headers': headers,
           'include_dirs': [p.as_posix() for p in includes],
           'stage': 'Experimental local firmware link inputs; no firmware/device acceptance.'}
(root / args.output).write_text(json.dumps(receipt, indent=2) + '\n')
print(f'Sealed {len(archives)} native archives and {len(headers)} interface headers for the isolated firmware link.')
