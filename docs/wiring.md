# Wiring: Elechouse PN5180 → ESP32-C6-DevKitC-1

Confirmed against the official [Elechouse PN5180 product
manual](https://www.elechouse.com/wp-content/uploads/2025/12/PN5180_Product_Manual.pdf)
(board photo in the manual matches ours exactly — same silkscreen, same
"SPI=3V3" jumper, same 10-pin 1.25mm connector).

## Main connector pinout (authoritative, from the manual)

10-pin, HX1.25mm pitch, **top to bottom** as printed on the PCB next to
the connector (zoom in on your own board's silkscreen to confirm which
physical end is "top" before trusting wire colors):

| # | Pin | Type | Notes |
|---|-----|------|-------|
| 1 | PD/CE | Input | Chip enable / hard power-down. High=enabled. Optional but recommended: lets firmware power-cycle the chip's logic core to recover from a stuck state without a physical reset. |
| 2 | 5V | Power | **RF antenna supply. Must be 5V, not 3.3V.** |
| 3 | PVDD | Ref | Logic-level reference, 1.65–3.6V. Tie to the host's IO voltage. |
| 4 | GND | Power | Ground |
| 5 | NSS | Input | SPI chip select (active low) |
| 6 | MOSI | Input | SPI data in |
| 7 | MISO | Output | SPI data out |
| 8 | SCK | Input | SPI clock |
| 9 | BUSY | Output | Flow-control line — **required**, the driver polls this before every SPI transaction |
| 10 | RST | Input | Soft reset |

There's also a 4-pad unpopulated extension header (REQ/AUX1/IRQ/GPO1)
near the antenna mounting holes — visible in the photo as 4 plain
through-holes. **Skip it**: those pins are for low-power card-detection
wake-up, which this project doesn't need (the reader runs continuously
on USB power, no deep sleep — see [bom.md](bom.md)). No header is
soldered there and none is needed.

### Logic-level jumper ("SPI=3V3")

The board has a solder jumper for PVDD. **Don't touch it** — leave it in
its default (open) state, and instead wire pin 3 (PVDD) directly to the
ESP32's 3.3V rail. Electrically equivalent to bridging the jumper, no
soldering required.

### Wire colors — confirmed from board photo

The board's silkscreen prints the pin labels directly beside the
connector, which let us read the wire colors against their labels
directly rather than guess a generic convention — useful since this
cable does **not** follow the usual black=GND/red=power convention.
Confirmed 2026-09-09 against a sharp macro photo (an earlier blurrier
photo had misread several of the middle wires — this table is the
corrected version):

| PN5180 pin | Wire color |
|---|---|
| PD/CE | red |
| 5V | black |
| PVDD | yellow |
| GND | green |
| NSS | blue |
| MOSI | white |
| MISO | orange |
| SCK | purple |
| BUSY | gray |
| RST | brown |

**Before powering anything**, it's still worth a continuity check
between GND (green) and the big gold mounting-hole ring (almost
certainly tied to the ground plane on an RF board) — should beep.
Cheap insurance given this color reading has already been wrong once.

## ESP32-C6-DevKitC-1 side

Pins chosen to avoid: strapping pins (GPIO4, 5, 8, 9, 15), the
USB-Serial/JTAG pins (GPIO12, 13, used by the native USB-C port for
flashing/console), and UART0 (GPIO16/17, kept free). GPIO8 also drives
the onboard WS2812 status LED — left alone.

| PN5180 pin | Wire color | ESP32-C6 GPIO | Notes |
|---|---|---|---|
| PD/CE | red | GPIO10 | Optional, recommended for reliability |
| 5V | black | 5V (VBUS) | From the DevKit's 5V pin, **not** 3V3 |
| PVDD | yellow | 3V3 | See jumper note above |
| GND | green | GND | |
| NSS (CS) | blue | GPIO1 | |
| MOSI | white | GPIO6 | |
| MISO | orange | GPIO3 | |
| SCK | purple | GPIO2 | |
| BUSY | gray | GPIO7 | |
| RST | brown | GPIO0 | |

Both boards run off the same 5V USB supply for now (bench test); the
PN5180 can surge to ~250mA during RF transmission, well within a
standard USB port/charger's budget alongside the ESP32-C6.

## Bring-up test plan (validate hardware before writing production firmware)

Goal: prove the SPI link and the actual meter tag are readable, before
investing in the MQTT/HA/OTA firmware. Fastest path is a throwaway
Arduino-core sketch (PlatformIO or Arduino IDE with ESP32-C6 board
support), independent of whatever framework the production firmware
ends up using:

