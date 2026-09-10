# Components to buy

Requirement: ESP32 board with WiFi (to publish to Home Assistant, e.g. via
MQTT), plus an NFC reader that actually supports **ISO 15693** — this is
the detail that matters most and rules out the usual cheap default.

## NFC reader: PN5180 module — **not sold by electrokit**

Checked electrokit.com directly: they carry the **PN532** (`NFC+RFID
13.56MHz shield PN532`, `RFID shield for Arduino 13.56MHz`) and a couple
of RFID readers (USB Olimex, UHF 868MHz), but nothing based on the
**PN5180**.

This matters because the PN532 is ISO14443A/FeliCa-focused and does not
properly support ISO15693 ("Type V") — the protocol our ST25DV04K-I tag
uses. It's the same reason the Home Assistant community's Zenner-meter
project (Mifare Ultralight/ISO14443) could use a PN532, while the
Qalcosonic W1 project (ISO15693, like ours) specifically needed a PN5180.
Buying a PN532 here would be a dead end.

**Source from AliExpress/Amazon/eBay instead** — search "PN5180 NFC
module". Commonly ~$5-10, SPI interface, 3.3V logic (separate 5V feed for
the RF antenna only). This is the same module used in the
`dbmaxpayne/esphome_qalcosonicnfc` reference project. Any of the common
breakout boards work; no need for a specific brand.

## ESP32 dev board — using a board already on hand

**ESP32-C6 DevKit-1** — already owned, no need to buy. Same family as the
`ESP32-C6-DevKitC 8MB` electrokit carries (179 SEK), and matches the board
used in `esp32_watertank`, so it's consistent across projects.

WiFi 6 capable (2.4GHz, backward compatible with any router). 3.3V logic,
same as classic ESP32, so PN5180 wiring (3.3V logic + separate 5V for the
RF antenna) is unaffected. The one difference from the reference PN5180
projects (`dbmaxpayne/esphome_qalcosonicnfc` etc., written for classic
ESP32): their hardcoded SPI pins (GPIO18/19/23/14/16/17) won't carry over
1:1 — pick free GPIOs on the C6 for MOSI/MISO/SCK/CS/BUSY/RESET and set
them explicitly in config. Standard SPI, so this is a config change, not
a design change. If using ESPHome, confirm the core version in use has
C6 board support (`esp32-c6-devkitc-1` or similar variant).

## Supporting parts — all at electrokit

- [Jumper wires 1-pin male-female 150mm 10-pack](https://www.electrokit.com/en/product/jumper-wires-1-pin-male-female-150mm-10-pack/) — to wire the PN5180 breakout (pin headers) to the ESP32.
- A breadboard, if not already on hand. The previously-linked 1360-connection kit is discontinued; use [Breadboard 840 connections](https://www.electrokit.com/en/kopplingsdack-840-anslutningar) (bare, no backing plate — fine if taping/rubber-banding the setup down near the meter as planned) plus [jumper wires for breadboard, 140-pack](https://www.electrokit.com/en/kopplingstrad-byglar-for-kopplingsdack) separately, since electrokit no longer bundles board+wires together.
- **USB-C cable**, not Micro USB — the ESP32-C6 DevKitC-1 uses USB-C (unlike the classic WROOM-32 board originally considered). E.g. [USB-C to USB-A cable, 15W, 1m](https://www.electrokit.com/en/usb-kabel-a-hane-c-hane-0.5m) (short/cheap is fine — no need for a data-rated cable, just power+flashing).

## Power supply — for continuous operation

Once out of the prototype stage, this reader runs continuously on USB
power near the meter (see "Not needed yet" below — no battery required).
Use a simple USB-A wall charger plugged in near the meter, with the
USB-C cable above:

- [USB charger, 2-port, 12W 2.4A](https://www.electrokit.com/usb-laddare-2port-12w-2.4a-vit) — plenty of headroom; the ESP32-C6 + PN5180 draw well under 1A even with the RF field active. A single-port 5V/1A charger (e.g. [Power supply 5VDC 5W 1A](https://www.electrokit.com/en/natadapter-5vdc-1a-5w)) would also work, but the 2.4A one leaves room if a future revision adds more current draw (e.g. a status LED, relay, or second sensor).

## Not needed yet

- Enclosure/mount to hold the reader against the meter's NFC location —
  skip for the breadboard-prototype stage; double-sided tape or a rubber
  band is enough to hold the PN5180 antenna in place against the meter
  housing for initial testing. Worth a small 3D-printed bracket later
  (see the Axioma Qalcosonic project on Thingiverse for a design to
  reference), once read reliability is confirmed.
- Deep-sleep/battery hardware — this reader can just run on USB power
  continuously near the meter; no need to battery-power it like the
  meter itself.

## Total estimate

~$5-10 (PN5180, external source) + ~150-200 SEK (breadboard, jumper
wires, USB-C cable, wall charger — skip anything already on hand) — the
ESP32-C6 DevKit-1 itself is already owned, no board purchase needed.
Cheapest and least certain part is confirming a specific PN5180 module
works well via SPI+ESP32 — worth ordering one first and validating a
basic block read before buying anything for a permanent mount.
