#!/usr/bin/env bash
# Create or update a user in the Mosquitto password file.
#
# Usage:
#   ./scripts/mosquitto-passwd.sh <username>
#
# You'll be prompted for the password. The hash is written to
# mosquitto/config/passwd (created on first run). Run this for:
#   - "telegraf"      → the ingest user (must match MQTT_USERNAME in .env)
#   - "boat-<mac>"    → one per device (must match the username in the ACL)
#
# Requires the stack to have been started at least once (so the container
# exists). Restart the broker afterwards:  docker compose restart mosquitto
set -euo pipefail

USER="${1:-}"
if [[ -z "$USER" ]]; then
  echo "usage: $0 <username>" >&2
  exit 1
fi

PASSWD_FILE="/mosquitto/config/passwd"
cd "$(dirname "$0")/.."

# -c creates the file (first user); omit it so later users are appended.
if docker compose exec mosquitto test -f "$PASSWD_FILE" 2>/dev/null; then
  docker compose exec mosquitto mosquitto_passwd "$PASSWD_FILE" "$USER"
else
  docker compose exec mosquitto mosquitto_passwd -c "$PASSWD_FILE" "$USER"
fi

echo "✓ User '$USER' written to mosquitto/config/passwd"
echo "  Apply it with:  docker compose restart mosquitto"
