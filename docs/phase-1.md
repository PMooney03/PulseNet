# Phase 1 — foundation, data model, and persistence

Phase 1 is the storage core. The check engine built on top of it is described in [phase-2.md](phase-2.md). This note covers the database those checks write to.

## Layout

```text
pulsenet/
├── CMakeLists.txt              # top-level entry, delegates to backend/
├── backend/
│   ├── CMakeLists.txt
│   ├── config.example.json
│   ├── include/pulsenet/
│   │   ├── config.hpp
│   │   ├── error.hpp           # Result / Status for expected failures
│   │   ├── models/             # targets, results, incidents, validation
│   │   ├── database/           # connection, migration, repositories
│   │   └── utilities/          # logger, UTC time, small string helpers
│   ├── src/                    # matching implementations
│   │   └── database/statement.hpp   # internal; not part of the public API
│   ├── tests/
│   └── third_party/            # SQLite amalgamation, nlohmann/json
├── docs/
└── scripts/verify-phase1.ps1   # Docker build + tests on this machine
```

TCP, HTTP, HTTPS, and DNS checks are Phase 2. The scheduler, incidents, the HTTP API, and the dashboard sit on this storage layer.

## What owns what

`Database` owns the `sqlite3*` with `std::unique_ptr` and a custom deleter that calls `sqlite3_close_v2`. There is no `shared_ptr`. The application creates one `Database`, then creates repositories that store a `Database&`. The repositories are destroyed first. A reference is enough because nothing shares ownership of the connection.

`Statement` owns one `sqlite3_stmt*` and finalizes it in its destructor, including when a bind or step throws.

Expected failures are values:

- validation problems
- missing rows
- foreign-key or check failures

`Result<T>` and `Status` carry those. Exceptions are for a broken database: the file cannot be opened, the schema cannot be read, or SQLite returns an error we cannot turn into one of those cases. Callers do not use exceptions for "target 5 does not exist".

## Concurrency

One connection. Every public `Database` method takes `mutex_` before using it. The important case is a multi-step critical section: `INSERT`, then `sqlite3_last_insert_rowid`. SQLite's own mutex serializes individual C API calls, but it does not keep another thread from running a statement in between those two calls. Our mutex does.

`std::mutex` is not recursive. A repository callback must not call back into `Database`. That deadlock is intentional. A recursive mutex would hide the lock-order bug.

The connection is also opened with `SQLITE_OPEN_FULLMUTEX` as a backstop, and `sqlite3_busy_timeout` is 5 seconds. WAL mode plus `synchronous=NORMAL` is the usual setting for a local desktop database: a crash is unlikely to tear a commit, without fsync on every write. Phase 1 still does not run two writers. The mutex is the correctness boundary. WAL is durability, not a connection pool.

The logger has its own mutex so one line is not split across threads. A log sink must not call the logger or the database.

Config is loaded once on the main thread before any worker exists. `getenv` is not used again from worker threads.

## Time and schema

Timestamps are UTC Unix milliseconds in `INTEGER` columns. They sort correctly and convert to `std::chrono::system_clock::time_point` without a timezone library. An unset `time_point` (the epoch) on insert means "stamp this now". A caller-supplied time is stored as given, which is what the tests use.

Schema changes go through `Migrator`. Version 1 creates `targets`, `results`, and `incidents`. `schema_migrations` records the version. Applying migrations twice is safe. SQLite DDL is transactional, so a failed version-1 migration rolls the new tables and the version row back together.

`PRAGMA foreign_keys = ON` is executed for every new connection. SQLite leaves foreign keys off unless you ask. `ON DELETE CASCADE` then removes results and incidents when a target is deleted.

## Models

| Type | Role in Phase 1 |
| --- | --- |
| `MonitorTarget` | User configuration: host, port, type, interval, timeout, thresholds, enabled |
| `MonitorResult` | One stored check. The engine does not exist yet, so tests write rows directly |
| `Incident` | One stored incident. Detection and auto-resolve are Phase 3. `resolve()` only updates the row |
| `MonitorType` | `tcp`, `http`, `https`, `dns` |
| `TargetStatus` | `online`, `degraded`, `offline`, `unknown` |

The repository does not decide whether a target is healthy. It stores the status it is given. Health calculation belongs to the monitoring engine in Phase 2.

Validation limits, so later phases cannot persist nonsense:

- TCP targets require a port. DNS, HTTP, and HTTPS may omit it.
- Host and port are separate. `8.8.8.8:53` and `https://example.com` are rejected.
- Names and reasons are trimmed before insert.

ICMP is not a monitor type. A ping monitor needs raw sockets and elevated privileges, and the product requirements are DNS, TCP, and HTTP(S).

## Configuration precedence

Lowest to highest:

1. Built-in defaults (`data/pulsenet.db`, log level `info`)
2. JSON file from `--config`
3. `PULSENET_DATABASE_PATH` and `PULSENET_LOG_LEVEL`
4. `--db`, `--log-level`, and `--demo`

`--help` and `--version` do not open the database.

## Third-party code

All of it is free:

| Piece | Version | License |
| --- | --- | --- |
| SQLite amalgamation | 3.54.1 | Public domain |
| nlohmann/json | 3.11.3 | MIT |
| GoogleTest | 1.15.2 (downloaded by CMake) | BSD-3-Clause |

Boost is not a dependency until the monitoring engine needs Asio and Beast.
