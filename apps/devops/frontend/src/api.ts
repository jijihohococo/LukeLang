export type ServiceStatus = "up" | "degraded" | "down" | "unknown";

export interface Overview {
  cpu: number;
  mem: number;
  disk: number;
  load1: number;
  services_up?: number;
  services_total: number;
  recorded_at?: string;
}

export interface ServiceRow {
  slug: string;
  name: string;
  description: string;
  check_url: string;
  status: ServiceStatus;
  latency_ms: number;
  checked_at: string;
}

export interface MetricPoint {
  cpu_pct: number;
  mem_pct: number;
  disk_pct: number;
  load1: number;
  recorded_at: string;
}

export interface Incident {
  id: number;
  title: string;
  status: "investigating" | "identified" | "resolved";
  summary: string;
  started_at: string;
  resolved_at: string;
}

const API_BASE = import.meta.env.DEV ? "/api" : "/ops/api";

async function get<T>(path: string): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, { cache: "no-store" });
  if (!res.ok) throw new Error(`${path} → ${res.status}`);
  return res.json() as Promise<T>;
}

export const api = {
  overview: () => get<Overview>("/overview"),
  services: () => get<ServiceRow[]>("/services"),
  metrics: () => get<MetricPoint[]>("/metrics"),
  incidents: () => get<Incident[]>("/incidents"),
};
