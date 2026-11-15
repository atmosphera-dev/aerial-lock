#!/usr/bin/env bash
set -euo pipefail

# Stock v0.4 ignores TMPDIR. Keep the existing real-session driver's tempfile-only
# fix; keymap generation, input and roundtrips remain upstream code.
mkdir -p /var/tmp/wtype
cd /var/tmp/wtype
curl --fail --location --retry 3 --max-time 120 \
    https://codeload.github.com/atx/wtype/tar.gz/refs/tags/v0.4 -o source.tar.gz
echo 'da91786d828b6a6e29b884bc510473939eda052658ebef87d7bdeafa6a8746f9  source.tar.gz' | sha256sum -c -
tar -xf source.tar.gz --strip-components=1
python3 - <<'PY'
from pathlib import Path
path = Path("main.c")
text = path.read_text()
old = '\tchar filename[] = "/tmp/wtype-XXXXXX";'
assert text.count(old) == 1
path.write_text(text.replace(old, '''\tconst char *directory = getenv("TMPDIR");
\tif (!directory || directory[0] != '/') fail("Absolute TMPDIR required");
\tchar filename[strlen(directory) + sizeof("/wtype-XXXXXX")];
\tsnprintf(filename, sizeof(filename), "%s/wtype-XXXXXX", directory);'''))
PY
wayland-scanner client-header protocol/virtual-keyboard-unstable-v1.xml virtual-keyboard-unstable-v1-client-protocol.h
wayland-scanner private-code protocol/virtual-keyboard-unstable-v1.xml virtual-keyboard-protocol.c
read -r -a flags <<< "$(pkg-config --cflags --libs wayland-client xkbcommon)"
cc -O2 -D_FILE_OFFSET_BITS=64 -o /usr/local/bin/wtype main.c virtual-keyboard-protocol.c \
    "${flags[@]}" -lrt
install -Dm644 LICENSE /usr/share/licenses/wtype/LICENSE
cd /var/tmp
rm -rf /var/tmp/wtype
