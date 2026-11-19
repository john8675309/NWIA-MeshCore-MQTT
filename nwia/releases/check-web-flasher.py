#!/usr/bin/env python3
"""Verify the static site's manifest, release hash and bundled module imports."""
import hashlib
import json
from pathlib import Path
import re

site = Path(__file__).resolve().parents[2] / 'web-flasher'
release = json.loads((site / 'release.json').read_text())
manifest = json.loads((site / 'manifest.json').read_text())
assert manifest['version'] == release['version']
assert len(manifest['builds']) == 1
build = manifest['builds'][0]
assert build['chipFamily'] == 'ESP32-S3'
assert build['parts'] == [{'path': release['path'], 'offset': 0}]
assert manifest['new_install_prompt_erase'] is True
assert manifest['new_install_improv_wait_time'] == 0
assert re.fullmatch(r'firmware/[a-zA-Z0-9._-]+\.bin', release['path'])
image = (site / release['path']).read_bytes()
assert len(image) == release['bytes']
assert hashlib.sha256(image).hexdigest() == release['sha256']
for module in site.rglob('*.js'):
    text = module.read_text()
    for path in re.findall(r'(?:from\s*|import\s*\()\s*["\'](\.[^"\']+)["\']', text):
        assert (module.parent / path).is_file(), (module.name, path)
assert (site / '.nojekyll').is_file()
assert (site / 'vendor/esp-web-tools/LICENSE').is_file()
print('PASS: flasher release/hash, ESP32-S3 offset, erase prompt and bundled imports')
