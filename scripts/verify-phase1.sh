#!/usr/bin/env bash
# Build and test inside a Linux container. Object files stay in /tmp
# so the Windows project folder is not filled with compiler output.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends cmake g++ ninja-build ca-certificates libboost-dev libssl-dev openssl

BUILD_DIR=/tmp/pulsenet-build
cmake -S /work -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR"
"$BUILD_DIR/bin/pulsenet_tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure --no-tests=error

echo
echo "---- startup ----"
/tmp/pulsenet-build/bin/pulsenet --db /tmp/empty.db | tee /tmp/empty.out
grep -q "status: ready" /tmp/empty.out
grep -q "schema_version: 1" /tmp/empty.out
grep -q "targets: 0" /tmp/empty.out
grep -q "monitoring: not started" /tmp/empty.out

echo
echo "---- demo ----"
/tmp/pulsenet-build/bin/pulsenet --db /tmp/pulsenet.db --demo | tee /tmp/demo.out
grep -q "demo_name: Google DNS" /tmp/demo.out
grep -q "demo_host: 8.8.8.8" /tmp/demo.out
grep -q "demo_port: 53" /tmp/demo.out
grep -q "targets: 1" /tmp/demo.out

echo
echo "---- demo again (must not duplicate the sample target) ----"
/tmp/pulsenet-build/bin/pulsenet --db /tmp/pulsenet.db --demo | tee /tmp/demo2.out
grep -q "targets: 1" /tmp/demo2.out

echo
echo "---- check with no targets ----"
/tmp/pulsenet-build/bin/pulsenet --db /tmp/empty.db --check | tee /tmp/check-empty.out
grep -q "monitoring: round complete" /tmp/check-empty.out
grep -q "targets: 0" /tmp/check-empty.out

echo
echo "---- check the sample target ----"
/tmp/pulsenet-build/bin/pulsenet --db /tmp/pulsenet.db --check | tee /tmp/check-demo.out
grep -q "check: Google DNS status=" /tmp/check-demo.out
grep -q "monitoring: round complete" /tmp/check-demo.out

echo
echo "PulseNet verification passed."
