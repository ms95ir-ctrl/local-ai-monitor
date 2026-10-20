# ES3C28P WiFi variant (320x240)

The [`es3c28p-320x240`](../es3c28p-320x240) firmware plus a **WIFI** tab, so the board can run on its battery
without a cable. Same pages (GPU, LLM, FANS), same line protocol.

## Using it

1. Flash it: `pio run -t upload` (see the USB variant's README for the colour/rotation flags, which are the same).
2. Open the **WIFI** tab, tap **Choose network**, tap your network in the list, type the password on the
   on-screen keyboard and tap **Join**.
3. The network is saved on the board (flash, not the firmware) once it has actually connected, so a mistyped
   password is never stored. It reconnects by itself after every reboot. **Forget** clears it.
4. The board then finds the host by itself and the tab shows `Monitor: <hostname> · <ip>`. No IP to type.

USB keeps working at the same time: plug the cable in and the USB feed also updates the screen.

## How it connects

- The host (`host/p100_feed.py`) broadcasts a UDP beacon `ESPDASH,8765,<hostname>` to port 8766 every 2 s.
- The board listens for it, then opens a TCP connection to port 8765 on the sender.
- Over that connection the host sends the same `P100,` / `LLM,` / `FAN,` lines as over USB, and the board sends back
  `SET,...` fan commands.
- If no beacon arrives within 6 s (some routers drop broadcasts), the board probes its own /24 for port 8765 instead.
- If the host goes silent for 8 s the board drops the link and looks for the beacon again.

The board and the host must be on the same local network (same subnet). Guest networks and some phone hotspots
isolate devices from each other and will not work. The host only accepts connections from private (LAN) addresses and
there is no password on the link, so anyone on the same LAN can read the stats and change the fan profile.
Do not forward port 8765.

## Limits

- Open networks join immediately; hidden networks (no name broadcast) are not listed.
- 2.4 GHz networks only (the ESP32-S3 has no 5 GHz).
- WiFi and an always-on backlight cost battery. There is no screen dimming or sleep yet.
