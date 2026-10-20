# Local AI Monitor

A small touchscreen dashboard for a home AI server. An ESP32-S3 touchscreen shows live
GPU and LLM stats from an NVIDIA Tesla P100 running `llama-server`, and lets you change fan profiles by
touch. A tiny Python feeder on the server sends the data over USB serial.

It runs 24/7 on my AI box (Tesla P100 16 GB, llama.cpp). It is written for that machine, so expect to
adapt the details (see [Adapting it](#adapting-it-to-your-machine)).

## Quick install (host machine)

On the machine that runs your models (Linux with systemd):

```bash
curl -fsSL https://raw.githubusercontent.com/ms95ir-ctrl/local-ai-monitor/main/install.sh | sh
```

It detects the GPU, a local llama-server, a board on USB and the LAN, shows what it will do, asks, then installs the
feed as a service. Add options after `sh -s --`, e.g. `| sh -s -- --dry-run` (detect only), `--yes` (no questions),
`--llama-url http://127.0.0.1:8081`, `--with-fans` (optional fan controller), `--uninstall`. The script is
[`install.sh`](install.sh) and the logic is in [`host/setup.py`](host/setup.py); read them before piping to a shell.
Then flash a board ([`es3c28p-wifi-320x240`](firmware/es3c28p-wifi-320x240)) and pick your WiFi on its screen.

## Two hardware variants

The server side is the same for both. Only the firmware differs.

| Variant | Board | Display | Status |
|---|---|---|---|
| [`esp32-s3-st7796-480x320`](firmware/esp32-s3-st7796-480x320) | ESP32-S3 DevKitC-1 with a separate display | 480x320 ST7796, FT6336 touch, UART bridge USB | In daily use. |
| [`es3c28p-320x240`](firmware/es3c28p-320x240) | LCDWIKI ES3C28P, all-in-one 2.8" board | 320x240 ILI9341V, FT6336 touch, native USB | Compiles. **Not yet tested on a real board.** |
| [`es3c28p-wifi-320x240`](firmware/es3c28p-wifi-320x240) | Same board, battery-friendly | Same screen, plus a WiFi tab | Pick the network on the touchscreen; the board finds the host by itself. USB still works. |

The second variant is a re-layout of the same three pages for the smaller screen. A two-part
3D-printable case for it, with the battery under the board, is in [`case/`](case).

```
 AI server (Linux)                                        ESP32-S3 + 480x320 TFT
┌──────────────────────────────┐   USB serial, 115200    ┌──────────────────────┐
│ nvidia-smi ─┐                │   P100,... LLM,... FAN  │  GPU | LLM | FANS    │
│ llama-server ┼─ p100_feed.py ├────────────────────────▶│  gauges, history     │
│ fanctl ──────┘ (systemd)     │◀────────────────────────┤  graphs, touch UI    │
└──────────────────────────────┘   SET,PROFILE / SET,FAN └──────────────────────┘
```

## What you get

Three pages, switched by tapping the tab bar or swiping:

- **GPU**: utilisation, temperature, VRAM and power gauges, SM and memory clocks, and a scrolling
  history graph of utilisation and temperature (2 samples per second: 3.7 minutes on the 480x320 variant, 2.4 on the 320x240 one). A
  LIVE / NO DATA badge shows whether the feed is alive.
- **LLM**: server state (offline, idle, reading prompt, generating), context fill, live and average
  generation and prompt speed in tokens per second, total tokens generated and processed, and server
  uptime. It reads `llama-server`'s `/slots` and `/metrics` and never sends prompts.
- **FANS**: live RPM and duty per fan, and touch buttons for the fan profiles (idle, normal,
  performance, auto) or a manual duty per fan.

All drawing goes into a full-frame off-screen buffer in PSRAM and is pushed once per frame, so there
is no flicker.

## Repository layout

| Path | What it is |
|---|---|
| `firmware/esp32-s3-st7796-480x320/` | Firmware for the DevKitC + 480x320 display (PlatformIO, Arduino, TFT_eSPI). |
| `firmware/es3c28p-320x240/` | Firmware for the LCDWIKI ES3C28P all-in-one board. |
| `firmware/es3c28p-wifi-320x240/` | The ES3C28P firmware with an on-screen WiFi menu (data over WiFi instead of the USB cable). |
| `firmware/tools/make_fonts.py` | Generates the smooth fonts header both variants use. |
| `host/p100_feed.py` | Feeder service: GPU stats, llama-server stats and fan status to the ESP, and its fan commands back. |
| `host/fanctl.py` | Fan controller daemon with temperature curves and GPU-aware safety limits. |
| `host/install_*.sh` | Installers that set up the two services under systemd. |
| `systemd/` | The unit files, for reading or manual install. |
| `case/` | Two-part 3D-printable case for the ES3C28P variant (OpenSCAD, STL, checks). Designed, not yet printed. |

## Hardware

Both variants need an ESP32-S3 with 16 MB flash and PSRAM, a display with an FT6336 capacitive touch
controller, and a USB cable to the server (shows up as `/dev/ttyACM0`).

**Variant `esp32-s3-st7796-480x320`:** an `esp32-s3-devkitc-1` and a 480x320 ST7796 SPI display. Pins
(set in `firmware/esp32-s3-st7796-480x320/platformio.ini` and `src/main.cpp`):

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| TFT MOSI | 11 | | Touch SDA | 8 |
| TFT SCLK | 12 | | Touch SCL | 9 |
| TFT MISO | 13 | | Touch RST | 7 |
| TFT CS | 10 | | Backlight | 5 |
| TFT DC | 14 | | TFT RST | 4 |

**Variant `es3c28p-320x240`:** the [LCDWIKI ES3C28P](https://www.lcdwiki.com) board has everything on
one PCB, so there is no wiring. Its pins come from the vendor specification and are listed in
`firmware/es3c28p-320x240/platformio.ini` and `src/main.cpp`. Its USB-C port is the ESP32-S3's own
USB, so there is no UART chip and the port may re-enumerate when the board resets.

## Setup

**1. Flash the firmware** (needs [PlatformIO](https://platformio.org/); your user must be in the
`dialout` group to use the serial port). Pick the folder for your hardware:

```bash
cd firmware/esp32-s3-st7796-480x320     # or: cd firmware/es3c28p-320x240
pio run -t upload
```

On the ES3C28P the first upload may need download mode: hold BOOT, press RESET, release BOOT.

**2. Install the feeder on the server** (needs `python3`, `pyserial` and `nvidia-smi`):

```bash
pip install pyserial
sudo ./host/install_feed.sh
```

This writes `/etc/systemd/system/esp-feed.service`, enables it at boot and starts it. The port and the
llama-server URL are the two arguments of `p100_feed.py`:

```bash
python3 host/p100_feed.py /dev/ttyACM0 http://127.0.0.1:8081
```

**3. Optional: the fan controller.** It needs root, an `nct6775` motherboard sensor chip, and
changes real fan speeds. Read `host/fanctl.py` first.

```bash
./host/install_fanctl.sh
fanctl status
```

Without it the FANS page shows "Fan controller not running" and the rest keeps working.

## Serial protocol

Plain text lines, one per message.

Host to ESP (`P100` four times a second, `LLM` twice, `FAN` once):

```
P100,<temp C>,<util %>,<mem used MiB>,<mem total MiB>,<power W>,<power limit W>,<sm MHz>,<mem MHz>
LLM,<state>,<ctx used>,<ctx total>,<gen tok/s>,<prompt tok/s>,<predicted total>,<prompt total>,<avg gen>,<avg prompt>,<uptime s>,<quant>,<model>
FAN,...                      (profile and per-fan rpm, duty and temperature)
```

ESP to host, on touch:

```
SET,PROFILE,<idle|normal|performance|auto>
SET,FAN,<fan index>,<duty %>
```

LLM state: 0 offline, 1 idle, 2 prompt processing, 3 generating.

## fanctl safety limits

Whatever the profile or manual setting, these always apply:

- GPU at 70 C or more: every fan at 80% or more. GPU at 78 C or more: every fan 100%.
- CPU at 92 C or more: CPU fan 100%. RX 580 at 90 C or more: its fan 100%.
- A temperature source that cannot be read: that fan goes to 100%.
- On any exit the fans are handed back to the motherboard's automatic mode.

## Adapting it to your machine

- **GPU:** the feeder calls `nvidia-smi -i 0`. Any NVIDIA card works; the "Tesla P100 16 GB" title is
  drawn in `pageGpu()` in `firmware/src/main.cpp`.
- **LLM server:** anything exposing llama.cpp's `/slots` and `/metrics` endpoints.
- **Fans:** `fanctl.py` is written for my board (Nuvoton `nct6775` PWM headers, one RX 580, and a
  40 mm fan duct on the GPU). The fan names, headers and curves near the top of the file are specific
  to it. The socket group is set to `local1` in `fanctl.py`; change `SOCK_GROUP` to your user.
- **Display:** other displays need new pins and a new driver in `platformio.ini`, and probably a new
  layout. The two variants are good starting points.
- **Fonts:** each variant's `include/fonts.h` is generated by `firmware/tools/make_fonts.py`
  (the ES3C28P one with `--val 22 --big 38`).

## Known limits

- The ES3C28P variant has only been compiled. Display colours, rotation and touch direction are the
  things most likely to need a one-line change; the knobs are listed in its README.
- If the ESP is unplugged the feeder keeps running and retries the serial port every second, so
  `systemctl status esp-feed` still says "active" while nothing is connected. Check `ls /dev/ttyACM*`.
- There are no photos or screenshots yet.

## Licence

MIT, see `LICENSE`.
