# Findings: reading the household water meter

## The meter

- Manufacturer/model: **Sagemcom Siconia WM20-L** (radio/comms module label),
  metrology body marked **SK 20-MI001-SMU061**.
- Installed by VASYD (Malmö/Lund/Burlöv area water utility), commissioned
  07/2023. MID Class 2, DN20, Q3 4.0 m³/h, battery valid until 2037.
- Ultrasonic (no moving parts), ships with LoRaWAN radio for the
  utility's own remote reading.
- Serial numbers on the label: `WT2319969009871`, `254039567`.

## Paths ruled out

- **LoRaWAN**: the meter transmits over LoRaWAN to VASYD, but the payload
  is encrypted end-to-end. Not viable without the utility's session key
  (and they won't hand it out — a key would let anyone spoof readings).
- **Camera + OCR of the LCD**: the display is blank by default (confirmed
  by photo — zero segments, not just hard to photograph) and only lights
  up for a limited window after a **physical press of the blue button**
  on the meter. There is no documented scheduled/automatic wake. Camera
  OCR would require a servo/solenoid to press a button on a sealed,
  MID-calibrated utility-owned device on every read cycle — mechanically
  possible but undesirable (moving parts on someone else's sealed
  equipment). Parked as a fallback only.

## The path that worked: NFC

The meter also carries a local **NFC interface**, confirmed for this
specific module on the LoRa Alliance's WM20-L listing ("the application
and communication module includes the display, the main LoRaWAN module
and the NFC based local interface").

### Tag identification

Read with a phone (iPhone, iOS NFC):

- **UID**: `E0:02:24:69:31:38:49:E3`
  - `E0` = fixed ISO15693 tag indicator byte.
  - `02` = IC manufacturer code for **STMicroelectronics**.
- **Chip**: **ST25DV04K-I** (confirmed via STMicroelectronics' own "NFC Tap"
  app) — a dual-interface (I2C + RF) NFC/RFID EEPROM. The meter's own MCU
  writes to it over I2C; any ISO15693 reader can read it over RF, no
  pairing/auth needed.
- **Memory**: 512 bytes total, 128 blocks × 4 bytes.
- No NDEF wrapper (no capability container at block 0) — the meter writes
  raw structured data directly, not a standard NDEF text record like some
  other vendors' meters (e.g. Zenner) use.

Generic consumer apps (NFC Tools, NXP TagInfo) could read the UID but
failed to identify the chip or dump memory — likely because their
auto-detection relies on a "Get System Info" response matching a known
database entry, and this chip either isn't in it or needs the
vendor-specific app. **STMicroelectronics' own "NFC Tap" app (free, iOS
17+) correctly identified the chip and dumped memory.**

### Memory dumps

Two export attempts, in [docs/dumps/](dumps/):

- `data.bin` — 124 bytes (first partial export attempt)
- `mem2.bin` — 396 bytes (fuller export; `mem3.bin` is the same data minus
  its first 12 bytes — a different "start block" export mode in the app)

None of the exports reached the full 512 bytes — the tail (~116 bytes) is
still unread. `mem2.bin` is the most complete and should be the basis for
further decoding.

### Decoded format: daily consumption log

Scanning for BCD-encoded dates (`DD MM YY 00`, each nibble a valid decimal
digit) found a repeating record structure:

```
[4-byte volume, uint32 little-endian, units of 0.0001 m³] [4-byte date, BCD DD MM YY 00]
```

Three consecutive daily entries were found (dump captured 2026-08-26):

| Date | Volume |
|---|---|
| 2026-08-26 (today) | 26.3418 m³ |
| 2026-08-25 | 26.3135 m³ |
| 2026-08-24 | 26.2931 m³ |

Daily deltas (~28 L, ~20 L) are plausible household usage. **Cross-checked
against ground truth**: the meter's live LCD (woken via the button, same
day) showed **26.3620 m³** — slightly higher than the tag's "today" value
of 26.3418 m³, consistent with the tag holding a **midnight snapshot**
while the display shows the running total read later that day. This
match is what confirms the decode is correct and the data is genuinely
in the clear (unencrypted).

