#!/usr/bin/env bash
set -euo pipefail

# The checkout stays read-only. All generated files and fixture sockets live
# on the container's disk-backed writable layer, not a shared /tmp.
mkdir -p /work/src /work/build /work/tmp /work/runtime /work/results
cp -a . /work/src/
cd /work/src
export TMPDIR=/work/tmp XDG_RUNTIME_DIR=/work/runtime
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=opengl LIBGL_ALWAYS_SOFTWARE=1
export QML_IMPORT_PATH="$PWD/Tests/imports"
export CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2

timeout 5m make DEBUG_DISMISS=0 DEBUG_VERBOSE=0
timeout 2m cmake -S Tests -B /work/build -G Ninja -DAERIAL_LOCK_SOURCE="$PWD"
timeout 5m cmake --build /work/build
# The render-failure case needs a real rendering platform. Owned-instance is
# not registered without an operator-supplied VM manifest.
timeout 15m ctest --test-dir /work/build --output-on-failure --timeout 300 \
    -E '^media-fallback-render$'
timeout 2m node --test Tests/*.test.cjs

# Preserve the documented whole-log warning gate, including setup/teardown.
status=0
timeout 5m /usr/lib/qt6/bin/qmltestrunner -import Tests/imports -input Tests \
    > /work/results/qmltestrunner.log 2>&1 || status=$?
cat /work/results/qmltestrunner.log
qwarnings=$(awk '/^QWARN[[:space:]]*:/ { count++ } END { print count+0 }' /work/results/qmltestrunner.log)
printf 'Whole-log QWARN count: %s\n' "$qwarnings"
test "$status" -eq 0 && test "$qwarnings" -eq 0
