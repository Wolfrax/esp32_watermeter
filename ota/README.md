# ota

Manual, on-demand OTA staging for this project. **Not a permanent
service** — nothing listens on port 8000 unless you start it here
yourself:

```sh
cd ota
python3 -m http.server 8000
```

`OTA_MANIFEST_URL` in `esp32/main/globals.h` points at
`http://wolfrax.local:8000/manifest.txt`, so this only needs to be
running while you actually want the device to pick up an update —
start it, power-cycle (or otherwise reboot) the device, then stop it
again once it's confirmed.

`ota.c`'s OTA check only runs **once at boot**, right after WiFi
connects — not on a timer. A device that's already running won't
notice a new manifest until its next reboot. No physical/USB access
to the device is needed for this — power-cycling it (even remotely,
if it's on a switchable outlet) is enough; only a change to
`OTA_MANIFEST_URL` itself would need a real USB reflash, since OTA
obviously can't deliver a change to where OTA looks.

## Layout

- `manifest.txt` (gitignored, regenerated per publish) —
  `<version>\n<firmware .bin URL>\n`. `ota.c`'s `sscanf` just splits
  on whitespace, so the newline is cosmetic — a space works too.
- `watermeter.bin` (gitignored, regenerated per publish) — the actual
  firmware image.
- `manifest.txt.example` (tracked) — template/reference for the format
  above.

## Publishing an update

```sh
cd esp32
source ~/.espressif/v6.0/esp-idf/export.sh
idf.py build
```

The build log's `-- App "watermeter" version: <ver>` line (from `git
describe`, effectively the commit hash unless this repo has tags) is
the version string to put in `manifest.txt` — it must differ from
whatever the target device is currently running, or
`ota_check_and_update()` will correctly see them as equal and skip the
update. Then, from the repo root:

```sh
cp esp32/build/watermeter.bin ota/watermeter.bin
printf "%s\nhttp://wolfrax.local:8000/watermeter.bin\n" "<ver>" > ota/manifest.txt
```

## Known gotcha: wolfrax.local mDNS resolution

The serving machine (`wolfrax`) also runs Docker, and its `docker0`
bridge interface (172.17.0.1) gets picked up by avahi alongside the
real LAN address — `avahi-resolve -n wolfrax.local` run **on that
machine** may return the docker-internal address instead of the LAN
one. That command isn't representative of what an ESP32 elsewhere on
the LAN actually gets via multicast mDNS, and in practice
(2026-09-10) the ESP32 correctly reached the real LAN address — but if
a device's OTA check ever can't resolve/reach `wolfrax.local` despite
the server clearly running, this dual-interface mDNS advertisement is
the first thing to suspect. Fix, if ever needed: add
`deny-interfaces=docker0` under `[server]` in
`/etc/avahi/avahi-daemon.conf` on `wolfrax` and restart
`avahi-daemon`.
