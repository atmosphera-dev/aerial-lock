#!/usr/bin/env bash
set -euo pipefail

# Like test.sh, never build in the runner's read-only checkout. This is a
# separate fresh container, with its own unprivileged PAM fixture and compositor.
mkdir -p /work/src /work/tmp
cp -a . /work/src/
cd /work/src
export TMPDIR=/work/tmp MAKEFLAGS=-j2 CMAKE_BUILD_PARALLEL_LEVEL=2
timeout 5m make install DEBUG_DISMISS=0 DEBUG_VERBOSE=0
timeout 2m python3 Tests/ci-lock-smoke.py
timeout 2m python3 Tests/ci-emergency-smoke.py
