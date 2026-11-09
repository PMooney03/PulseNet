import { endpoint, ticket, when } from "../model";
import { useLive } from "../live";
import { Link } from "../route";

export function Dashboard() {
  const { targets, summary, incidents } = useLive();
  const attention = targets.filter((target) => target.last_status !== "online");

  return (
    <>
      <section className="counts">
        <Link className="count" to="/targets?status=online"><strong>{summary?.online ?? 0}</strong><span>online</span></Link>
        <Link className="count" to="/targets?status=degraded"><strong>{summary?.degraded ?? 0}</strong><span>degraded</span></Link>
        <Link className="count" to="/targets?status=offline"><strong>{summary?.offline ?? 0}</strong><span>offline</span></Link>
        <Link className="count" to="/targets?status=unknown"><strong>{summary?.unknown ?? 0}</strong><span>unknown</span></Link>
        <Link className="count" to="/incidents"><strong>{summary?.open_incidents ?? 0}</strong><span>open incidents</span></Link>
      </section>

      <div className="layout">
        <section>
          <div className="section-head">
            <h2>Targets</h2>
            <Link to="/targets">Manage</Link>
          </div>
          {targets.length === 0 ? <p className="muted panel">No targets yet. Add one from the targets page.</p> : (
            <div className="cards">
              {targets.map((target) => (
                <Link className="card panel" key={target.id} to={`/targets/${target.id}`}>
                  <div className="card-top">
                    <strong>{target.name}</strong>
                    <span className={`pill ${target.last_status}`}>{target.last_status}</span>
                  </div>
                  <div className="muted">{endpoint(target)}</div>
                  <div className="muted">
                    {target.last_latency_ms == null ? "No measurement" : `${target.last_latency_ms} ms`}
                    {" · "}
                    {when(target.last_checked_at_ms)}
                  </div>
                </Link>
              ))}
            </div>
          )}
        </section>

        <aside>
          <section className="panel">
            <h2>Needs attention</h2>
            {attention.length === 0 ? <p className="muted">Every target is online.</p> : attention.map((target) => (
              <Link className="incident" key={target.id} to={`/targets/${target.id}`}>
                <span className={`pill ${target.last_status}`}>{target.last_status}</span>
                <div>{target.name}</div>
                <div className="muted">{target.last_error || endpoint(target)}</div>
              </Link>
            ))}
          </section>
          <section className="panel">
            <h2>Open incidents</h2>
            {incidents.filter((incident) => incident.status !== "resolved").length === 0 ? <p className="muted">No open incidents.</p> : incidents.filter((incident) => incident.status !== "resolved").map((incident) => (
              <Link className="incident" key={incident.id} to={`/targets/${incident.target_id}`}>
                <span className={`pill ${incident.severity}`}>{incident.severity}</span>
                <div>{ticket(incident.id)}</div>
                <div>{incident.reason}</div>
                <div className="muted">{when(incident.started_at_ms)}</div>
              </Link>
            ))}
          </section>
        </aside>
      </div>
    </>
  );
}
