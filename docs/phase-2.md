# Phase 2 — one round of checks

Phase 2 measures a target and stores the result. It does not repeat on a timer. `pulsenet --check` runs every stored target once, prints a line for each, and exits. The scheduler that keeps doing that is Phase 3.

## What a check is

| Type | What "answered" means |
| --- | --- |
| `tcp` | DNS resolved the host and a TCP connection completed |
| `dns` | The resolver returned at least one address. Nothing is connected |
| `http` | A `GET /` returned an HTTP status line. Default port 80 if the row has none |
| `https` | Same request over TLS. Default port 443. The certificate is verified unless a test turns that off |

The default ports are applied for that probe only. They are not written back into the `targets` table.

## Status rules

`classify()` in `monitoring/health.cpp` is a pure function. The probe records an `Observation`. It does not decide online or offline itself.

| Status | Meaning | `success` | Stored? |
| --- | --- | --- | --- |
| `unknown` | The check was not attempted: the target is disabled, or it failed validation | false | No |
| `offline` | Nothing answered: timeout, DNS failure, connection refused, TLS failure, or an HTTP read that never produced a status | false | Yes |
| `degraded` | The service answered, but the latency was over the threshold, or the HTTP status was outside 200–399 | true | Yes |
| `online` | The service answered within the threshold. HTTP 200–399 counts, including redirects | true | Yes |

A latency equal to the threshold is online. Only a strictly greater time is degraded. `4xx` and `5xx` are degraded, not offline, because the server did respond. `CheckService` skips `unknown` and any result whose `target_id` is still 0, so disabled rows do not fill the history table. `--check` still prints them.

## Threads and ownership

`ProbeEngine::check_all` builds one `io_context` and a small pool (default 2, clamped to 1–32). Every target is started before any worker calls `run()`. Asio multiplexes the sockets; the pool does not grow with the number of targets.

Each probe is a `Session` owned by `unique_ptr` for the whole call. Completion handlers capture `this`. That is safe because:

1. `start()` only queues work.
2. The workers are joined before `check_all` returns.
3. The `unique_ptr`s are destroyed after that join.

There is no `shared_ptr` in this path. A handler cannot outlive the session, because `io_context::run()` does not return until every handler has finished, and the last handler drops the work guard that keeps `run()` alive.

The timer, resolver, and socket for one probe share a strand, so the timeout handler and the I/O handler never run at the same time. The timeout sets a flag, cancels the resolver and the socket, and the cancelled handler turns that flag into one `offline` result. A later handler sees that the session is already finished and does nothing.

The TLS context is configured on the calling thread before any worker starts. OpenSSL allows concurrent handshakes on one context after that. Sessions hold a reference to it and are destroyed first.

## Latency

The clock starts when the session is constructed and stops when the success path runs. DNS time is included. A timeout still records the elapsed time on the observation, but `unknown` results omit `response_time` because no measurement was made.

## What the tests cover

`test_health.cpp` feeds observations in and checks the four statuses. It does not open a socket.

`test_probes.cpp` talks only to `127.0.0.1`:

- TCP connect, HTTP 204, and HTTP 503
- a closed port, `localhost` DNS, a name that should not resolve, a disabled target, and a TCP target with no port
- a stalled server that must trip the timeout
- four HTTP checks against one slow server, which must overlap
- HTTPS against a local self-signed certificate: verified checks are offline, and `verify_tls = false` is online
- `CheckService` writes one row and reads it back

The public internet is not required for the tests. `scripts/verify-phase1.sh` also runs `--check` against the sample `8.8.8.8:53` row and only checks that a status line was printed. Offline is a valid result if that address is unreachable.
