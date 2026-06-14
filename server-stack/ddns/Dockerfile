# Tiny image that runs the Cloudflare DDNS script on an interval, so the broker's
# A record tracks the host's dynamic public IP. Part of the compose stack — the
# single source of truth — instead of a host-level cron/systemd timer.
FROM alpine:3.20

RUN apk add --no-cache bash curl ca-certificates

COPY cloudflare-ddns.sh /app/cloudflare-ddns.sh
COPY docker-entrypoint.sh /app/docker-entrypoint.sh
RUN chmod +x /app/cloudflare-ddns.sh /app/docker-entrypoint.sh

# Config (CF_API_TOKEN / CF_ZONE / CF_RECORD / DDNS_INTERVAL) is passed via the
# environment from compose (.env). The script's own .env sourcing is a no-op here
# since /.env doesn't exist in the container.
ENTRYPOINT ["/app/docker-entrypoint.sh"]
