import { LiveProvider, useLive } from "./live";
import { Dashboard } from "./pages/Dashboard";
import { IncidentsPage } from "./pages/IncidentsPage";
import { TargetPage } from "./pages/TargetPage";
import { TargetsPage } from "./pages/TargetsPage";
import { Link, targetIdFromPath, useLocation } from "./route";

function Shell() {
  const href = useLocation();
  const path = href.split("?")[0].replace(/\/$/, "") || "/";
  const { error, live } = useLive();
  const targetId = targetIdFromPath(path);

  let page = <Dashboard />;
  if (targetId != null) page = <TargetPage id={targetId} />;
  else if (path === "/targets") page = <TargetsPage />;
  else if (path === "/incidents") page = <IncidentsPage />;
  else if (path !== "/") page = (
    <section className="panel">
      <h2>Page not found</h2>
      <p className="muted">That address is not part of PulseNet.</p>
      <Link to="/">Back to the dashboard</Link>
    </section>
  );

  return (
    <main className="app">
      <header className="top">
        <div className="top-row">
          <div>
            <Link className="brand" to="/"><h1>PulseNet</h1></Link>
            <p>Local checks for hosts and services. Results stay on this machine.</p>
          </div>
          <div className={live ? "live" : "live off"}>{live ? "Live" : "Reconnecting"}</div>
        </div>
        <nav>
          <Link className={path === "/" ? "active" : ""} to="/">Dashboard</Link>
          <Link className={path === "/targets" || targetId != null ? "active" : ""} to="/targets">Targets</Link>
          <Link className={path === "/incidents" ? "active" : ""} to="/incidents">Incidents</Link>
        </nav>
      </header>
      {error ? <p className="error">{error}</p> : null}
      {page}
    </main>
  );
}

export function App() {
  return (
    <LiveProvider>
      <Shell />
    </LiveProvider>
  );
}
