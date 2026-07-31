# this fork removes the need for the nrf using just the esp32 for this. 


# pinguino-ha-remote

![De'Longhi Pinguino, now connected: web UI, Home Assistant, MQTT/HTTP, ambient sensor, via an emulated BLE remote on an ESP32-S3 + nRF52840 bridge](docs/assets/photos/feature-overview.png)

Control a **De'Longhi Pinguino air-conditioner** from your LAN / Home Assistant by
**emulating its manual BLE remote**. Commands from the web UI, MQTT, or Home Assistant bond
with the AC and **change its state on-air** — no cloud, no IR blaster.

**Status:** working end-to-end on the two-board build (`power`, `up`, `down`, `mode`, `eco`,
`timer`, `fan`, `silent`, `flap` all act on the AC). **`main` is now single-board** — the BLE
emulator moved onto the ESP32-S3's own radio and that path is **not yet confirmed on the AC**.
For a known-good unit today, use [release v0.3.0](https://github.com/bdherouville/pinguino-ha-remote/releases/tag/v0.3.0).

## What you get

One small board, plus an optional sensor:

```
HA / LAN ──MQTT/HTTP──► ESP32-S3 ──BLE──► AC
```

- **ESP32-S3** — everything: Wi-Fi, web UI, MQTT/Home Assistant, *and* the BLE remote
  emulator that bonds with the AC.
- **BME280** — ambient temperature / humidity / pressure (optional), reported to HA.

The nRF52840 used to be here because the AC was thought to reject an ESP32 emulator. That was
an artefact of the old ESP32 build advertising the wrong discoverable flag, not a radio
limitation — the ESP32 still can't *sniff* the remote, but it doesn't need to in order to
*be* one. Full reasoning and the protocol: [`docs/ganymede_protocol.md`](docs/ganymede_protocol.md).

## Bill of materials

| Qty | Part | Role | Source |
|-----|------|------|--------|
| 1 | **ESP32-S3 SuperMini** (ESP32-S3FH4R2) | Wi-Fi bridge **+ BLE emulator** | [AliExpress](https://fr.aliexpress.com/item/1005008807808123.html) |
| 1 | **BME280 / BMP280** module | ambient T/H/P (optional) | [AliExpress](https://fr.aliexpress.com/item/1005007527106667.html) |
| — | USB cables + jumper wires | power & wiring | — |

*(Reverse-engineering the protocol yourself needs an nRF52840 as a sniffer — see
[`tools/nrf_sniffer/`](tools/nrf_sniffer/README.md). A working deployment doesn't: the ESP32
can't sniff, but it doesn't need to.)*

| | |
|---|---|
| ![Mounted boards](docs/assets/photos/mounted-boards.jpg) | ![Close-up of the powered stack](docs/assets/photos/mounted-boards-closeup.jpg) |
| nRF52840 emulator + ESP32-S3 bridge + BME280 | Powered: red = nRF, green = ESP32-S3 |

## Get a working unit

Prebuilt binaries are attached to every
[**release**](https://github.com/bdherouville/pinguino-ha-remote/releases/latest). Releases up
to v0.3.0 are the two-board build; the single-board firmware currently has to be built locally.

### 1 · Flash the ESP32-S3

- **Browser:** open the [web flasher](https://bdherouville.github.io/pinguino-ha-remote/flash/)
  (Chrome/Edge), plug in, **Install**. *(Serves the last release — still two-board.)*
- **CLI:** `esptool --chip esp32s3 -p <PORT> write_flash 0x0 ganymede-bridge-esp32s3.bin`
- **Local build** (needed for the single-board firmware):
  `. ~/esp/esp-idf/export.sh && cd firmware/esp32_bridge && idf.py build flash monitor`

Details → [`firmware/esp32_bridge/README.md`](firmware/esp32_bridge/README.md).

### 2 · Wire the sensor (optional)

```
ESP32-S3        BME280
  GPIO1 (SCL) ─► SCL
  GPIO2 (SDA) ◄► SDA
  GND ───────── GND
```
Pinouts and board quirks: [`docs/HARDWARE.md`](docs/HARDWARE.md).

### 3 · Connect the bridge to Wi-Fi

On first boot the ESP32 raises an open AP **`Ganymede-Bridge`** → connect → open
`http://192.168.4.1` → pick your Wi-Fi → (optional) set your **MQTT broker** for Home Assistant.

### 4 · Pair with the AC

The emulator behaves like a real remote: with **no bond** it automatically advertises in
**pairing mode** (Limited Discoverable) as `Ganymede` — no button needed.

1. Put the **AC** in pairing mode (make sure the **AC is plugged in and turned off**, then
   **hold MODE ~10 s**; its display dot blinks rapidly).
2. They bond within ~60 s. The board's LED goes **green** = ready to relay.

On the first boot of the single-board firmware, check the serial log for the line reporting the
HID report handle — it says whether the GATT layout matched the real remote, and if not, exactly
what to change. See [`firmware/esp32_bridge/README.md`](firmware/esp32_bridge/README.md).

That's it — it **pairs, unpairs, re-pairs, and switches with the original remote exactly like a
physical remote**, so the address doesn't matter (the prebuilt generic binary works as-is).

### 5 · Control it

Use the bridge **web UI**, **Home Assistant** (9 buttons + 3 sensors auto-discovered over
MQTT), or publish to `ganymede/cmd/<button>`.

## Documentation

- [**Protocol & reverse engineering**](docs/ganymede_protocol.md) — identity, advertising,
  GATT, button reports, the link-layer pairing gate, SMP, and how it was reverse-engineered.
- [**Hardware & build notes**](docs/HARDWARE.md) — board gotchas (clock, bootloader, flash
  offset), pinouts, why the BLE lives on the nRF.
- [**Releasing**](RELEASING.md) — how CI builds and publishes the binaries.
- Firmware: [ESP32-S3 bridge](firmware/esp32_bridge/README.md) ·
  [nRF52840 emulator](firmware/nrf52_emulator/README.md) ·
  [nRF Sniffer](tools/nrf_sniffer/README.md).

## License

[MIT](LICENSE) for this project's code and docs. Third-party components (Nordic nRF Sniffer,
ESP-IDF managed components, the NimBLE reference) keep their upstream licenses.
