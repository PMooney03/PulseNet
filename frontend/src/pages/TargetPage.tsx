import { useEffect, useState } from "react";
import { useLive } from "../live";
import { api, CheckResult, endpoint, navigate, ticket, when } from "../model";
import { Link } from "../route";

export function TargetPage({ id }: { id: number }) {
  const { targets, incidents, loaded, setError, reload } = useLive();
  const target = targets.find((item) => item.id === id) ?? null;
  const mine = incidents.filter((incident) => incident.target_id === id);
  const open = mine.filter((incident) => incident.status !== "resolved");
  const resolved = mine.filter((incident) => incident.status === "resolved");
  const [history, setHistory] = useState<CheckResult[]>([]);
  const [summaryText, setSummaryText] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let cancelled = false;
    api<{ results: CheckResult[] }>(`/api/targets/${id}/results`)
      .then((body) => {
        if (!cancelled) setHistory(body.results);
      })
      .catch((reason: Error) => setError(reason.message));
    api<{ summary: string }>(`/api/targets/${id}/diagnostics`)
      .then((body) => {
        if (!cancelled) setSummaryText(body.summary);
      })
      .catch((reason: Error) => setError(reason.message));
    return () => {
      cancelled = true;
    };
  }, [id, targets, setError]);

  async function checkNow() {
    setBusy(true);
    setError("");
    try {
      const body = await api<{ summary: string }>(`/api/targets/${id}/diagnose`, { method: "POST" });
      setSummaryText(body.summary);
      await reload();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Check failed");
    } finally {
      setBusy(false);
    }
  }

  async function removeTarget() {
    setBusy(true);
    setError("");
    try {
      await api(`/api/targets/${id}`, { method: "DELETE" });
      await reload();
      navigate("/targets");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Could not delete the target");
      setBusy(false);
    }
  }

  if (!target) {
    return (
      <section className="panel">
        <Link className="back" to="/targets">Targets</Link>
        <h2>{loaded ? "Target not found" : "Loading"}</h2>
        {loaded ? <p className="muted">That target is not in the database.</p> : null}
      </section>
    );
  }

  return (
    <section className="panel target-page">
      <Link className="back" to="/targets">Targets</Link>
      <div className="card-top">
        <h2>{target.name}</h2>
        <span className={`pill ${target.last_status}`}>{target.last_status}</span>
      </div>
      <p className="muted">{endpoint(target)}</p>
      <p>{summaryText || "Waiting for the latest explanation."}</p>

      <div className="facts">
        <div><strong>{target.interval_seconds}s</strong><span>interval</span></div>
        <div><strong>{target.timeout_ms} ms</strong><span>timeout</span></div>
        <div><strong>{target.latency_threshold_ms} ms</strong><span>slow after</span></div>
        <div><strong>{target.failure_threshold}</strong><span>failures to alert</span></div>
        <div><strong>{target.enabled ? "yes" : "no"}</strong><span>enabled</span></div>
      </div>

      <div className="actions">
        <button disabled={busy} onClick={checkNow} type="button">Check now</button>
        <button className="ghost" disabled={busy} onClick={removeTarget} type="button">Delete</button>
      </div>

      <h3>Incidents</h3>
      {open.length === 0 ? <p className="muted">No open incidents for this target.</p> : open.map((incident) => (
        <div className="incident" key={incident.id}>
          <span className={`pill ${incident.severity}`}>{incident.severity}</span>
          <div>{ticket(incident.id)}</div>
          <div>{incident.reason}</div>
          <div className="muted">{when(incident.started_at_ms)}</div>
        </div>
      ))}
      {resolved.length === 0 ? null : resolved.map((incident) => (
        <div className="incident" key={incident.id}>
          <span className="pill resolved">resolved</span>
          <div>{ticket(incident.id)} resolved</div>
          <div>{incident.reason}</div>
          <div className="muted">{when(incident.resolved_at_ms)}</div>
        </div>
      ))}

      <h3>History</h3>
      {history.length === 0 ? <p className="muted">No checks stored yet.</p> : (
        <ul>
          {history.map((result) => (
            <li className="history" key={result.id}>
              <span className={`pill ${result.status}`}>{result.status}</span>{" "}
              {result.response_time_ms == null ? "" : `${result.response_time_ms} ms `}
              {result.http_status == null ? "" : `HTTP ${result.http_status} `}
              <span className="muted">{when(result.checked_at_ms)}</span>
              {result.error_message ? <div className="muted">{result.error_message}</div> : null}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
