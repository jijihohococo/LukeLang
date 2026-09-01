#!/usr/bin/env bash
# Collect host metrics + HTTP service checks → Postgres (luke_devops).
# Install: */1 * * * * /var/luke/devops/collect_metrics.sh >> /var/log/luke-devops-collect.log 2>&1
set -euo pipefail

DB_NAME="${LUKE_DEVOPS_DB:-luke_devops}"
DB_USER="${LUKE_DEVOPS_USER:-luke_ops}"
export PGPASSWORD="${LUKE_DEVOPS_PASS:-luke_ops}"

# Host metrics
read -r load1 _ _ < /proc/loadavg
mem_pct=$(free | awk '/Mem:/ {printf "%.1f", $3/$2 * 100}')
cpu_pct=$(top -bn1 | awk -F'[, ]+' '/^%Cpu/ {idle=$8; if(idle=="") idle=$9; printf "%.1f", 100-idle}')
disk_pct=$(df -P / | awk 'NR==2 {gsub(/%/,"",$5); print $5}')

psql -h 127.0.0.1 -U "$DB_USER" -d "$DB_NAME" -v ON_ERROR_STOP=1 -c \
  "INSERT INTO host_metrics (cpu_pct, mem_pct, disk_pct, load1) VALUES (${cpu_pct:-0}, ${mem_pct:-0}, ${disk_pct:-0}, ${load1:-0});"

check_service() {
  local slug="$1" url="$2"
  local start end ms code status
  start=$(date +%s%3N)
  code=$(curl -fsS -o /dev/null -w '%{http_code}' --max-time 8 "$url" 2>/dev/null || echo "000")
  end=$(date +%s%3N)
  ms=$((end - start))
  status="down"
  if [[ "$code" =~ ^2 ]]; then status="up"
  elif [[ "$code" =~ ^3 ]]; then status="degraded"
  fi
  psql -h 127.0.0.1 -U "$DB_USER" -d "$DB_NAME" -v ON_ERROR_STOP=1 -c \
    "INSERT INTO service_checks (service_id, status, latency_ms)
     SELECT id, '${status}', ${ms} FROM services WHERE slug='${slug}';"
}

while IFS='|' read -r slug url; do
  [[ -z "$slug" ]] && continue
  check_service "$slug" "$url"
done < <(psql -h 127.0.0.1 -U "$DB_USER" -d "$DB_NAME" -At -c \
  "SELECT slug || '|' || check_url FROM services WHERE check_url <> '';")

# Prune old rows (keep ~7 days)
psql -h 127.0.0.1 -U "$DB_USER" -d "$DB_NAME" -c \
  "DELETE FROM host_metrics WHERE recorded_at < NOW() - INTERVAL '7 days';
   DELETE FROM service_checks WHERE checked_at < NOW() - INTERVAL '7 days';" >/dev/null
