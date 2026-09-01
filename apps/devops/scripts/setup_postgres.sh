#!/usr/bin/env bash
# Create Postgres role + database for the DevOps dashboard.
set -euo pipefail

DB_NAME="${LUKE_DEVOPS_DB:-luke_devops}"
DB_USER="${LUKE_DEVOPS_USER:-luke_ops}"
DB_PASS="${LUKE_DEVOPS_PASS:-luke_ops}"

sudo -u postgres psql -v ON_ERROR_STOP=1 <<SQL
DO \$\$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = '${DB_USER}') THEN
    CREATE ROLE ${DB_USER} LOGIN PASSWORD '${DB_PASS}';
  END IF;
END
\$\$;
SELECT 'CREATE DATABASE ${DB_NAME} OWNER ${DB_USER}'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = '${DB_NAME}')\gexec
GRANT ALL PRIVILEGES ON DATABASE ${DB_NAME} TO ${DB_USER};
SQL

DIR="$(cd "$(dirname "$0")" && pwd)"
SCHEMA="${DIR}/../backend/schema.sql"
if [[ ! -f "$SCHEMA" && -f "${DIR}/schema.sql" ]]; then
  SCHEMA="${DIR}/schema.sql"
fi

sudo -u postgres psql -v ON_ERROR_STOP=1 -d "$DB_NAME" -f "$SCHEMA"

echo "Postgres ready: db=${DB_NAME} user=${DB_USER}"
