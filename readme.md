# WLED BLE JSON usermod

WLED runtime lighting control over direct BLE GATT. Independent external usermod;
no WLED core edits. Protocol: [protocol.md](protocol.md).

**Opt-in beta firmware. Hardware, factory OTA and real-phone qualification is not complete.**
[Beta 0.1.0-beta.1](https://github.com/boardsesh/wled-usermod-ble-json/releases/tag/0.1.0-beta.1)
can be installed by the wall owner through Boardsesh after explicitly accepting
beta testing and confirming the controller model. The first beta accepts only a
Gledopto GL-C-015WL-D with classic ESP32, 4 MB flash, and stock WLED 16.0.1
(`/json/info` brand `WLED`, product `FOSS`). Other factory/custom builds remain
unsupported until a matching source profile is added. A successful compilation
does not establish OTA safety. Export the configuration/preset backup before
installing; interrupted installation or changed settings may require recovery.

The manifest identifies these releases as `channel: "beta", qualified: false`.
Neither the beta label nor owner consent claims hardware qualification. Audio is
preferred; the separately labeled no-audio fallback needs additional owner consent.
Boardsesh clients that only accept qualified releases keep installation disabled.

## Use in another WLED build

Pinned baseline: WLED 16.0.1, commit `29b389df1c1aaec6ff53aea742d17063b985906c`.
Pinned radio library: NimBLE-Arduino 2.5.2. PlatformIO 6.1.18.
Use WLED's standalone mechanism:

```ini
[env:my_ble_controller]
extends = env:esp32dev
build_flags = ${env:esp32dev.build_flags} -fno-common
custom_usermods =
  ${env:esp32dev.custom_usermods}
  https://github.com/boardsesh/wled-usermod-ble-json.git#<reviewed-commit>
```

The compiler flag prevents tentative NimBLE globals from resolving against the
pinned Tasmota core's older bundled privacy object. Full-feature generic builds
may need the optional-integration trims in the supplied profiles to fit 4 MB OTA.

Generic builds default to disabled: enable **BLE JSON / enabled** in WLED Usermod
Settings. Boardsesh build profiles default enabled on first install only. A saved
false setting is respected on subsequent updates. No GPIO, LED, relay, microphone,
network, preset, or power-limit configuration is hardcoded or persisted by this mod.
No pairing or bonding is required. Any nearby client can control runtime lighting
while enabled. The service deliberately cannot configure Wi-Fi, reboot, save
presets or install firmware.

## Build and verify candidates

```sh
python -m pip install platformio==6.1.18
git clone https://github.com/wled/WLED.git ../WLED
git -C ../WLED checkout 29b389df1c1aaec6ff53aea742d17063b985906c
python tools/build.py --wled ../WLED --environment boardsesh_ble_audio
python tools/build.py --wled ../WLED --environment boardsesh_ble_no_audio
c++ -std=c++17 -Wall -Wextra -Werror -I../WLED/wled00/src/dependencies/json -Isrc tests/host.cpp -o /tmp/ble-json-host
/tmp/ble-json-host test-vectors.json
```

The pinned build configuration retains the standard 4 MB partition table with two
`0x180000` (1.5 MiB) application slots. The build tool refuses modified partitions
and oversized images, and produces a candidate `.bin`, SHA-256 and size report.
It never flashes hardware. Both profiles preserve LED drivers, the JSON API,
web settings, Wi-Fi and OTA; unrelated integrations are trimmed before audio.
The audio-disabled profile is separately identified and must never be silently
substituted for the audio build. The no-audio module retains the dormant
`AudioReactive` configuration through WLED saves, including microphone settings,
so a later audio-enabled update can restore that configuration. WLED source/build inputs are pinned; upstream
build timestamps and dependency resolution are not yet proven bit reproducible.

BLE callbacks only buffer bounded frames and connection events. Validation and
WLED state application happen on the main loop, under the JSON lock. Indications
are sent one fragment at a time, waiting for acknowledgement. Disconnect, fragment
timeout and OTA suspension discard pending work. Invalid framing closes the
connection because no request ID can safely be recovered. Well-framed invalid
requests return explicit errors. Acknowledgement confirms WLED applied the
request; it cannot confirm physical LED output.

## Qualification before a beta becomes stable

First target: Gledopto GL-C-015WL-D, classic ESP32, 4 MB. Record exact source
firmware identity, partition table and OTA slot size on real hardware. Test an
in-place `/update` upload from the factory firmware and stock WLED 16.0.1, without
repartitioning or replacing filesystem contents. Verify preserved LED count,
chipset, color order, GPIOs, relay, microphones, Wi-Fi, presets and saved BLE setting.
Test locked OTA/PIN, interrupted upload, reboot identification, subsequent update,
and stock-WLED return. Repeat for the separately named no-audio build if offered.

Run real Android/iOS 500/1000-pixel lighting, configured bounds, clear/replace,
brightness/nightlight rearming, audio/climbing switching, two-phone handoff,
background, sign-out and another board connection. Obtain independent Astra BLE
review. Record evidence and artifact hash before marking any release qualified or
stable. The beta manifest uses only the verified CI-built assets with matching
size/SHA-256 reports; it does not change the image, partition table, or controller
settings. Betas require explicit owner consent in addition to source-profile,
architecture, flash-size, model, backup, checksum and OTA-lock checks. The owner
confirms and initiates every installation; publishing a beta never flashes a device.
