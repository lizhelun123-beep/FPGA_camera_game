"""Verify the exported source snapshot, frozen artifacts and vendored JS graph."""
import argparse
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    count = 0
    for row in (ROOT / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        expected, relative = row.split('  ', 1)
        path = (ROOT / relative).resolve()
        if not path.is_relative_to(ROOT) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError('File changed or missing: ' + relative)
        count += 1
    base = ROOT / 'web_upper/pc_vision/web/vendor/three'
    for path in base.rglob('*.js'):
        specs = re.findall(r'(?:from\s*|import\s*\(\s*)[\'"]([^\'"]+)[\'"]', path.read_text(encoding='utf-8'))
        for spec in specs:
            if spec.startswith('.'):
                dependency = path.parent / spec
            elif spec.startswith('three/addons/'):
                dependency = base / 'examples/jsm' / spec.removeprefix('three/addons/')
            elif spec == 'three':
                dependency = base / 'build/three.module.js'
            else:
                raise ValueError('Unresolved JS import: ' + spec)
            if not dependency.is_file():
                raise ValueError('Missing JS dependency: ' + str(dependency))
    files = [p for p in ROOT.rglob('*') if p.is_file() and not any(
        part in {'output', 'build', '__pycache__', '.git', '.venv', '.godot'} for part in p.relative_to(ROOT).parts)]
    largest = max(files, key=lambda p: p.stat().st_size)
    if largest.stat().st_size >= 25 * 1024 * 1024:
        raise ValueError('Unexpected large single file: ' + str(largest))
    print(f'PASS_PACKAGE_SHA256 files={count} THREE_IMPORTS_RESOLVED largest_bytes={largest.stat().st_size}')


if __name__ == '__main__':
    main()
