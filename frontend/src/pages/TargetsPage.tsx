import { FormEvent, useMemo, useState } from "react";
import { useLive } from "../live";
import { api, endpoint, navigate, Target, when } from "../model";
import { Link, useLocation } from "../route";

export function TargetsPage() {
  const { targets, setError, reload } = useLive();
  const href = useLocation();
  const params = new URLSearchParams(href.split("?")[1] ?? "");
  const typeFilter = params.get("type") ?? "all";
  const statusFilter = params.get("status") ?? "all";
  const [busy, setBusy] = useState(false);
  const [form, setForm] = useState({ name: "", host: "", port: "", type: "tcp", interval_seconds: "30" });

  const visible = useMemo(
    () =>
      targets.filter(
        (target) =>
          (typeFilter === "all" || target.type === typeFilter) &&
          (statusFilter === "all" || target.last_status === statusFilter),
      ),
    [targets, typeFilter, statusFilter],
  );

  function setFilter(key: "type" | "status", value: string) {
    const next = new URLSearchParams(params);
    if (value === "all") next.delete(key);
    else next.set(key, value);
    const query = next.toString();
    navigate(query ? `/targets?${query}` : "/targets");
  }

  async function onCreate(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError("");
    try {
      const body: Record<string, unknown> = {
        name: form.name,
        host: form.host,
        type: form.type,
        interval_seconds: Number(form.interval_seconds),
      };
      if (form.port !== "") body.port = Number(form.port);
      const created = await api<Target>("/api/targets", { method: "POST", body: JSON.stringify(body) });
      setForm({ name: "", host: "", port: "", type: "tcp", interval_seconds: "30" });
      await reload();
      navigate(`/targets/${created.id}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not add the target");
    } finally {
      setBusy(false);
    }
  }

  return (
    <>
      <form className="panel" onSubmit={onCreate}>
        <label>Name<input required value={form.name} onChange={(event) => setForm({ ...form, name: event.target.value })} /></label>
        <label>Host<input required value={form.host} onChange={(event) => setForm({ ...form, host: event.target.value })} placeholder="192.168.1.1" /></label>
        <label>Port<input value={form.port} onChange={(event) => setForm({ ...form, port: event.target.value })} placeholder="optional" /></label>
        <label>Type
          <select value={form.type} onChange={(event) => setForm({ ...form, type: event.target.value })}>
            <option value="tcp">tcp</option>
            <option value="http">http</option>
            <option value="https">https</option>
            <option value="dns">dns</option>
          </select>
        </label>
        <label>Interval (seconds)<input required value={form.interval_seconds} onChange={(event) => setForm({ ...form, interval_seconds: event.target.value })} /></label>
        <button disabled={busy} type="submit">Add target</button>
      </form>

      <section className="table-wrap panel">
        <div className="filters">
          <select value={typeFilter} onChange={(event) => setFilter("type", event.target.value)}>
            <option value="all">All types</option>
            <option value="tcp">tcp</option>
            <option value="http">http</option>
            <option value="https">https</option>
            <option value="dns">dns</option>
          </select>
          <select value={statusFilter} onChange={(event) => setFilter("status", event.target.value)}>
            <option value="all">All statuses</option>
            <option value="online">online</option>
            <option value="degraded">degraded</option>
            <option value="offline">offline</option>
            <option value="unknown">unknown</option>
          </select>
        </div>
        <table>
          <thead>
            <tr><th>Name</th><th>Target</th><th>Status</th><th>Latency</th><th>Checked</th></tr>
          </thead>
          <tbody>
            {visible.map((target) => (
              <tr key={target.id}>
                <td><Link to={`/targets/${target.id}`}>{target.name}</Link></td>
                <td>{endpoint(target)}</td>
                <td><span className={`pill ${target.last_status}`}>{target.last_status}</span></td>
                <td>{target.last_latency_ms == null ? "—" : `${target.last_latency_ms} ms`}</td>
                <td>{when(target.last_checked_at_ms)}</td>
              </tr>
            ))}
            {visible.length === 0 ? <tr><td colSpan={5} className="muted">No targets match.</td></tr> : null}
          </tbody>
        </table>
      </section>
    </>
  );
}
