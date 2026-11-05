# PulseNet

PulseNet is a local network monitoring and diagnostics application. The monitoring engine, scheduler, health rules, and diagnostics are C++. A React dashboard will sit on top of a C++ REST and WebSocket API. Nothing here calls a paid service or a cloud account.

**This repository is at Phase 2.** The backend stores monitors and can run one check round: TCP connect, HTTP/HTTPS request, or DNS lookup. It then exits. The repeating scheduler is Phase 3.

```text
Phase 1  Foundation, models, SQLite
Phase 2  TCP / HTTP / DNS monitoring         <- you are here
Phase 3  Scheduler and incident detection
Phase 4  REST API and WebSockets
Phase 5  React dashboard
Phase 6  Diagnostics
Phase 7  Concurrency and test review
Phase 8  Docker Compose, screenshots, polish
```

## What a run prints

A run looks like this:

```text
PulseNet 0.1.0
status: ready
database: data/pulsenet.db
schema_version: 1
targets: 0
monitoring: not started
```

`--demo` inserts a sample TCP monitor for `8.8.8.8:53` if that row is not already there, then prints it. Running `--demo` twice does not create a second copy.

```text
demo_name: Google DNS
demo_host: 8.8.8.8
demo_port: 53
demo_type: tcp
```

The sample row is configuration until you pass `--check`. That flag runs one measurement per stored target.

```text
pulsenet --db data/pulsenet.db --demo --check
```

```text
monitoring: round complete
check: Google DNS status=online latency_ms=14 http=-
```

`http=-` means the monitor was not HTTP. A slow answer or an HTTP status outside 200-399 is `degraded`. No answer is `offline`. A disabled target is `unknown` and is not written to the history table.

## Architecture

```text
pulsenet --check
    |
    |-- CheckService          stores each real measurement
    |-- ProbeEngine           one io_context, a small thread pool
          |-- TCP / DNS / HTTP sessions   Boost.Asio and Beast
          |-- classify()                   pure status rules
    |-- repositories
          |-- SQLite
```

Checks for every target are in flight together. The pool size does not grow with the number of targets. Each session is owned by `unique_ptr` until `io_context::run()` returns, so completion handlers do not use `shared_ptr`. Status rules and the lifetime notes are in [docs/phase-2.md](docs/phase-2.md). Phase 1's database notes are in [docs/phase-1.md](docs/phase-1.md).

## Storage

```text
pulsenet (process)
    |
    |-- config and logger
    |
    |-- Migrator
    |
    |-- repositories (non-owning Database&)
            |
            |-- Database  (unique_ptr<sqlite3>, one mutex)
                    |
                    |-- SQLite file
```

`Database` owns the SQLite connection. Repositories borrow it. Expected errors (bad input, missing row, foreign key) come back as `Result` / `Status`. A database that cannot be opened throws.

One mutex wraps the connection so an insert and the following `last_insert_rowid` cannot be split by another thread. Details and the interview-oriented notes are in [docs/phase-1.md](docs/phase-1.md).

## Free software used

| Component | License |
| --- | --- |
| SQLite 3.54.1 amalgamation, vendored | Public domain |
| nlohmann/json 3.11.3, vendored | MIT |
| GoogleTest 1.15.2, fetched by CMake | BSD-3-Clause |
| Boost (Asio and Beast headers) | Boost Software License |
| OpenSSL | Apache 2.0 |
| Ubuntu base image used only to compile on this PC | free |

No API keys. The React app and Docker Compose are later phases. Boost and OpenSSL come from the system package manager inside the build container.

## Build and test on this PC

Docker is installed. A C++ compiler and CMake are not on `PATH`, so the checked-in way to build and test is:

```powershell
.\scripts\verify-phase1.ps1
```

That starts `ubuntu:24.04`, installs CMake, g++, and Ninja inside the container, configures the project, runs the tests, then runs `pulsenet` and `pulsenet --demo`. The first run needs a network connection so CMake can download GoogleTest. It does not install a compiler on Windows.

You should see `Phase 2 verification passed.`

Object files stay in the container (`/tmp/pulsenet-build`). The project directory is not filled with a `build` folder.

### Useful commands inside a build

If you would rather step through it:

```powershell
docker run --rm -it -v "${PWD}:/work" -w /work ubuntu:24.04 bash
```

Then, in that shell:

```bash
apt-get update
apt-get install -y --no-install-recommends cmake g++ ninja-build ca-certificates libboost-dev libssl-dev openssl
cmake -S /work -B /tmp/pulsenet-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/pulsenet-build
ctest --test-dir /tmp/pulsenet-build --output-on-failure
/tmp/pulsenet-build/bin/pulsenet --help
/tmp/pulsenet-build/bin/pulsenet --db /tmp/pulsenet.db --demo
```

### Flags

```text
pulsenet [--db PATH] [--log-level LEVEL] [--config FILE] [--demo] [--check]
pulsenet --help
pulsenet --version
```

| Input | Purpose |
| --- | --- |
| `--db` | SQLite file. Default `data/pulsenet.db` (the folder is created) |
| `--log-level` | `trace`, `debug`, `info`, `warn`, `error` |
| `--config` | JSON file. See `backend/config.example.json` |
| `--demo` | Ensure the sample Google DNS target exists |
| `--check` | Measure every stored target once, save the results, then exit |
| `PULSENET_DATABASE_PATH` | Same as `--db`, but flags win |
| `PULSENET_LOG_LEVEL` | Same as `--log-level`, but flags win |

Logs go to stderr. The status report goes to stdout.

## Native CMake build

Use this once a free compiler, CMake, Boost, and OpenSSL are installed. On Linux those are `libboost-dev`, `libssl-dev`, and `openssl`. The Docker command above installs them for you.

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\Debug\pulsenet.exe --demo
```

Single-config generators (Ninja, Makefiles) put the binary at `build/bin/pulsenet` and do not need `--config Debug`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/pulsenet --demo
```

C++20. The top-level `CMakeLists.txt` builds `backend/`. Configuring `backend/` on its own also works.

## Tests

GoogleTest covers:

- enum parsing and validation rules
- UTC timestamp formatting
- config precedence (file, environment, flags)
- schema migration, including running it twice
- target, result, and incident create/read/update
- foreign keys and cascade delete
- concurrent inserts against the single connection
- status rules for online, degraded, offline, and unknown
- localhost TCP, HTTP, and HTTPS probes, including a timeout and parallel checks

Probe tests talk to `127.0.0.1` only. They do not need the public internet. `--demo --check` is the optional live check against `8.8.8.8`.

## Project layout

```text
backend/include/pulsenet/models        domain records
backend/include/pulsenet/database      connection, migration, repositories
backend/include/pulsenet/monitoring    status rules, probe engine, check round
backend/include/pulsenet/utilities     logger and time
backend/src                            implementations
backend/tests                          GoogleTest
backend/third_party/sqlite             SQLite amalgamation
backend/third_party/nlohmann          json.hpp
```

## Decisions that affect later phases

These are the choices Phase 3 will build on.

- Monitor types are `tcp`, `http`, `https`, and `dns`. There is no ICMP ping.
- Host and port are stored separately. HTTP checks request `/`. A URL in the host field is rejected until a form splits it.
- `--check` runs one round and the process exits. The repeating scheduler is Phase 3.
- One SQLite file, one connection, one mutex.
- Health is calculated in C++ from the probe observation. The dashboard will only display it.
- HTTPS verifies the certificate chain unless a test turns that off. A self-signed certificate is offline.
