"""Embed exact publisher CSS and generated English messages into const data."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parent
source = Path('D:/crossloop-netsurf-probe-20261114/source/netsurf-all-3.11/netsurf')
out = root / 'native-probe/frontend'
# This profile keeps ad blocking disabled; its unused stylesheet is not embedded.
files = [(n, source / 'resources' / n) for n in ('default.css', 'quirks.css', 'internal.css')]
files.append(('Messages', source / 'frontends/monkey/res/en/Messages'))
lines = ['/* Generated from verified NetSurf 3.11 resources. GPL-2.0. */', '#include "ProbeResources.h"', '#include <string.h>']
entries = []
for index, (name, path) in enumerate(files):
    data = path.read_bytes()
    symbol = f'probe_resource_{index}'
    lines.append(f'static const uint8_t {symbol}[] = {{')
    for start in range(0, len(data), 24):
        lines.append('  ' + ','.join(str(b) for b in data[start:start + 24]) + ',')
    lines.append('};')
    entries.append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
lines += ['nserror probe_resource_get(const char *name, const uint8_t **data, size_t *length) {',
          '  if (!name || !data || !length) return NSERROR_BAD_PARAMETER;']
for index, (name, _) in enumerate(files):
    lines.append(f'  if (!strcmp(name, "{name}")) {{ *data = probe_resource_{index}; *length = sizeof(probe_resource_{index}); return NSERROR_OK; }}')
lines += ['  *data = NULL; *length = 0; return NSERROR_NOT_FOUND;', '}',
          'nserror probe_resource_release(const uint8_t *data) {']
for index in range(len(files)):
    lines.append(f'  if (data == probe_resource_{index}) return NSERROR_OK;')
lines += ['  return NSERROR_BAD_PARAMETER;', '}']
(out / 'ProbeResources.c').write_text('\n'.join(lines) + '\n', encoding='utf-8')
(root / 'resources-receipt.json').write_text(json.dumps(entries, indent=2) + '\n')
print(json.dumps(entries))
