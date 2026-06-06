#!/usr/bin/env bash
# Dynamic DNS: keep a Cloudflare A record pointed at this site's current public
# IP. Run it on a schedule (cron) so the broker domain always resolves home.
#
# IMPORTANT: the record must be DNS-only (grey cloud), NOT proxied. Cloudflare's
# proxy only handles HTTP(S) and would break MQTT/TLS on 8883.
#
# Setup:
#   - CF_API_TOKEN : Cloudflare token with Zone:DNS:Edit
#   - CF_ZONE      : the zone (e.g. example.com)
#   - CF_RECORD    : the full record name (e.g. mqtt.example.com)
# Set them in the environment or edit the defaults below.
#
# Cron (every 5 min):
#   */5 * * * * /path/to/server-stack/ddns/cloudflare-ddns.sh >> /var/log/ddns.log 2>&1
# Variables are read from server-stack/.env automatically.
set -euo pipefail

# Source .env from the server-stack root (one level up from ddns/).
STACK_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
set -a; [[ -f "$STACK_DIR/.env" ]] && source "$STACK_DIR/.env"; set +a

CF_API_TOKEN="${CF_API_TOKEN:?set CF_API_TOKEN in server-stack/.env}"
CF_ZONE="${CF_ZONE:?set CF_ZONE in server-stack/.env}"
CF_RECORD="${CF_RECORD:?set CF_RECORD in server-stack/.env}"

api() { curl -sf -H "Authorization: Bearer $CF_API_TOKEN" -H "Content-Type: application/json" "$@"; }

IP="$(curl -sf https://api.ipify.org)"
[[ -n "$IP" ]] || { echo "could not determine public IP"; exit 1; }

ZONE_ID="$(api "https://api.cloudflare.com/client/v4/zones?name=$CF_ZONE" | grep -o '"id":"[^"]*"' | head -1 | cut -d'"' -f4)"
REC_JSON="$(api "https://api.cloudflare.com/client/v4/zones/$ZONE_ID/dns_records?type=A&name=$CF_RECORD")"
REC_ID="$(echo "$REC_JSON" | grep -o '"id":"[^"]*"' | head -1 | cut -d'"' -f4)"
CUR_IP="$(echo "$REC_JSON" | grep -o '"content":"[^"]*"' | head -1 | cut -d'"' -f4)"

if [[ "$IP" == "$CUR_IP" ]]; then
  echo "$(date -Is) $CF_RECORD already $IP — no change"
  exit 0
fi

# proxied:false is essential — keep the record DNS-only.
api -X PUT "https://api.cloudflare.com/client/v4/zones/$ZONE_ID/dns_records/$REC_ID" \
  --data "{\"type\":\"A\",\"name\":\"$CF_RECORD\",\"content\":\"$IP\",\"ttl\":120,\"proxied\":false}" \
  >/dev/null
echo "$(date -Is) updated $CF_RECORD -> $IP"
