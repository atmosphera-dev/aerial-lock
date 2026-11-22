#!/usr/bin/env bash
# Runs as root only in the fresh image-preparation guest, via cloud-init.
set -euo pipefail
umask 022
root=/var/lib/aerial-ci
mkdir -p "$root/tmp"
export TMPDIR="$root/tmp" MAKEFLAGS=-j2 CMAKE_BUILD_PARALLEL_LEVEL=2
exec >"$root/provision.log" 2>&1
# Publish only a complete record; a reader must never see an empty exit file.
publish_exit() {
    local status=$?
    printf '%s\n' "$status" > "$root/provision.exit.pending"
    mv -- "$root/provision.exit.pending" "$root/provision.exit"
}
trap publish_exit EXIT
# Pacman abandons its whole transaction after one stalled or reset transfer and
# never retries. Download the files it resolved (and their signatures) into its
# cache with bounded retries instead; pacman still verifies each cached
# package's signature and checksum before installing it.
fetch_packages() {
    local prefix=$1 cache=$2 locations=$3 location name config
    config="$TMPDIR/fetch-packages.curlrc"
    : > "$config"
    while IFS= read -r location; do
        case $location in
            file://*) continue ;;
            "$prefix"*) ;;
            *) printf 'unexpected package location: %s\n' "$location" >&2; return 1 ;;
        esac
        name=${location##*/}
        case $location$name in
            *[!A-Za-z0-9@._+:/-]* | */) printf 'unexpected package location: %s\n' "$location" >&2; return 1 ;;
        esac
        printf 'url = "%s"\noutput = "%s/%s"\n' \
            "$location" "$cache" "$name" "$location.sig" "$cache" "$name.sig" >> "$config"
    done < "$locations"
    if [[ -s $config ]]; then
        curl --fail --location --no-progress-meter --parallel --parallel-max 4 \
            --connect-timeout 30 --speed-limit 1024 --speed-time 30 \
            --retry 8 --retry-all-errors --config "$config"
    fi
    rm -f -- "$config"
}
cd "$root/provision"
snapshot=$(python3 -c 'import json; print(json.load(open("image.lock.json"))["snapshot"])')
# Pacman, not this shell, expands the repository/architecture placeholders.
# shellcheck disable=SC2016
printf 'Server = https://archive.archlinux.org/repos/%s/$repo/os/$arch\n' "$snapshot" > /etc/pacman.d/mirrorlist
mapfile -t packages < <(python3 -c 'import json; print("\n".join(json.load(open("image.lock.json"))["requested_packages"]))')
pacman -Syy --noconfirm
pacman -Sup --noconfirm --needed --print-format '%l' "${packages[@]}" > "$root/package-locations"
fetch_packages "https://archive.archlinux.org/repos/$snapshot/" /var/cache/pacman/pkg "$root/package-locations"
rm -f -- "$root/package-locations"
pacman -Su --noconfirm --needed "${packages[@]}"
systemctl enable --now qemu-guest-agent sshd
install -d /etc/systemd/system/qemu-guest-agent.service.d
printf '[Service]\nEnvironment=TMPDIR=%s\n' "$TMPDIR" > /etc/systemd/system/qemu-guest-agent.service.d/aerial-ci.conf
systemctl daemon-reload
systemctl restart qemu-guest-agent
if ! id builder >/dev/null 2>&1; then useradd --create-home builder; fi
install -d -o builder -g builder /home/builder/aerial-ci/tmp
package=qt6-mpvqml
install -d -o builder -g builder "/home/builder/aerial-ci/$package"
install -o builder -g builder "aur/$package/PKGBUILD" "/home/builder/aerial-ci/$package/PKGBUILD"
pushd "/home/builder/aerial-ci/$package"
runuser -u builder -- env TMPDIR=/home/builder/aerial-ci/tmp MAKEFLAGS=-j2 CMAKE_BUILD_PARALLEL_LEVEL=2 \
    makepkg --noconfirm --cleanbuild
popd
pacman -U --noconfirm "/home/builder/aerial-ci/$package/$package"-[0-9]*-x86_64.pkg.tar.zst
mkdir -p "$root/wtype"
cd "$root/wtype"
curl --fail --location --retry 3 --max-time 120 \
    "$(python3 -c 'import json; print(json.load(open("/var/lib/aerial-ci/provision/image.lock.json"))["wtype"]["url"])')" -o source.tar.gz
python3 - <<'PY'
import hashlib, json
from pathlib import Path
lock = json.loads(Path('/var/lib/aerial-ci/provision/image.lock.json').read_text())
assert hashlib.sha256(Path('source.tar.gz').read_bytes()).hexdigest() == lock['wtype']['sha256']
PY
tar -xf source.tar.gz --strip-components=1
python3 - <<'PY'
from pathlib import Path
p = Path('main.c'); text = p.read_text()
old = '\tchar filename[] = "/tmp/wtype-XXXXXX";'
assert text.count(old) == 1
p.write_text(text.replace(old, '''\tconst char *directory = getenv("TMPDIR");
\tif (!directory || directory[0] != '/') fail("Absolute TMPDIR required");
\tchar filename[strlen(directory) + sizeof("/wtype-XXXXXX")];
\tsnprintf(filename, sizeof(filename), "%s/wtype-XXXXXX", directory);'''))
PY
wayland-scanner client-header protocol/virtual-keyboard-unstable-v1.xml virtual-keyboard-unstable-v1-client-protocol.h
wayland-scanner private-code protocol/virtual-keyboard-unstable-v1.xml virtual-keyboard-protocol.c
read -r -a flags <<< "$(pkg-config --cflags --libs wayland-client xkbcommon)"
cc -O2 -D_FILE_OFFSET_BITS=64 -o /usr/local/bin/wtype main.c virtual-keyboard-protocol.c "${flags[@]}" -lrt
install -Dm644 LICENSE /usr/share/licenses/wtype/LICENSE
python3 "$root/provision/guest.py" inventory
# Runtime test instances have restricted networking. Their clock is supplied by
# QEMU's RTC, so an online NTP readiness job must not gate cloud-init's final
# account/SSH readiness marker. Keep timesyncd itself available; only the boot
# wait is masked in this disposable test image, never on the physical host.
systemctl mask systemd-time-wait-sync.service
pacman -Scc --noconfirm
# Individually owned preparation trees only; never shared or inherited user data.
find /home/builder/aerial-ci "$root/wtype" -mindepth 1 -delete
printf 'AERIAL_IMAGE_PROVISIONED\n'
