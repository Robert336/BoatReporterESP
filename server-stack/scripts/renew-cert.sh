#!/usr/bin/env bash
# Weekly certificate renewal check.
#
# Usage (manual):
#   ./scripts/renew-cert.sh
#
# Designed to be run from cron weekly. It only touches the broker when the
# certificate is actually renewed, so Mosquitto is NOT restarted on every run.
#
# Returns:
#   0  – certificate is valid (no action or renewal successful)
#   1  – renewal failed or config missing
set -euo pipefail
cd "$(dirname "$0")/.."

# shellcheck disable=SC1091
set -a; [[ -f .env ]] && source .env; set +a
DOMAIN="${MQTT_DOMAIN:?set MQTT_DOMAIN in .env}"

CERT_SRC="letsencrypt/live/${DOMAIN}/fullchain.pem"
CERT_DST="certs/fullchain.pem"
RENEWAL_DAYS=30
RENEWAL_SECONDS=$((RENEWAL_DAYS * 86400))

# ── Helper: copy cert + restart broker only when the file changed ───────────
restart_if_changed() {
  local changed=0

  if [[ ! -f "$CERT_SRC" ]]; then
    echo "ERROR: $CERT_SRC not found after renewal" >&2
    return 1
  fi

  # Compare SHA-256 of the current live cert vs. the one Mosquitto is using.
  # This is robust against certbot's idempotent "already renewed" runs.
  local src_hash dst_hash
  src_hash="$(openssl dgst -sha256 -binary "$CERT_SRC" | xxd -p -c 64)"
  if [[ -f "$CERT_DST" ]]; then
    dst_hash="$(openssl dgst -sha256 -binary "$CERT_DST" | xxd -p -c 64)"
  else
    dst_hash=""
  fi

  if [[ "$src_hash" != "$dst_hash" ]]; then
    echo "Certificate changed, copying to certs/ …"
    cp -L "$CERT_SRC" "$CERT_DST"
    cp -L "letsencrypt/live/${DOMAIN}/privkey.pem" certs/privkey.pem
    changed=1
  else
    echo "Certificate unchanged, no copy needed."
  fi

  # Also compare the *deployed* file (what mosquitto currently serves) vs.
  # the certs/ copy. If they differ, restart anyway (e.g. manual edit).
  local deployed_hash
  if [[ -f "$CERT_DST" ]]; then
    deployed_hash="$(openssl dgst -sha256 -binary "$CERT_DST" | xxd -p -c 64)"
  else
    deployed_hash=""
  fi

  if [[ "$src_hash" != "$deployed_hash" ]] || [[ $changed -eq 1 ]]; then
    echo "Restarting mosquitto to load new certificate …"
    docker compose restart mosquitto
  fi
}

# ── 1. Initial issue (no cert yet) ────────────────────────────────────────
if [[ ! -f "$CERT_SRC" ]]; then
  echo "No certificate found for ${DOMAIN}; running initial issuance …"
  ./scripts/issue-cert.sh
  restart_if_changed
  exit 0
fi

# ── 2. Check expiry ─────────────────────────────────────────────────────────
# openssl -checkend returns non-zero when the cert expires before the given
# number of seconds.
if openssl x509 -in "$CERT_SRC" -noout -checkend "$RENEWAL_SECONDS" >/dev/null 2>&1; then
  echo "Certificate for ${DOMAIN} valid for >${RENEWAL_DAYS} days — no renewal needed."
  restart_if_changed  # ensure certs/ is in sync even if valid
  exit 0
fi

echo "Certificate for ${DOMAIN} expires within ${RENEWAL_DAYS} days; renewing …"

# ── 3. Renew (DNS-01 via Cloudflare) ────────────────────────────────────────
CF_INI="$(pwd)/secrets/cloudflare.ini"
if [[ ! -f "$CF_INI" ]]; then
  echo "Missing $CF_INI — cannot renew." >&2
  exit 1
fi

docker run --rm \
  -v "$(pwd)/letsencrypt:/etc/letsencrypt" \
  -v "$CF_INI:/cloudflare.ini:ro" \
  certbot/dns-cloudflare:latest \
  renew \
    --dns-cloudflare \
    --dns-cloudflare-credentials /cloudflare.ini \
    --dns-cloudflare-propagation-seconds 30 \
    --non-interactive \
    --deploy-hook "sh -c 'echo certbot-renewal-success > /etc/letsencrypt/renewal-success-flag'"

# Certbot 'renew' exits 0 whether it renewed or not. Detect actual work via the
# flag file (touched by --deploy-hook only on successful renewal) or by hash.
# We simply run restart_if_changed unconditionally; the hash check prevents
# unnecessary restarts.
restart_if_changed

# Fix ownership after any certbot run so the host user can manage files.
if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
  sudo chown -R "$(id -u):$(id -g)" letsencrypt/ 2>/dev/null || true
fi

echo "✓ Renewal check complete."