**Implication for the project**: this looks like a once-per-day snapshot,
not a live counter. Good enough for daily consumption tracking / leak
trend detection in Home Assistant. Not sufficient for real-time
"water running now" alerts — that would still need the display+camera
route if ever wanted.

### Not yet decoded

- A high-entropy byte range (~offset 0x78–0x9F in `mem2.bin`) — unknown,
  possibly a checksum, key material, or unrelated production data.
- A repeated 8-byte chunk (`23 70 00 69 00 00 26 91`) appearing at two
  separate offsets — unidentified, possibly a sequence/status field.
- Structured-looking bytes near the tail of the used region (~offset
  0x6c–0x9b) including another "today"-dated field — not decoded.
  Candidates by analogy with similar meters (e.g. the Zenner NFC tag
  reverse-engineered by the Home Assistant community): current/live
  volume, temperature, battery status, flow rate, serial number.

Re-run [analyze_dump.py](analyze_dump.py) against any new/fuller dump to
redo the date/volume scan.

## Validated: full read via ESP32-C6 + PN5180 (2026-09-10)

The ESP32-C6 + PN5180 reader (wiring in [wiring.md](wiring.md), test
sketch in [../bringup/PN5180_bringup](../bringup/PN5180_bringup)) was
attached to the meter and confirmed end-to-end:

- UID read over ISO15693 matched the known tag exactly:
  `E0:02:24:69:31:38:49:E3`.
- Captured the **full 512-byte tag** for the first time — past phone
  dumps topped out at ~396 bytes. Saved as
  [dumps/mem_full_esp32_20260910.bin](dumps/mem_full_esp32_20260910.bin).
  This resolves the previously-missing final ~116 bytes: they're just
  zero-filled/unused (all zero from offset ~0x188 to the end) — not
  hiding undecoded data, just unused tag capacity.
- Re-running `analyze_dump.py` against the new dump decoded three
  fresh daily entries, consistent with the original decode and with
  plausible daily usage:

  | Date | Volume |
  |---|---|
  | 2026-09-10 (today) | 26.6908 m³ |
  | 2026-09-09 | 26.6683 m³ |
  | 2026-09-08 | 26.6495 m³ |

  (Deltas ~19–23 L/day.) The record layout, offsets, and rolling
  daily-log behavior described above are confirmed correct against
  live data 15 days after the original phone-based decode.

- **Confirmed the tag is a static once-daily snapshot, not live**,
  independently reproducing the original "not a live counter" finding
  above with a precise measurement: re-read the tag a few hours later
  the same day and it was **byte-for-byte identical** to the morning
  dump — nothing on the tag changes intraday. Meanwhile a manual LCD
  reading taken at that second read showed **26.7033 m³**, vs. the
  tag's still-frozen "today" value of **26.6908 m³** — a 0.0125 m³
  (12.5 L) gap that's real same-day usage the tag hasn't recorded yet.
  Implication for polling design: polling more than once a day gets
  nothing extra; poll once daily, safely after the (likely midnight)
  snapshot write — e.g. once every few hours, or once around 01:00 —
  rather than relying on an exact write time.

## Prior art referenced

- Home Assistant community thread ["Water meter reading via
  NFC"](https://community.home-assistant.io/t/water-meter-reading-via-nfc/626124)
  — Zenner DE-20-MI001-PTB011 meter, Mifare Ultralight/NDEF (different tag
  tech than ours, ISO14443 not ISO15693), read via ESP32 + PN532 with a
  patched ESPHome component. Useful for the general MQTT/HA integration
  pattern (raw MQTT publish + `json_attributes_topic`, since HA sensor
  state is capped at 255 bytes).
- GitHub [`dbmaxpayne/esphome_qalcosonicnfc`](https://github.com/dbmaxpayne/esphome_qalcosonicnfc)
  — Axioma Qalcosonic W1 meter, **ISO15693** (same tag tech as ours), read
  via ESP32 + **PN5180** using the ATrappmann PN5180 Arduino library,
  decoding an OMS/M-Bus formatted payload. Closest architectural match —
  hardware choice (PN5180) and general approach should transfer directly,
  though the byte-level payload format will differ (ours isn't M-Bus
  framed, based on the decode so far).
