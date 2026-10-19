# ES3C28P variant (320x240)

Firmware for the LCDWIKI **ES3C28P**: a 2.8" all-in-one ESP32-S3 board with an ILI9341V 240x320 SPI
display and an FT6336 touch controller. It shows the same three pages as the 480x320 variant (GPU, LLM,
FANS) laid out for 320x240, and speaks the same serial protocol, so the host side is unchanged.

**Status: compiles, not yet tested on a real board.** Pin numbers come from the vendor specification
(section 4.2). Three settings depend on the exact panel and how you mount it, and are the first things
to adjust if the screen looks wrong:

| Symptom | Change (in `platformio.ini` build_flags) |
|---|---|
| Colours look inverted (light background) | Remove `-DTFT_INVERSION_ON=1` |
| Red and blue swapped | Add `-DTFT_RGB_ORDER=TFT_RGB` |
| Screen upside down | Set `-DTFT_ROTATION=3` |
| Taps land mirrored or rotated | Add `-DTOUCH_MAP=0` (or 1, 2, 3); the four mappings are described above `readTouch()` in `src/main.cpp` |

Differences from the 480x320 variant:

- **USB:** `Serial` is the ESP32-S3's native USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`), because the USB-C
  port connects straight to the chip.
- **LCD reset** is shared with the chip reset, so `TFT_RST` is `-1`.
- **Layout:** gauges are labelled UTIL, TEMP, VRAM and POWER, totals use short labels, large token
  counts switch to `123.5M`, and long model names are shortened to fit beside the state badge.
- **Graph:** 292 samples, about 2.4 minutes.
- **Fonts:** regenerated with a 22 px value font and a 38 px big-number font.

Build and flash:

```bash
pio run -t upload
```
