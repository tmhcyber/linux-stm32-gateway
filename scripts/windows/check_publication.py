"""Verify selected public evidence, application inputs and Markdown links."""
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]


def check():
    artifacts = json.loads((ROOT / 'evidence/MANIFEST.json').read_text(encoding='utf-8'))['artifacts']
    for artifact in artifacts:
        digest = hashlib.sha256((ROOT / artifact['path']).read_bytes()).hexdigest()
        assert digest == artifact['public_sha256'], artifact['path']
        if artifact['kind'] == 'original':
            assert digest == artifact['original_sha256'], artifact['path']
    inputs = json.loads((ROOT / 'evidence/SOURCE_CODE.json').read_text(encoding='utf-8'))['files']
    for name, expected in inputs.items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == expected, name
    reference = ROOT / 'fixtures/mcu-stage6'
    mcu = json.loads((reference / 'host-result.json').read_text(encoding='utf-8'))
    for name, expected in mcu['sources'].items():
        assert hashlib.sha256((reference / name).read_bytes()).hexdigest() == expected, name
    assert hashlib.sha256((reference / 'vectors.bin').read_bytes()).hexdigest() == mcu['vectors_sha256']
    count = 0
    for path in [ROOT / 'README.md', *(ROOT / 'docs').rglob('*.md'),
                 *(ROOT / 'evidence').rglob('*.md'), *(ROOT / 'fixtures').rglob('*.md')]:
        text = re.sub(r'```.*?```', '', path.read_text(encoding='utf-8'), flags=re.S)
        for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', text):
            if re.match(r'\w+://|mailto:|#', target):
                continue
            name = unquote(target.split('#')[0])
            assert (path.parent / name).exists(), (path.relative_to(ROOT), target)
            count += 1
    for path in [*(ROOT / 'evidence').rglob('*.json'), reference / 'host-result.json']:
        json.loads(path.read_text(encoding='utf-8-sig'))
    return dict(passed=True, artifacts=len(artifacts), source_inputs=len(inputs), local_links=count)


if __name__ == '__main__':
    print(json.dumps(check(), indent=2))
