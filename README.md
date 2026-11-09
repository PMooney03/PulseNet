# PulseNet

PulseNet is a free network monitor that runs on your own machine. You tell it which hosts to watch. On a timer it checks each one, stores the result, and shows whether that service is up, slow, or down. If the same failure keeps happening, it opens a ticket. When the service answers again, the ticket is marked resolved. Optional Slack messages use the same ticket number.

Nothing leaves the machine except the checks themselves and, if you turn it on, the Slack webhook. There is no monitoring account and no paid API. History and incidents stay in a local SQLite file.

Each target is one of four checks:

- **TCP** — open a connection to a host and port.
- **HTTP** — request the host and read the status code. The default port is 80.
- **HTTPS** — the same request over TLS, and the certificate is verified. The default port is 443.
- **DNS** — resolve the hostname.

A slow answer, or an HTTP status outside 200–399, is **degraded**, not offline. The service did respond. **Offline** means nothing answered: timeout, refused connection, DNS failure, or a bad certificate.

## Boot it

You need [Docker Desktop](https://www.docker.com/products/docker-desktop/) running. You do not need a C++ compiler or Node installed on Windows. The image builds both.

1. Open PowerShell in this folder.

```powershell
cd C:\Users\pierc\Desktop\PulseNet
```

2. Build the image and start the monitor. The first run compiles the C++ engine and the dashboard, so it takes a few minutes. Later starts reuse the image.

```powershell
docker compose up --build
```

3. Wait until the log prints `listening: 0.0.0.0:8080`. Then open [http://127.0.0.1:8080](http://127.0.0.1:8080).

The container starts with `--demo`, so the first boot adds one sample target, **Google DNS** (`8.8.8.8`, port 53, TCP). The **Live** label on the dashboard means the page is receiving new results. That sample needs the public internet. Your own targets can be local.

4. Stop it with `Ctrl+C` in that same window. The database stays in the Docker volume `pulsenet-data`, so targets and tickets are still there next time.

Start it again, without rebuilding, with:

```powershell
docker compose up
```

To run it in the background instead, use `docker compose up -d`. `docker compose down` stops the container and leaves the volume in place. `docker compose down -v` deletes that database.

## How a check becomes an incident

The sample target is Google DNS (`8.8.8.8:53`, TCP). The **Live** label means the page is receiving new results over a WebSocket.

**1. The dashboard is the overview.** Counts across the top open a filtered list. Each card is one target. Anything offline, slow, or unknown is called out under Needs attention, and open incidents sit beside that.

![Dashboard with Google DNS and Recovery check online](Images/Screenshot%202026-11-09%20140720.png)

**2. Targets is where you add a host.** Name, host, optional port, check type, and how often to measure it. Leave the port empty for HTTP (80), HTTPS (443), and DNS. Filters narrow the table by type or status. Click a name to open that target: the plain-language explanation, its settings, **Check now**, delete, and the check history.

![Target list and the add-target form](Images/Screenshot%202026-11-09%20140732.png)

**3. Repeated failures open a ticket.** One bad check is not an incident. The same kind of failure has to repeat for that target's threshold (3 checks on a target added from the page). Unavailable is critical. Sustained slowness is a warning. A single status change is info. The ticket number is `PN-` plus the incident id.

When a later check succeeds, that ticket is marked resolved. Open tickets stay at the top of Incidents. Fixed ones stay underneath with the same number, so you can see what happened.

![Incidents page with PN-8 and older tickets resolved](Images/Screenshot%202026-11-09%20140742.png)

**4. Critical and warning tickets also post to Slack.** The down message names the ticket, the target, and why nothing answered. The next successful check posts again and says that ticket is resolved. Info changes stay on the dashboard.

![Slack channel showing PN-7 and PN-8 opening and resolving](Images/Screenshot%202026-11-09%20141144.png)

To turn alerts on, put a Slack incoming-webhook URL in a file named `.env` in this folder. That file is not committed.

```text
PULSENET_TEAMS_WEBHOOK=https://hooks.slack.com/services/your-webhook
```

Restart with `docker compose up -d`. The log line `teams: on` means the URL was read. A target that stays online does not send a message.

## What a check means

| Status | Meaning |
| --- | --- |
| online | The service answered within its latency limit. HTTP 200–399 counts, including redirects. |
| degraded | The service answered, but it was slow or the HTTP status was outside 200–399. |
| offline | Nothing answered: timeout, DNS failure, connection refused, or a TLS failure. |
| unknown | The target is disabled or the settings are invalid. These are not stored as history. |

## What is in the box

```text
backend/     C++20 engine, SQLite, HTTP API, WebSocket
frontend/    React dashboard
Dockerfile   builds both and serves the dashboard on port 8080
```

The monitoring engine is C++20. The dashboard is only a view of what that engine stored. Notes on storage and on how a single check is measured are in [docs/phase-1.md](docs/phase-1.md) and [docs/phase-2.md](docs/phase-2.md).

`.\scripts\verify-phase1.ps1` builds the engine in Docker, runs the tests, and measures the sample target once. The normal way to use PulseNet is `docker compose up`.
