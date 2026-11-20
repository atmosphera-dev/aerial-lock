# Disposable KVM test platform

This route has no Ark, libvirt, bridge, fleet SSH key or desktop dependency.
Use a disposable Linux VM with nested KVM for local reproduction. Never run
these test workloads on your live desktop. Blacksmith x64 Linux runners are
ephemeral VMs with nested KVM support and provide the CI execution boundary.

## First group

`niri-emergency` is a separate group with exactly two recovery outcomes:
`missing-runner` and `main-crash` (three confirmed normal-locker deaths). Both
require the installed native emergency client, wrong/right real pam_unix,
unlock/sync, natural supervisor exit and desktop input without transition-key
leakage. Summaries reject missing, duplicate, mixed-backend or stale-identity
cells; JUnit names both cases. The locked image already includes libxkbcommon;
this feature does not regenerate its lock. Both helper and emergency bytes are
installed at their compiled paths and hashed against the staged payload.
The workflow dispatch choice propagates the group through request, guest and
validation. Image publication remains exclusive to niri-smoke on master.

`niri-smoke` runs the existing remediation driver's `niri / unlock / 1` case
under headless Sway, using Mesa software OpenGL. Sway only hosts the nested
niri window. Required outcomes are actual session-lock confirmation, real
keyboard/PAM unlock, natural locker exit 0 and native desktop input afterwards.
It is not wrong-password, supervisor, video-motion, multi-output, physical GPU,
IME, suspend or full acceptance proof. Existing portable and Sway PR checks
remain unchanged. The new workflow is manual only (`workflow_dispatch`).

## Rebuild and run

Inside the disposable execution VM, install Python 3, Git, OpenSSH client,
QEMU system-x86 and image tools, cloud-localds and its ISO creation dependency.
Grant that VM's ordinary test account read/write access to `/dev/kvm`.
No desktop display, GPU, libvirt daemon or container engine is required.
Use a disk-backed workspace with enough space for the official input,
preparation disk, compressed image and test overlay. Observe peak allocation;
sparse capacity is not available storage. Set TMPDIR before every command.

Example configuration, stored outside the source checkout:

```json
{
  "instance": "aerial-ci-image-build",
  "host": "my-disposable-builder-vm",
  "user": "tester",
  "storage": "/home/builder/aerial-work/vm/state",
  "guest_root": "/home/tester/aerial-ci",
  "cpus": 2,
  "memory_mib": 2048,
  "boot_seconds": 300,
  "build_seconds": 2100,
  "group_seconds": 600,
  "export_seconds": 60
}
```

The outer account (`builder` in this example) is distinct from the inner
guest account (`tester`). Paths are explicit; relative storage paths resolve
against the configuration file. The generated image currently contains tester;
select the same account when consuming it. A fresh official-image rebuild can
configure a different account. UID, network MAC, UUID, SSH port and sockets are
discovered/generated, never copied from a fleet. Optional `ssh_port`, `ssh_key`,
`qemu` and `firmware` select a loopback port, explicit private client identity,
QEMU executable or BIOS firmware. Otherwise the launcher generates a private
client key and selects a free loopback port. Never put a password in config.

From the repository root inside the execution VM:

```sh
export TMPDIR="$HOME/aerial-work/tmp"
mkdir -p "$TMPDIR"
python3 Tests/vm/test_platform.py
python3 Tests/vm/vm_platform.py build-image --config "$HOME/aerial-work/image.json" \
  --lock Tests/vm/image.lock.json --output-dir "$HOME/aerial-work/vm/image"
pushd "$HOME/aerial-work/vm/image"
sha256sum -c SHA256SUMS
popd
```

Use another configuration for the test instance (for example instance
`aerial-ci-niri-smoke`, with the same storage parent). Then:

```sh
image="$HOME/aerial-work/vm/image/aerial-ci-arch-x86_64.qcow2"
expected=$(awk '$2 == "aerial-ci-arch-x86_64.qcow2" { print $1 }' "$HOME/aerial-work/vm/image/SHA256SUMS")
python3 Tests/vm/vm_platform.py run-group --config "$HOME/aerial-work/test.json" \
  --image "$image" --sha256 "$expected" --source "$PWD" \
  --group niri-smoke --results "$HOME/aerial-work/results/niri-smoke"
```

