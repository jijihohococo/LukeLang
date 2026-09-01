-- Schema for luke_devops (run inside database)

CREATE TABLE IF NOT EXISTS services (
  id          SERIAL PRIMARY KEY,
  slug        TEXT UNIQUE NOT NULL,
  name        TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  check_url   TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS service_checks (
  id          BIGSERIAL PRIMARY KEY,
  service_id  INT NOT NULL REFERENCES services(id) ON DELETE CASCADE,
  status      TEXT NOT NULL CHECK (status IN ('up', 'degraded', 'down')),
  latency_ms  INT NOT NULL DEFAULT 0,
  checked_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS service_checks_service_time
  ON service_checks (service_id, checked_at DESC);

CREATE TABLE IF NOT EXISTS host_metrics (
  id          BIGSERIAL PRIMARY KEY,
  cpu_pct     REAL NOT NULL,
  mem_pct     REAL NOT NULL,
  disk_pct    REAL NOT NULL,
  load1       REAL NOT NULL DEFAULT 0,
  recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS host_metrics_time ON host_metrics (recorded_at DESC);

CREATE TABLE IF NOT EXISTS incidents (
  id          SERIAL PRIMARY KEY,
  title       TEXT NOT NULL,
  status      TEXT NOT NULL CHECK (status IN ('investigating', 'identified', 'resolved')),
  summary     TEXT NOT NULL DEFAULT '',
  started_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  resolved_at TIMESTAMPTZ
);

INSERT INTO services (slug, name, description, check_url) VALUES
  ('website', 'Website', 'lukelang.org — landing, learn, download', 'https://lukelang.org/'),
  ('docs', 'Documentation', 'Hosted docs at /docs/', 'https://lukelang.org/docs/'),
  ('packages', 'Package registry', 'packages.lukelang.org index + tarballs', 'https://packages.lukelang.org/index.json'),
  ('status', 'Status page', 'status.lukelang.org', 'https://status.lukelang.org/'),
  ('ci', 'Continuous integration', 'GitHub Actions main branch', 'https://api.github.com/repos/lucasdmarshall/LukeLang/commits/main/status')
ON CONFLICT (slug) DO NOTHING;

GRANT ALL ON ALL TABLES IN SCHEMA public TO luke_ops;
GRANT ALL ON ALL SEQUENCES IN SCHEMA public TO luke_ops;
