"""Compile-time snapshot identity; release BUILD.json carries the full manifest."""
import hashlib
import json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if (root/'SOURCE.json').exists():
    source=json.loads((root/'SOURCE.json').read_text())
    print(source['snapshot']+':'+source['source_manifest_sha256'])
else:
    inputs=sorted([*root.glob('src/*.c'),*root.glob('include/*.h'),root/'Makefile'])
    print('development:'+hashlib.sha256(b''.join(p.read_bytes() for p in inputs)).hexdigest())