1. **Pre-power check**: multimeter continuity check across the
   assembled harness — confirm no short between 5V and 3V3/GND/any
   signal pin.
2. **ESP32 alone**: power the DevKit via USB with nothing wired yet,
   confirm it enumerates as a serial device and a blink sketch runs.
   Sanity check before adding the module.
3. **Wire per the table above**, then flash
   [`bringup/PN5180_bringup/PN5180_bringup.ino`](../bringup/PN5180_bringup/PN5180_bringup.ino)
   — a throwaway Arduino-core sketch built against the exact pin
   assignment above and the `PN5180 Library`/`PN5180_ELECHOUSE` API
   (`PN5180ISO15693`, `getInventory`, `readSingleBlock`, ...). It:
   - Hard-reboots the chip via PD/CE, then reads back the product/
     firmware/EEPROM version registers over SPI — a sane (non-0xFF)
     value is the checkpoint that proves SPI + BUSY wiring is correct,
     independent of any tag being present.
   - Polls for an ISO15693 tag and, on the actual water meter, checks
     the UID against the already-known `E0:02:24:69:31:38:49:E3`
     (from findings.md) — the end-to-end proof the antenna sees the
     real target, not just some other nearby tag.
   - Dumps every block it can read over serial — also a chance to
     finally capture the full 512 bytes (past phone dumps only got
     ~396B, see findings.md "Not yet decoded").

   Board setting: "ESP32C6 Dev Module" in Arduino IDE/arduino-esp32
   core, **with "USB CDC On Boot" set to Enabled**
   (`esp32:esp32:esp32c6:CDCOnBoot=cdc` as an arduino-cli FQBN) — it
   defaults to Disabled, which silently routes `Serial` to UART0
   (GPIO16/17) instead of the native USB port, so the console looks
   dead even though the sketch is running fine. Confirmed
   2026-09-09: board flashed and console read via arduino-cli +
   esptool from this session, output above.

   Library: the Arduino Library Manager's registered "PN5180" (by
   Playful Technology) is a **different, incompatible library** — this
   sketch is written against `wilson-elechouse/PN5180_ELECHOUSE`
   (a.k.a. Andreas Trappmann's "PN5180 Library"), which isn't in the
   registry. Install it manually: clone
   `https://github.com/wilson-elechouse/PN5180_ELECHOUSE` into your
   Arduino `libraries/` folder.

   This board also has two USB-C ports — a native USB port
   (enumerates as Espressif `303a:1001`, "USB JTAG/serial debug unit",
   used for flashing and, with CDCOnBoot=cdc, the console) and a
   separate UART-bridge port (used only if CDCOnBoot is left
   Disabled). Use the native one for both flashing and monitoring.
4. Only after that checkpoint passes, move to the production firmware
   (WiFi, MQTT to rpi4.local, OTA, Home Assistant integration).

### Toolchain used for the bring-up test

`arduino-cli` (installed to `~/bin`, esp32 core + library added to
`~/Arduino/libraries/PN5180_ELECHOUSE`), not the full Arduino IDE:

```sh
FQBN="esp32:esp32:esp32c6:CDCOnBoot=cdc"
arduino-cli compile --fqbn "$FQBN" bringup/PN5180_bringup
arduino-cli upload -p /dev/ttyACM0 --fqbn "$FQBN" bringup/PN5180_bringup
# then read the console, e.g.: python3 -c "import serial,sys; ..." or arduino-cli monitor
```

Note: this board's own WiFi/NVS data from a prior unrelated project
was already on it before this test — irrelevant here, since flashing
via arduino-cli/esptool overwrites the bootloader and partition table
too, and this sketch doesn't touch WiFi/NVS at all.

## Production firmware framework: ESP-IDF (decided 2026-09-10)

Plain ESP-IDF (C), matching the existing `esp32_bme280` project's
approach (own `wifi.c`, custom main loop, no Arduino/ESPHome).

The PN5180 driver from the bring-up sketch
(`wilson-elechouse/PN5180_ELECHOUSE`, Arduino-only) doesn't carry over
directly — either port its SPI/register logic to raw ESP-IDF
`driver/spi_master.h` calls (it's a fairly thin register-level driver,
see its `PN5180.cpp`/`PN5180ISO15693.cpp`), or pull in `arduino-esp32`
as an ESP-IDF managed component to reuse it as-is. Worth deciding once
firmware work actually starts.

(ESPHome was the other option considered — gets WiFi/MQTT/OTA for
free via YAML, and `dbmaxpayne/esphome_qalcosonicnfc` is a working
ESPHome external component for PN5180+ISO15693 to start from — but
doesn't match this project's established toolchain, so not chosen.)
