import "./style.css";
import { api, type MetricPoint, type Overview, type ServiceRow } from "./api";

const app = document.querySelector<HTMLDivElement>("#app")!;
const POLL_MS = 5000;

function pctClass(v: number): string {
  if (v >= 90) return "metric__value--bad";
  if (v >= 75) return "metric__value--warn";
  return "metric__value--ok";
}

function stateClass(status: string): string {
  if (status === "up") return "state state--up";
  if (status === "degraded") return "state state--degraded";
  if (status === "down") return "state state--down";
  return "state state--unknown";
}

function stateLabel(status: string): string {
  if (status === "up") return "Operational";
  if (status === "degraded") return "Degraded";
  if (status === "down") return "Down";
  return "Unknown";
}

function overallHeadline(overview: Overview, services: ServiceRow[]): string {
  const anyDown = services.some((s) => s.status === "down");
  const anyDeg = services.some((s) => s.status === "degraded");
  if (anyDown) return "Some systems <em>down</em>";
  if (anyDeg) return "Partial <em>degradation</em>";
  return "All systems <em>operational</em>";
}

function chartBars(points: MetricPoint[], pick: (p: MetricPoint) => number): string {
  if (!points.length) return "";
  const slice = [...points].reverse().slice(-60);
  return slice
    .map((p) => {
      const v = pick(p);
      const h = Math.max(8, Math.round((v / 100) * 72));
      const level = v >= 90 ? "bad" : v >= 75 ? "warn" : "ok";
      return `<i style="height:${h}px" data-level="${level}" title="${v.toFixed(1)}%"></i>`;
    })
    .join("");
}

function render(
  overview: Overview,
  services: ServiceRow[],
  metrics: MetricPoint[],
  incidents: Awaited<ReturnType<typeof api.incidents>>,
  updated: Date,
  err?: string,
) {
  const servicesHtml = services
    .map(
      (s) => `<li>
        <b>${escapeHtml(s.name)}</b>
        <span>${escapeHtml(s.description)}${s.latency_ms ? ` · ${s.latency_ms}ms` : ""}</span>
        <em class="${stateClass(s.status)}">${stateLabel(s.status)}</em>
      </li>`,
    )
    .join("");

  const incidentsHtml =
    incidents.length === 0
      ? `<p class="empty"><b>No incidents recorded.</b> Open issues appear here with status and timeline.</p>`
      : `<ul class="components">${incidents
          .map(
            (i) => `<li>
              <b>${escapeHtml(i.title)}</b>
              <span>${escapeHtml(i.summary || i.status)}</span>
              <em class="${i.status === "resolved" ? "state state--up" : "state state--degraded"}">${escapeHtml(i.status)}</em>
            </li>`,
          )
          .join("")}</ul>`;

  app.innerHTML = `
    <div class="grain" aria-hidden="true"></div>
    <header class="top">
      <a class="top__brand" href="https://lukelang.org">
        <img src="https://lukelang.org/assets/luke-mark-sm.png" alt="" width="32" height="32" />
        <span>LukeLang</span>
      </a>
      <nav class="top__links" aria-label="Primary">
        <a href="https://lukelang.org">Home</a>
        <a href="https://status.lukelang.org">Status</a>
        <a href="https://lukelang.org/docs/">Docs</a>
        <a href="https://github.com/lucasdmarshall/LukeLang/tree/main/apps/devops">Source</a>
      </nav>
    </header>

    <section class="head">
      <p class="head__kicker">DevOps · LukeLang backend</p>
      <h1><span class="beacon" aria-hidden="true"></span> ${overallHeadline(overview, services)}</h1>
      <p>
        Live host metrics and service checks from Postgres — API served by a compiled LukeLang binary.
        ${err ? `<br/><strong style="color:var(--red)">${escapeHtml(err)}</strong>` : ""}
      </p>
    </section>

    <main>
      <section>
        <h2>Host <em>metrics</em></h2>
        <div class="metrics">
          <div class="metric">
            <div class="metric__label">CPU</div>
            <div class="metric__value ${pctClass(overview.cpu)}">${fmt(overview.cpu)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Memory</div>
            <div class="metric__value ${pctClass(overview.mem)}">${fmt(overview.mem)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Disk</div>
            <div class="metric__value ${pctClass(overview.disk)}">${fmt(overview.disk)}%</div>
          </div>
          <div class="metric">
            <div class="metric__label">Load (1m)</div>
            <div class="metric__value">${overview.load1.toFixed(2)}</div>
          </div>
          <div class="metric">
            <div class="metric__label">Services up</div>
            <div class="metric__value metric__value--ok">${overview.services_up ?? "—"}/${overview.services_total}</div>
          </div>
        </div>
        <p class="meta">Last sample: ${overview.recorded_at ? escapeHtml(overview.recorded_at) : "—"} · refreshed ${updated.toISOString().replace("T", " ").slice(0, 19)} UTC</p>
      </section>

      <section>
        <h2>CPU <em>24h</em></h2>
        <div class="chart" aria-hidden="true">${chartBars(metrics, (p) => p.cpu_pct)}</div>
      </section>

      <section>
        <h2>Components</h2>
        <ul class="components">${servicesHtml}</ul>
      </section>

      <section>
        <h2>Incident <em>history</em></h2>
        ${incidentsHtml}
      </section>
    </main>

    <footer>
      <p>LukeLang DevOps · API: LukeLang + Postgres · UI: TypeScript</p>
      <nav><a href="https://lukelang.org/community/">Report a problem</a></nav>
    </footer>
  `;
}

function fmt(n: number): string {
  return Number.isFinite(n) ? n.toFixed(1) : "—";
}

function escapeHtml(s: string): string {
  return s
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

async function tick() {
  try {
    const [overview, services, metrics, incidents] = await Promise.all([
      api.overview(),
      api.services(),
      api.metrics(),
      api.incidents(),
    ]);
    render(overview, services, metrics, incidents, new Date());
  } catch (e) {
    render(
      { cpu: 0, mem: 0, disk: 0, load1: 0, services_up: 0, services_total: 0 },
      [],
      [],
      [],
      new Date(),
      e instanceof Error ? e.message : "API unreachable",
    );
  }
}

void tick();
setInterval(() => void tick(), POLL_MS);
