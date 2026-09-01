#!/usr/bin/env bash
# Deploy DevOps dashboard to the lukelang.org VPS.
#   apps/devops/scripts/build.sh   # local or CI
#   apps/devops/deploy/install.sh  # on VPS via ssh
set -euo pipefail

HOST="${HOST:-root@187.127.108.32}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

"$ROOT/apps/devops/scripts/build.sh"

echo "→ uploading API + scripts"
ssh "$HOST" 'mkdir -p /var/luke/devops'
scp -q "$ROOT/apps/devops/backend/devops_api" "$HOST:/var/luke/devops/"
scp -q "$ROOT/apps/devops/scripts/collect_metrics.sh" "$HOST:/var/luke/devops/"
scp -q "$ROOT/apps/devops/deploy/luke-devops.service" "$HOST:/etc/systemd/system/"
ssh "$HOST" 'chmod +x /var/luke/devops/devops_api /var/luke/devops/collect_metrics.sh'

echo "→ postgres (idempotent)"
scp -q "$ROOT/apps/devops/scripts/setup_postgres.sh" "$ROOT/apps/devops/backend/schema.sql" "$HOST:/tmp/"
ssh "$HOST" 'bash /tmp/setup_postgres.sh'

echo "→ systemd"
ssh "$HOST" 'systemctl daemon-reload && systemctl enable --now luke-devops.service && systemctl restart luke-devops.service'

echo "→ site bundle (includes site/ops from vite build)"
"$ROOT/scripts/deploy_site.sh"

echo "→ nginx /ops routes"
scp -q "$ROOT/apps/devops/deploy/nginx-ops.snippet" "$HOST:/tmp/nginx-ops.snippet"
ssh "$HOST" 'bash -s' <<'REMOTE'
set -euo pipefail
CONF=/etc/nginx/sites-available/lukelang.conf
MARK="# luke devops dashboard"
if ! grep -q "$MARK" "$CONF"; then
  sed -i "/location = \/README.md/i\\
    $MARK\\
    location /ops/api/ {\\
        proxy_pass http://127.0.0.1:8820/api/;\\
        proxy_http_version 1.1;\\
        proxy_set_header Host \$host;\\
        proxy_set_header X-Real-IP \$remote_addr;\\
    }\\
    location /ops/ {\\
        try_files \$uri \$uri/ /ops/index.html;\\
    }\\
" "$CONF"
fi
nginx -t && systemctl reload nginx
REMOTE

echo "→ seed collector cron"
ssh "$HOST" '(crontab -l 2>/dev/null | grep -v collect_metrics.sh; echo "* * * * * /var/luke/devops/collect_metrics.sh >> /var/log/luke-devops-collect.log 2>&1") | crontab -'
ssh "$HOST" '/var/luke/devops/collect_metrics.sh || true'

echo "done — https://lukelang.org/ops/"
