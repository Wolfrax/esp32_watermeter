# esp32_watermeter

WiFi-connected ESP32 reader for the household water meter (Sagemcom Siconia
WM20-L / SK20-MI001-SMU061, installed by VASYD), publishing consumption to
Home Assistant.

Status: **hardware bring-up validated end-to-end (2026-09-10)** — the
ESP32-C6 + PN5180 reader, wired per [docs/wiring.md](docs/wiring.md),
read the meter's tag live and matched the known UID, capturing the
first complete 512-byte dump (see
[docs/findings.md](docs/findings.md#validated-full-read-via-esp32-c6--pn5180-2026-09-10)).
Production firmware (WiFi, MQTT, OTA, Home Assistant) not started yet
— the current code is only the throwaway bring-up sketch in
[bringup/](bringup/).

The meter's LoRaWAN
uplink is encrypted and off-limits, and its LCD is normally blank (only
lights up on the physical wake button — not usable for automated reading).
Instead, the meter exposes an **ISO 15693 NFC tag (ST25DV04K-I)** that holds
plaintext daily consumption history. This has been read by hand with a
phone (STMicroelectronics "NFC Tap" app) and the data format has been
partially decoded — see [docs/findings.md](docs/findings.md).

Next step: production firmware — WiFi, MQTT publish to `rpi4.local`,
OTA updates, Home Assistant integration. See "Open decision" in
[docs/wiring.md](docs/wiring.md) for the ESP-IDF vs. ESPHome framework
choice still to be made.

## Repo contents

- [docs/findings.md](docs/findings.md) — full write-up of the investigation:
  meter identification, why LoRaWAN/display/camera-OCR were ruled out, the
  NFC tag details, and the decoded data format.
- [docs/dumps/](docs/dumps/) — raw memory dumps pulled from the tag:
  early partial dumps via phone (`data.bin` 124B, `mem2.bin`/`mem3.bin`
  ~396B), and the first complete dump via the ESP32+PN5180 reader
  (`mem_full_esp32_20260910.bin`, full 512B).
- [docs/analyze_dump.py](docs/analyze_dump.py) — script used to find the
  BCD date fields and volume values in the dumps; rerun against any new
  dump to re-check/extend the decode.
- [docs/bom.md](docs/bom.md) — components to buy, with electrokit.com
  availability.
- [docs/wiring.md](docs/wiring.md) — PN5180 ↔ ESP32-C6-DevKitC-1 wiring
  (pinout, GPIO assignment, safety notes) and the hardware bring-up
  test plan.
- [bringup/PN5180_bringup/](bringup/PN5180_bringup/) — throwaway
  Arduino-core sketch used to validate the wiring; not the production
  firmware.

- [esp32/](esp32/) — the production ESP-IDF firmware (plain C, no
  Arduino/ESPHome — see "framework" decision in
  [docs/wiring.md](docs/wiring.md)). **Flashed and confirmed working
  end-to-end 2026-09-10**: WiFi connects, reads the meter tag, and
  publishes state + availability + Home Assistant MQTT-discovery
  config to `rpi7.local`, all verified live on the broker.

## Production firmware (esp32/) — status and how to build

WiFi (NVS-backed credentials, mirrors the `esp32_bme280` project's
`wifi.c`) + PN5180/ISO15693 driver (ported from
`wilson-elechouse/PN5180_ELECHOUSE`'s Arduino source to ESP-IDF's
`spi_master`) + the meter-specific decode + MQTT publish (with a Home
Assistant MQTT-discovery config message) + a continuous polling loop.

**Polling interval is deliberately short for now** (30 min, see
`POLL_INTERVAL_MS` in `esp32/main/globals.h`) rather than once daily —
the tag's update cadence isn't fully characterized yet (see
[docs/findings.md](docs/findings.md)), so this collects enough samples
to see it empirically before dialing back to a more efficient
interval. Every published MQTT message includes a `changed_since_last`
field for exactly this purpose.

OTA is wired up matching the `esp32_watertank` project's convention
exactly (ported `ota.c`/`ota.h` unchanged): a plain-text manifest
(version + `.bin` URL) polled from `OTA_MANIFEST_URL`, `esp_https_ota`
to apply it, and the same rollback-safety flow — a freshly-flashed OTA
image that can't even reach WiFi triggers an automatic revert to the
previous image. MQTT broker corrected to `rpi7.local` (not `rpi4.local`
as first scaffolded — cross-checked against `esp32_watertank`'s
`globals.h`, which has the same host/broker documented).

**Not done yet:**
- Nothing is being served yet at `OTA_MANIFEST_URL` — `ota_check_and_update()`
  degrades gracefully (logs + skips) until that's set up, matching how
  `esp32_watertank` bootstraps a new project before its manifest exists.
- WiFi credentials are filled in; MQTT has no auth configured
  (`MQTT_USERNAME`/`MQTT_PASSWORD` empty) — fine if the broker allows
  anonymous connections, same as `esp32_watertank`.
- OTA manifest hosting (nothing served at `OTA_MANIFEST_URL` yet).

**To build:**

```sh
source ~/.espressif/v6.0/esp-idf/export.sh   # or wherever ESP-IDF is installed
cd esp32
idf.py set-target esp32c6   # only needed once
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # native USB port; use /dev/ttyUSB0 for the UART bridge port instead
```
