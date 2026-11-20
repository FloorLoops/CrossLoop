"""Report archive allocation ownership and unresolved platform dependencies."""
from pathlib import Path
import subprocess
import json
import hashlib
import argparse

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build-esp32s3')
parser.add_argument('--output', default='native-symbols.json')
args = parser.parse_args()
build = root / args.build
tool = Path('D:/The Primordial/ChatGPT/Non-Project Chats/2026-11-05/xteink-crosspoint-plugins-settings/work/x4-pio-core/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe')
archives = sorted(build.glob('*.a')) + [build / 'third_party/zlib-1.4/libz.a']
defined, undefined = set(), set()
records = []
strong_definitions = {}
plain_allocators = {'malloc', 'calloc', 'realloc', 'free', 'strdup', 'strndup'}
for archive in archives:
    output = subprocess.check_output([str(tool), '-g', str(archive)], text=True)
    own_defined, own_undefined = set(), set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in 'TDBRVWGCUS':
            (own_undefined if parts[-2] == 'U' else own_defined).add(parts[-1])
            if parts[-2] in 'TDBRGS':
                strong_definitions.setdefault(parts[-1], []).append(archive.name)
    defined |= own_defined
    undefined |= own_undefined
    if archive.name == 'libz.a':
        assert 'z_inflate' in own_defined
        assert 'crossloop_ns_z_errmsg' in own_defined and 'z_errmsg' not in own_defined
        assert not ({'inflate', 'deflate', 'crc32', 'adler32', 'zlibVersion', 'zcalloc', 'zcfree'} & own_defined)
    records.append({'archive': archive.name, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                    'plain_allocator_references': sorted(plain_allocators & own_undefined),
                    'engine_allocator_references': sorted(s for s in own_undefined if s.startswith('engine_memory_'))})
remaining = sorted(undefined - defined)
duplicates = {symbol: owners for symbol, owners in strong_definitions.items() if len(owners) > 1}
report = {'archives': records, 'unresolved_after_archive_union': remaining, 'duplicate_strong_symbols': duplicates,
          'proof': 'Symbol inventory only, not a firmware link or execution.'}
(root / args.output).write_text(json.dumps(report, indent=2) + '\n')
assert all(not r['plain_allocator_references'] for r in records), records
assert not duplicates, duplicates
print('All selected archives reference only the engine allocation family.')
print('Unresolved platform/core symbols:', len(remaining))
print('No duplicate strong symbols; browser compression symbols use z_ prefix.')