An output/instance directory must not already exist. On failure inspect and
preserve it; do not replay cloud-init over an interrupted preparation disk.
A new reviewed attempt uses a different storage/output directory. Local tools
retain owned disks and receipts for inspection. The CI job disposes only its
own ephemeral guest after sanitized export, even if a failed locker survives;
failure stays failure and recovery is never enabled.

## Provenance and trust

The builder verifies the official Arch cloud-image checksum before boot,
uses one dated Arch archive snapshot with a complete package/repository lock,
and builds the vendored unmodified AUR recipes with pinned source checksums.
The wtype v0.4 source/checksum and TMPDIR-only keymap patch match the existing
container helper. Licenses remain installed with the dependencies.
The image uses QEMU's RTC; its online NTP boot wait is masked so restricted
test networking cannot block cloud-init. Sway's scheduler capability is removed
only in the disposable test guest before unprivileged process/socket inspection,
as in the existing headless container smoke.

Cloud-init writes a readiness marker to this QEMU process's private serial
log only after its account and SSH modules. That marker binds actual DMI UUID,
NIC MACs, boot/machine identity and public SSH host key to the launch receipt.
The launcher verifies UUID/MAC and uses that out-of-band key for strict pinned
SSH, then checks guest DMI/account again. No keyscan, accept-new, SSH agent,
ambient SSH config or signing key is used. QMP verifies KVM and requests
shutdown; QEMU exit, not a reply from a disappearing guest agent, confirms it.
There is no TCG fallback. Test networking uses restricted QEMU user networking
and a loopback-only SSH forward; preparation alone enables downloads.

The normal build refuses unresolved or changed pins. To deliberately prepare
a new lock, edit the input pins in a reviewed branch and run `build-image`
with `--resolve-lock` in the preparation VM. This writes a candidate
`resolved-lock.json` beside its output, never edits the input lock. Review
the complete package/source change, then commit the candidate and run a normal
locked rebuild. CI must never resolve/update pins automatically.

While provisioning replaces packages, progress/exit and failure-log observers
use unprivileged sh/coreutils reads, not Python or sudo/env. Exit records are
published by atomic rename and strictly parsed on the host. Python-based
cloud-init and inventory checks run before or after the package transaction,
never as its progress poll.

The provisioning status observer alone tolerates exact pre-auth SSH reset or
connection-refused diagnostics (exit 255, no remote output) while the package
transaction can restart sshd. A remote-start marker distinguishes remote exit
255 from these failures. SSH calls and poll delays consume the same absolute
provisioning deadline; an unavailable transport never means completion.
Host-key/authentication errors, unknown SSH failures, command timeouts, remote
exits and malformed status remain hard failures. All SSH uses the original
serial-pinned key; no new key is accepted and no general SSH retry is added.
`PROVISION_OBSERVATION` and `provision-observation.json` record polls,
transport retries/recoveries (including zero) and the outcome. The successful
build also retains the boot's `sshd-journal.log` for restart correlation.

Outputs are the standalone compressed qcow2, SHA256SUMS and
image-manifest.json (input, lock and tool hashes, sizes and preparation receipt).
Rebuildable means the same locked software/provisioning, not identical random
machine IDs or filesystem timestamps. Published checksums identify exact bytes.
HTTPS plus the official pinned hash is not a detached-signature proof;
the manifest reports whether a signature was independently verified.
`disk-usage.json` records one-second sampled filesystem growth/minimum free space
during image construction. It is a measured storage observation, not a quota.

## Results and publication

Compact results include JSON, JUnit and compressed filtered diagnostic text,
with per-file evidence hashes. PASS requires the exact expected consumer
outcomes and source/image/instance identity. Missing, timed-out, BLOCKED,
malformed or stale results are not passes. Credentials are generated only in
the guest, sent on stdin, restored and removed, and checked before export.
Ordinary diagnostic artifacts contain no disks, seed, keys, shadow backup,
raw input protocol, video or build archive. Never upload the whole workspace.

