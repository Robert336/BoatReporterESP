#!/usr/bin/env bash
# Loop wrapper so the one-shot DDNS script runs on an interval inside the
# container — this replaces the host cron/systemd-timer scheduling.
#
# Deliberately NOT `set -e`: a single failed update (transient API/network
# blip, or the script's own :? guard on a missing var) must not kill the loop.
set -uo pipefail

: "${DDNS_INTERVAL:=300}"
echo "$(date -Is) cloudflare-ddns: running every ${DDNS_INTERVAL}s"

while true; do
  /app/cloudflare-ddns.sh || echo "$(date -Is) ddns update failed — retrying in ${DDNS_INTERVAL}s"
  sleep "${DDNS_INTERVAL}"
done
