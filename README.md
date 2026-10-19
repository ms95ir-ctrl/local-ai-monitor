# Local AI Monitor

A small touchscreen dashboard for a home AI server. An ESP32-S3 with a 480x320 colour touchscreen shows live
GPU and LLM stats from an NVIDIA Tesla P100 running `llama-server`, and lets you change fan profiles by
touch. A tiny Python feeder on the server sends the data over USB serial.

It runs 24/7 on my AI box (Tesla P100 16 GB, llama.cpp). It is written for that machine, so expect to
adapt the details (see [Adapting it](#adapting-it-to-your-machine)).

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
  history graph of utilisation and temperature (about 3.7 minutes at 2 samples per second). A
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
| `firmware/` | ESP32-S3 firmware (PlatformIO, Arduino, TFT_eSPI): UI, touch, serial protocol. |
| `host/p100_feed.py` | Feeder service: GPU stats, llama-server stats and fan status to the ESP, and its fan commands back. |
| `host/fanctl.py` | Fan controller daemon with temperature curves and GPU-aware safety limits. |
| `host/install_*.sh` | Installers that set up the two services under systemd. |
| `systemd/` | The unit files, for reading or manual install. |
| `case/` | OpenSCAD 3D-printable parts (work in progress, see `case/README.md`). |

## Hardware

- ESP32-S3 board with 16 MB flash and PSRAM (`esp32-s3-devkitc-1` in PlatformIO).
- 480x320 ST7796 SPI display with an FT6336 capacitive touch controller.
- A USB cable from the board to the server (shows up as `/dev/ttyACM0`).

Pins (set in `firmware/platformio.ini` and `firmware/src/main.cpp`):

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| TFT MOSI | 11 | | Touch SDA | 8 |
| TFT SCLK | 12 | | Touch SCL | 9 |
| TFT MISO | 13 | | Touch RST | 7 |
| TFT CS | 10 | | Backlight | 5 |
| TFT DC | 14 | | TFT RST | 4 |

## Setup

**1. Flash the firmware** (needs [PlatformIO](https://platformio.org/); your user must be in the
`dialout` group to use the serial port):

```bash
cd firmware
pio run -t upload
```

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
- **Display:** other ST7796 setups need new pins in `platformio.ini`.
- **Fonts:** `firmware/include/fonts.h` is generated by `firmware/tools/make_fonts.py`.

## Known limits

- If the ESP is unplugged the feeder keeps running and retries the serial port every second, so
  `systemctl status esp-feed` still says "active" while nothing is connected. Check `ls /dev/ttyACM*`.
- There are no photos or screenshots yet.

## Licence

MIT, see `LICENSE`.
