# Test input on Wayland

Run native GUI verification only in the project's authorized graphical test VM.
The hosted CI lane below covers build, QML lint and headless units only. Ordinary
QTest key/pointer helpers exercise in-process semantics; they do not generate
compositor input or a Wayland grab serial. A separate QQuickWindow alone is not
proof of an anchored native popup. Qt 6.12 rejects a grabbing popup without the
required native parent/input state.

## Hosted build, lint and headless units

`.github/workflows/ci.yml` runs on pushes and pull requests to `main`, or manual
dispatch, in an Ubuntu 24.04 hosted VM using a digest-pinned official Arch Linux
base-devel container. Dependencies follow Arch's rolling repositories: a full
`pacman -Syu --noconfirm --needed git cmake ninja qt6-base qt6-declarative
qt6-shadertools qt6-svg` avoids partial upgrades. Qt must satisfy the project's
existing minimum of 6.12.0; hosted CI does not assert an exact rolling version.
Package, compiler, CMake and Ninja versions are recorded with the logs.

Inside that hosted VM's workspace, CI runs:

```sh
export TMPDIR="$GITHUB_WORKSPACE/build/tmp"
mkdir -p "$TMPDIR" build/ci-logs
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DINTERACTIVE_BUILD_PLAYGROUND=ON -DINTERACTIVE_BUILD_TESTS=ON
cmake --build build --parallel 2
cmake --build build --target all_qmllint --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure \
  --no-tests=error -R '^interactive\.(color-edit-state|appearance|trace)$'
```

The anchored selection runs exactly `interactive.color-edit-state`,
`interactive.appearance` and `interactive.trace`. Appearance uses unshown
QQuickWindows and trace uses QGuiApplication; neither needs native exposure or
input. `interactive.playground-calibration` is excluded because it launches the
real Playground. No existing test is disabled or weakened.

Configure, build, QML lint and unit logs plus CTest's `LastTest.log` are uploaded
even on failure; logged pipelines preserve failing exit statuses. Build outputs
are not archived. All temporary files use the workspace's disk-backed `build/tmp`,
created before package operations and recreated after checkout.

This lane is not native GUI acceptance. Wayland input, exposed-window/popup
readiness and Playground runtime outcomes remain in `interactive-dev` on
`ark.home`. Hosted success neither resolves nor waives existing crash/readiness
findings. The native input setup and outcome requirements below remain unchanged.

## Input-host ownership

`TestProbe.inputHostReady(item)` requires an exposed, active host equal to
`QGuiApplication::focusWindow()`. Exposure, rendering and `QWindow::isActive()`
alone are insufficient: a transient ancestor can report active while a different
window owns input. `TestProbe.activateForInput(item)` requests window activation
when needed and waits for this exact ownership before reporting success. It does
not select or repair an item's focus. The lower-level `TestProbe.activate(item)`
retains its request-only contract for pointer flows; its receipt is not input
readiness.

The shared playground edit helper establishes input-host ownership before its
real click, then still requires the field's actual `activeFocus` and text outcome.
This applies to initial opens, reopens and returning to an owner after closing
a popup. Pointer-only menu clicks retain their separate path: a native menu can
legitimately keep its parent's keyboard ownership. `TestProbe.click()` reports
QTest dispatch only, not activation, native pointer input or a focus outcome.
The native Space boundary uses the same ownership predicate plus Wayland checks.

## Native Space boundary

`TestProbe.nativeKeyClick(hostItem, Qt.Key_Space)` is test-only. Its caller first
waits for `nativeKeyHostReady`: an exposed, active Wayland host equal to
`QGuiApplication::focusWindow()`. It invokes fixed `/usr/bin/ydotool key 57:1
57:0` once, using only the explicitly selected private socket and a minimal
environment. It never substitutes a QTest event, retries, primes a keymap or
changes the popup type.

An application-wide observer requires exactly one spontaneous, non-repeating
Space press followed by release at the intended QWindow, native scan code 65
and keysym 0x20, plus successful process exit, within one 5-second deadline.
Other native keys, duplicate/out-of-order events and wrong receivers fail.
`IA_NATIVE_KEY` logs include native metadata, receiver, event timestamp, process
receipt and failure reason. Process exit zero alone is not success. The observer
does not consume events or access Qt private state.

On failure, an unfinished owned client is killed/reaped with a bounded cleanup
wait. Only an actually observed held Space may receive one release-only cleanup
command; it is logged and never changes failure to success. Cleanup has a
separate bounded wait and never retries the press. If release cannot be
confirmed, stop the owned keyboard backend before continuing.

`tst_popups.qml` uses this boundary for shared Wayland Window openings, including
reopens and dialogs. The playground Window-menu transaction opener uses the same
boundary. Other platforms and Item rows retain their explicit QTest path. All
existing outcome/hosting assertions and other pointer/key checks remain.
The popup and playground CTest suites are `RUN_SERIAL` because native focus and
the seat are shared.

## Isolated guest backend

Prepare this explicitly inside the authorized VM, never on a physical host.
Tests do not install packages, run sudo, start a daemon or modify permissions.

| # | Requirement | Setup/verification |
|---|---|---|
| 1 | Existing system Qt lane and graphical session | Use matching Qt libraries, plugins and QML imports, and disk-backed `TMPDIR` for every command. |
| 2 | ydotool with keyboard-only daemon support | Inspect the installed version/help; require `--mouse-off`, `--socket-path` and `--socket-perm`. No mouse or touch device. |
| 3 | Temporary uinput access | Save `getfacl -p /dev/uinput`; grant only the test account an ACL for this task. No group/udev rule or mode 0666. |
| 4 | Private backend | New `build/native-input/`, owned by the test account, mode 0700. Tester-owned transient user unit runs `ydotoold --mouse-off --socket-path=<absolute build/native-input/ydotool.sock> --socket-perm=0600`. |
| 5 | Observed readiness | libinput inspection must identify `ydotoold virtual device` on the intended seat with keyboard capability and no pointer/touch capability. Verify the socket is owned by the test account and mode 0600. Do not send a priming key or use a fixed readiness sleep. |
| 6 | Backend lifetime | Record the original linger setting; enable only if required to survive SSH disconnects. Track the exact owned user unit. |
| 7 | Explicit test selection | Export `IA_TEST_YDOTOOL_SOCKET` as that absolute socket path into the sanitized test environment. Missing/invalid path or ownership/permissions fails before any key dispatch. |
| 8 | Teardown | After the last required native-input run, stop the owned unit, restore the saved uinput ACL and original linger setting, verify device removal/no owned clients, then shut down an idle guest. |

For native-role auditing, use `WAYLAND_DEBUG=client` in the same VM. Preserve the
native key press/release and `get_popup`/`xdg_popup.grab` serial chain, including
nested popups; no extra toplevel fallback is acceptable. Retain actual process
exits and complete QtTest totals. A failed proof stops for root-cause review,
not another input transport or retries until green.
