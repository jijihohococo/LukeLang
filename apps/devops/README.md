# LukeLang DevOps Dashboard

Internal operations dashboard for lukelang.org infrastructure.

- **Backend:** LukeLang (`apps/devops/backend/main.lk`) → native binary + Postgres
- **Frontend:** TypeScript + Vite (`apps/devops/frontend/`) — matches lukelang.org theme
- **URL:** https://lukelang.org/ops/

## Local development

```bash
# Postgres
apps/devops/scripts/setup_postgres.sh

# API
cd vm && ./build/luke BUILD ../apps/devops/backend/main.lk -o ../apps/devops/backend/devops_api
LUKE_PG_CONN='host=127.0.0.1 dbname=luke_devops user=luke_ops password=luke_ops' \
  ../apps/devops/backend/devops_api

# UI (proxies /api → :8820)
cd apps/devops/frontend && npm ci && npm run dev
```

## Deploy (VPS)

```bash
apps/devops/scripts/build.sh
apps/devops/deploy/install.sh
```

Collector runs every minute via cron (`collect_metrics.sh`).
