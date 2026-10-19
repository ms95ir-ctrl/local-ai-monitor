#!/bin/bash
# Installs the ESP32 dashboard feed (p100_feed.py) as a systemd service that starts at boot.
# Undo: sudo systemctl disable --now esp-feed; sudo rm /etc/systemd/system/esp-feed.service
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
sudo tee /etc/systemd/system/esp-feed.service >/dev/null <<UNIT
[Unit]
Description=ESP32 control panel feed (P100 + LLM stats over USB serial)
After=multi-user.target local-llm.service fanctl.service

[Service]
Type=simple
User=$(id -un)
ExecStart=/usr/bin/python3 -I $DIR/p100_feed.py /dev/ttyACM0 http://127.0.0.1:8081
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable esp-feed
sudo systemctl restart esp-feed
sleep 3
systemctl is-active esp-feed
