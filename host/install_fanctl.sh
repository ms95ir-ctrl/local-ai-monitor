#!/bin/bash
# Installs the fanctl fan controller as a root systemd service.
# What it does (nothing else):
#   1. copies host/fanctl.py -> /usr/local/bin/fanctl
#   2. makes the nct6775 motherboard-sensor driver load at boot (/etc/modules-load.d/nct6775.conf)
#   3. installs + starts /etc/systemd/system/fanctl.service
# Undo:  sudo systemctl disable --now fanctl; sudo rm /etc/systemd/system/fanctl.service \
#             /usr/local/bin/fanctl /etc/modules-load.d/nct6775.conf
# Stopping the service hands the fans back to firmware (automatic) control.
set -euo pipefail
cd "$(dirname "$0")"
sudo install -m 755 fanctl.py /usr/local/bin/fanctl
echo nct6775 | sudo tee /etc/modules-load.d/nct6775.conf >/dev/null
sudo tee /etc/systemd/system/fanctl.service >/dev/null <<'UNIT'
[Unit]
Description=Temperature-curve fan controller (CPU fan, RX 580) with P100-aware safety limits
After=multi-user.target

[Service]
Type=simple
ExecStartPre=-/sbin/modprobe nct6775
ExecStart=/usr/bin/python3 /usr/local/bin/fanctl daemon
ExecStopPost=/usr/bin/python3 /usr/local/bin/fanctl --restore
Restart=always
RestartSec=2
StateDirectory=fanctl
RuntimeDirectory=fanctl

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable fanctl
sudo systemctl restart fanctl   # restart so an updated fanctl.py takes effect
sleep 3
systemctl is-active fanctl
fanctl status
