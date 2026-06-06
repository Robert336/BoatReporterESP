#!/usr/bin/env bash
# Obtain a Let's Encrypt certificate for the broker via the DNS-01 challenge,
# using Cloudflare DNS. DNS-01 is used (not HTTP-01) because we only open port
# 8883 — there is no inbound 80/443 for an HTTP challenge.
#
# One-time prep:
#   1. Your domain's DNS is managed by Cloudflare.
#   2. Create a Cloudflare API token (Zone:DNS:Edit for the zone) and put it in
#      server-stack/secrets/cloudflare.ini  (chmod 600):
#         dns_cloudflare_api_token = <token>
#   3. Set MQTT_DOMAIN in .env to the hostname (e.g. mqtt.example.com).
#
# Usage:
#   ./scripts/issue-cert.sh
#
# Re-run to renew (or automate — see the renewal note in ../README.md). After a
# renewal, copy the new files into certs/ and restart the broker.
set -euo pipefail
cd "$(dirname "$0")/.."

# shellcheck disable=SC1091
set -a; [[ -f .env ]] && source .env; set +a
DOMAIN="${MQTT_DOMAIN:?set MQTT_DOMAIN in .env}"

# If a cloudflare.ini doesn't exist yet, generate one from .env so the user
# only has to set CF_API_TOKEN in one place.
CF_INI="$(pwd)/secrets/cloudflare.ini"
if [[ ! -f "$CF_INI" ]]; then
  if [[ -n "${CF_API_TOKEN:-}" ]]; then
    mkdir -p "$(pwd)/secrets"
    printf 'dns_cloudflare_api_token = %s\n' "$CF_API_TOKEN" > "$CF_INI"
    chmod 600 "$CF_INI"
    echo "Created $CF_INI from CF_API_TOKEN in .env"
  else
    echo "Missing $CF_INI — add CF_API_TOKEN to .env or create secrets/cloudflare.ini manually." >&2
    exit 1
  fi
fi

# Run certbot in a container; persist its state in ./letsencrypt.
docker run --rm \
  -v "$(pwd)/letsencrypt:/etc/letsencrypt" \
  -v "$CF_INI:/cloudflare.ini:ro" \
  certbot/dns-cloudflare:latest \
  certonly \
    --dns-cloudflare \
    --dns-cloudflare-credentials /cloudflare.ini \
    --dns-cloudflare-propagation-seconds 30 \
    -d "$DOMAIN" \
    --non-interactive --agree-tos --register-unsafely-without-email

# Mosquitto reads certs/fullchain.pem + certs/privkey.pem. Copy (not symlink)
# so the read-only bind mount into the container resolves real files.
mkdir -p certs
cp -L "letsencrypt/live/$DOMAIN/fullchain.pem" certs/fullchain.pem
cp -L "letsencrypt/live/$DOMAIN/privkey.pem"   certs/privkey.pem
echo "✓ Cert for $DOMAIN copied to certs/. Restart broker: docker compose restart mosquitto"
