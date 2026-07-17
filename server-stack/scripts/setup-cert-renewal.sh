#!/usr/bin/env bash
# Install a weekly systemd timer that runs scripts/renew-cert.sh.
# This avoids relying on cron (which many distros no longer enable by default)
# and gives us a persistent unit file you can inspect with `systemctl status`.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run with sudo:  sudo $0" >&2
  exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERVICE_FILE="/etc/systemd/system/boat-cert-renewal.service"
TIMER_FILE="/etc/systemd/system/boat-cert-renewal.timer"

cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=BoatReporter MQTT certificate renewal
After=network-online.target docker.service

[Service]
Type=oneshot
User=$(id -u)
WorkingDirectory=$PROJECT_DIR
ExecStart=$PROJECT_DIR/scripts/renew-cert.sh
EOF

cat > "$TIMER_FILE" <<EOF
[Unit]
Description=Run BoatReporter MQTT cert renewal weekly

[Timer]
OnCalendar=Sun 03:00:00
Persistent=true
RandomizedDelaySec=3600

[Install]
WantedBy=timers.target
EOF

systemctl daemon-reload
systemctl enable --now boat-cert-renewal.timer

echo "✓ Weekly timer installed."
echo "  Check:  systemctl status boat-cert-renewal.timer"
echo "  Logs:   journalctl -u boat-cert-renewal.service -f"