### Opt-in publication

The manual workflow always rebuilds and exercises its image. Its boolean
`publish` input defaults to false. Only `publish=true` on `master`, after the
rebuild and niri smoke succeed, stages that exact image for a separate publish
job. Both the job condition and publisher code refuse non-master refs. Never
dispatch a branch with publish=true. Build/smoke permissions remain contents:
read; contents: write and GH_TOKEN exist only in the publish job/step.

The master publishing path uses one explicit, one-day GitHub transfer artifact
containing the qcow2, SHA256SUMS, image.lock.json, image-manifest.json and
publication.json. This is the only image-artifact exception; it needs about
2 GiB of temporary Actions storage headroom. No image passes through Ark.
The receiving job rechecks every checksum and the tested source/image/lock
identities before any GitHub write. Ordinary runs do not stage this artifact.
The published image.lock.json contains the exact tested resolved-lock.json
bytes; its parsed content must match the input lock even if formatting differs.
The publish job downloads the producer's recorded artifact name, so a partial
re-run still selects the retained successful producer attempt.

The publisher reserves an unused tag at the tested source commit, named
`ci-image-YYYYMMDD` using the UTC date. Existing tags or releases cause `-1`,
`-2`, etc. suffixes; a concurrent reservation selects another unused name.
It creates a prerelease with Latest explicitly disabled, separate from product
versions. It never moves a tag or overwrites an asset. A failed publication
may leave its reserved tag/draft for inspection; a later attempt uses a new
name, not a destructive retry. Each release asset stays below 2 GiB.
The short release note records source, image/lock hashes and the proof-run URL.

### Consume a published image without rebuilding

Inside your authorized disposable execution VM (including a local Ark fork),
start from the repository root and choose an exact reviewed release tag:

```sh
(
set -eu
export TMPDIR="$HOME/aerial-work/tmp"
mkdir -p "$TMPDIR"
source="$PWD"
tag=ci-image-YYYYMMDD # Replace with the exact approved tag, including its suffix.
directory="$HOME/aerial-work/vm/$tag"
mkdir -p "$directory"
cd "$directory"
for asset in aerial-ci-arch-x86_64.qcow2 SHA256SUMS image.lock.json image-manifest.json publication.json; do
  curl --fail --location "https://github.com/atmosphera-dev/aerial-lock/releases/download/$tag/$asset" -o "$asset" || exit
done
sha256sum -c SHA256SUMS &&
expected=$(awk '$2 == "aerial-ci-arch-x86_64.qcow2" { print $1; matches++ } END { if (matches != 1) exit 1 }' SHA256SUMS) &&
python3 "$source/Tests/vm/vm_platform.py" run-group \
  --config "$HOME/aerial-work/test.json" --image "$directory/aerial-ci-arch-x86_64.qcow2" \
  --sha256 "$expected" --source "$source" --group niri-smoke \
  --results "$HOME/aerial-work/results/$tag-niri-smoke"
)
```

The subshell stops on a failed download, checksum or digest extraction without
changing the invoking shell's options. The smoke command runs only after all
verification succeeds.

The test configuration is the same explicit fresh-instance configuration
documented above. Review publication.json/image.lock.json for the source and
software identity. Obtain the expected digest from the approved release
descriptor/checksum file, not by hashing a downloaded image and treating its
own hash as authentication. Never use a moving Latest URL. A future workflow
can replace its rebuild step with this exact-tag/digest download and retain
the existing run-group command. This workflow does not yet offer prebuilt-image
inputs; unavailable images must fail, not silently fall back to another image.

