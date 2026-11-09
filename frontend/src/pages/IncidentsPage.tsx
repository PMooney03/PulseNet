import { useLive } from "../live";
import { Incident, ticket, when } from "../model";
import { Link } from "../route";

function IncidentRow({ incident, name }: { incident: Incident; name: string }) {
  const resolved = incident.status === "resolved";
  return (
    <Link className="incident" to={`/targets/${incident.target_id}`}>
      <span className={`pill ${incident.severity}`}>{incident.severity}</span>
      <div>{ticket(incident.id)}{resolved ? " resolved" : ""}</div>
      <div>{name}</div>
      <div>{incident.reason}</div>
      <div className="muted">{resolved ? `Resolved ${when(incident.resolved_at_ms)}` : when(incident.started_at_ms)}</div>
    </Link>
  );
}

export function IncidentsPage() {
  const { incidents, targets } = useLive();
  const nameOf = (targetId: number) => targets.find((item) => item.id === targetId)?.name ?? `Target ${targetId}`;
  const open = incidents.filter((incident) => incident.status !== "resolved");
  const resolved = incidents.filter((incident) => incident.status === "resolved");

  return (
    <>
      <section className="panel">
        <h2>Open incidents</h2>
        {open.length === 0 ? <p className="muted">No open incidents.</p> : open.map((incident) => (
          <IncidentRow incident={incident} key={incident.id} name={nameOf(incident.target_id)} />
        ))}
      </section>
      <section className="panel" style={{ marginTop: "1rem" }}>
        <h2>Resolved</h2>
        {resolved.length === 0 ? <p className="muted">No resolved tickets yet.</p> : resolved.map((incident) => (
          <IncidentRow incident={incident} key={incident.id} name={nameOf(incident.target_id)} />
        ))}
      </section>
    </>
  );
}
