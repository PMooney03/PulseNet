export type Status = "online" | "degraded" | "offline" | "unknown";

export interface Target {
  id: number;
  name: string;
  host: string;
  port: number | null;
  type: "tcp" | "http" | "https" | "dns";
  interval_seconds: number;
  timeout_ms: number;
  enabled: boolean;
  latency_threshold_ms: number;
  failure_threshold: number;
  last_status: Status;
  last_latency_ms: number | null;
  last_checked_at_ms: number | null;
  last_error: string | null;
}

export interface Summary {
  online: number;
  degraded: number;
  offline: number;
  unknown: number;
  open_incidents: number;
}

export interface Incident {
  id: number;
  target_id: number;
  severity: "info" | "warning" | "critical";
  reason: string;
  started_at_ms: number;
  resolved_at_ms: number | null;
  status: string;
}

export interface CheckResult {
  id: number;
  status: Status;
  response_time_ms: number | null;
  http_status: number | null;
  error_message: string | null;
  checked_at_ms: number;
}

export async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...init,
  });
  const text = await response.text();
  const body = text ? JSON.parse(text) : {};
  if (!response.ok) {
    throw new Error(body.error ?? response.statusText);
  }
  return body as T;
}

export function when(ms: number | null): string {
  if (!ms) return "not checked";
  return new Date(ms).toLocaleString();
}

export function ticket(id: number): string {
  return `PN-${id}`;
}

export function endpoint(target: Pick<Target, "host" | "port" | "type">): string {
  return `${target.host}${target.port ? `:${target.port}` : ""} · ${target.type}`;
}

export function navigate(to: string) {
  const next = to.startsWith("/") ? to : `/${to}`;
  if (next === window.location.pathname + window.location.search) return;
  window.history.pushState({}, "", next);
  window.dispatchEvent(new PopStateEvent("popstate"));
}