Both workflow jobs use `blacksmith-8vcpu-ubuntu-2404` (8 vCPU, 32 GB RAM,
160 GB disk). Blacksmith documents nested KVM on x64 Linux, not ARM, and
unlimited runner concurrency in its [runner documentation](https://docs.blacksmith.sh/blacksmith-runners/overview).
The workflow still checks `/dev/kvm` and acquires its ACL at each use.
It uses one nested VM at a time with two vCPUs and 4096 MiB RAM, unchanged
explicit deadlines, three-day compact artifacts and no image cache.
GitHub's artifact storage and workflow limits still apply; Blacksmith's runner
capacity does not increase the repository's GitHub artifact storage quota.
Opt-in master publication additionally needs the one-day image transfer quota
above; release assets are durable and separate from Actions artifact storage.
Measure before claiming it fits. The user selected Blacksmith on 2026-11-18
after the shared Arch CI image builds ran out of memory on GitHub runners.

## Master acceptance and split/merge

`VM acceptance` is a separate manual workflow, not a replacement for PR Checks
or `VM niri smoke`. Dispatch the candidate branch/commit with `purpose=on-demand`
for ordinary acceptance, or `purpose=before-release` **before creating its tag**.
Require the final `merge` job to succeed before the release. Both choices run
the same complete matrix; there is no partial/reduced release tier.
Before the new workflow is registered on master, dispatch the existing
`VM niri smoke` workflow on the candidate branch with `group=acceptance`,
`publish=false` and the desired purpose. It calls the exact same acceptance
workflow and skips its smoke/image-publication producer; no PR or temporary
push trigger is needed to prove the branch.

The plan freezes four disjoint niri group slices and 12 stable cell IDs:

| # | Group | Cells | Consumer outcomes |
|---|---|---|---|
| 1 | `smoke` | 1 | Real PAM unlock, natural exit and desktop key witness |
| 2 | `pam` | 1 | Wrong/right PAM with retained lock, then unlock and desktop input |
| 3 | `emergency-recovery` | 2 | Missing runner and three confirmed normal crashes, real PAM, no sampled desktop exposure |
| 4 | `emergency-failures` | 8 | Installed-supervisor fault cases, authenticated recovery or explicitly observed protection/exhaustion |

All CI groups use nested one-output niri under headless Sway/pixman and Mesa
software OpenGL. This is not native DRM, multi-output, power, Hyprland, physical
display, IME or soak proof. There is no QEMU graphics bridge or render-node
dependency. Native niri rejects software EGL devices, and Hyprland's allocator
also needs a GPU render node; neither is assumed on GPU-less Blacksmith runners.

M1-M9 belong to the parked IME experiment and are intentionally not imported.
Companion acquisition/playback is added only after its driver merges into master;
it cannot be inferred from plugin installation or borrowed from an open branch.

### Frozen identities and exact coverage

`Tests/vm/acceptance.py plan` writes the complete deterministic matrix before
execution. It binds source revision/content, image lock and provisioning/tool
identity, workflow run and attempt. Each slice rebuilds the same locked image
specification independently. Random machine identities and filesystem timestamps
make image bytes differ across rebuilds: the merge requires common specification
identity and validates the **exact per-slice image hash** against that slice's
VM request/receipt, never treats independent builds as byte-identical.
Lock comparison uses canonical parsed content; the image manifest separately
binds the exact resolved-lock file bytes, so formatting cannot masquerade as
package drift or bypass checksum validation.

No image artifact is uploaded or fanned out. Each job uploads JSON, JUnit and
compressed sanitized diagnostics only, with evidence hashes and three-day
retention. The unconditional final merge requires every expected slice once,
every cell once, a distinct instance per slice, matching source/run/attempt,
validated end-of-chain consumer fields, correct image/request identity and clean
guest disposal. It rehashes downloaded evidence before accepting it. Missing,
failed, BLOCKED, cancelled, unavailable, stale or corrupt slices fail the combined
JSON/JUnit/Markdown result. A failed matrix job cannot be hidden by PASS artifacts.
Reruns preserve attempt identity; never merge artifacts from different attempts.

### Runtime and reproduction

Blacksmith runs all four slices in parallel (at most eight jobs). Each nested guest gets two
vCPUs and 4096 MiB. Each slice has a 150-minute job deadline, including a
55-minute image step and 65-minute boot/product-build/group/export step; group
execution itself is bounded to 20 minutes. The expected full wall time is
45-90 minutes, to be checked against the measured combined summary. The job
deadline stays below six hours with cleanup/upload margin. Independent image
builds trade runner time for avoiding multi-GiB Actions artifact storage.

The guest parent applies the group deadline to an isolated adapter process,
including blocked cleanup waits. Timeout preserves progress/logs and leaves
surviving lockers to owned-VM disposal; it never kills their process group.

For local reproduction **inside an authorized disposable execution VM** with
nested KVM and at least 12 GiB outer memory, use disk-backed paths. Build the
locked image as above, then from the source root:

```sh
export TMPDIR="$HOME/aerial-work/tmp"
python3 Tests/vm/test_platform.py
python3 Tests/vm/test_acceptance.py
python3 Tests/vm/acceptance.py local --workspace "$HOME/aerial-work" --source "$PWD" \
  --config "$HOME/aerial-work/test.json" \
  --image "$HOME/aerial-work/vm/image/aerial-ci-arch-x86_64.qcow2" \
  --checksums "$HOME/aerial-work/vm/image/SHA256SUMS"
```

`local` makes one fresh guest per slice, preserves failed evidence and updates
one interim combined result after every slice. It exits successfully only for
the complete valid matrix. Its unique run directory holds `plan.json`, `slices/`
and `merged/{summary.json,junit.xml,summary.md}`. Do not replay an interrupted
instance or accept an interim incomplete summary as full acceptance.

### Manual pre-release acceptance on Ark

Before releasing, also run the GPU-dependent groups in fresh, owned GPU-backed
Ark VMs using the `mpvqml-vm` / `ark-user-vms` route. Use a disk-backed workspace,
TMPDIR inside it, an operator-produced owned manifest and throwaway real-PAM
fixtures as described in [Tests/README.md](../README.md#real-session-integration-drivers).
Do not use a shared base or the host desktop. The CI result does not cover these
groups, and a successful CI merge does not replace their manual receipts.

| # | Manual lane | Required acceptance |
|---|---|---|
| 1 | Native two-output niri | `auth-handoff-vm.py --ime none`, remediation `panel-zero-outputs`, emergency `outputs` and power-actions VM groups |
| 2 | Hyprland | Smoke/PAM, emergency recovery/failures, password/output handoff, panel failover and power-actions VM groups |

Existing entry points, run **inside the owned Ark guest**, with `$ROOT` and
`$VM_RECORD` prepared using the real-session driver instructions:

```sh
export TMPDIR="$ROOT/tmp"
python3 Tests/auth-handoff-vm.py --root "$ROOT" --vm-record "$VM_RECORD" --ime none
bash Tests/run-remediation.sh --root "$ROOT" --vm-record "$VM_RECORD" --lane debug \
  --backend niri-drm --case panel-zero-outputs --cycles 1
python3 Tests/emergency-integration.py --root "$ROOT" --vm-record "$VM_RECORD" \
  --backend niri-drm --case outputs --outputs 2
python3 Tests/emergency-integration.py --root "$ROOT" --vm-record "$VM_RECORD" \
  --backend hyprland --case recovery --outputs 2 --hyprland-virtual-outputs
python3 Tests/emergency-integration.py --root "$ROOT" --vm-record "$VM_RECORD" \
  --backend hyprland --case failures --outputs 2 --hyprland-virtual-outputs
bash Tests/run-remediation.sh --root "$ROOT" --vm-record "$VM_RECORD" --lane debug \
  --backend hyprland --case panel-failover --outputs 3 --hyprland-virtual-outputs --cycles 1
```

Power acceptance requires the fixture preparation and launch from the active
graphical login1 subject documented in [Tests/README.md](../README.md).
Hyprland password/output handoff remains a manual session exercise; the existing
`auth-handoff-vm.py` only automates native niri. Retain exact compositor versions,
outcomes and limitations with each release's manual report. Hyprland's mapped
FALLBACK is not a true zero-output state; its known crash-replacement pixel
exposure is not a zero-exposure PASS. Adding these lanes to CI requires a
GPU-capable runner or a proven virgl path; no bridge experiment is part of this tier.
