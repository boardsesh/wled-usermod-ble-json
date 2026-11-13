#!/usr/bin/env python3
"""Build candidates only. No flashing, publishing, or partition changes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

WLED_COMMIT = '29b389df1c1aaec6ff53aea742d17063b985906c'
APP_SLOT_BYTES = 0x180000
ROOT = Path(__file__).resolve().parents[1]


def run(*arguments, **options):
    return subprocess.run(arguments, check=True, **options)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wled', type=Path, required=True, help='Checkout of pinned WLED source')
    parser.add_argument('--environment', choices=['boardsesh_ble_audio', 'boardsesh_ble_no_audio'], default='boardsesh_ble_audio')
    parser.add_argument('--pio', default='pio')
    arguments = parser.parse_args()
    checkout = arguments.wled.resolve()
    actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != WLED_COMMIT:
        raise SystemExit('Refusing unpinned WLED source')
    partition = checkout / 'tools/WLED_ESP32_4MB_1MB_FS.csv'
    if subprocess.check_output(['git', '-C', str(checkout), 'diff', '--', str(partition)]):
        raise SystemExit('Refusing modified partition table')
    configuration = (ROOT / 'build/platformio_override.ini').read_text().replace('symlink://../wled-usermod-ble-json', f'symlink://{ROOT}')
    (checkout / 'platformio_override.ini').write_text(configuration)
    run(arguments.pio, 'run', '-d', str(checkout), '-e', arguments.environment)
    firmware = checkout / '.pio/build' / arguments.environment / 'firmware.bin'
    image = firmware.read_bytes()
    if len(image) > APP_SLOT_BYTES:
        raise SystemExit(f'Image {len(image)} exceeds existing OTA slot {APP_SLOT_BYTES}')
    output = ROOT / 'dist'
    output.mkdir(exist_ok=True)
    filename = f'wled-16.0.1-{arguments.environment}.bin'
    shutil.copyfile(firmware, output / filename)
    digest = hashlib.sha256(image).hexdigest()
    (output / f'{filename}.sha256').write_text(f'{digest}  {filename}\n')
    report = {'wledCommit': WLED_COMMIT, 'environment': arguments.environment, 'imageBytes': len(image), 'appSlotBytes': APP_SLOT_BYTES, 'sha256': digest, 'audio': arguments.environment == 'boardsesh_ble_audio', 'qualified': False}
    (output / f'{filename}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
