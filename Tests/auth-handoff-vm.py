#!/usr/bin/env python3
"""Native niri/PAM writer-handoff acceptance in an operator-owned test VM.

Requires the standard owned manifest and normal pam_unix fixture. Run once
without an input-method daemon and once with the guest's native fcitx5 route.
No passwords or password-derived editor diagnostics are persisted.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("driver", Path(__file__).with_name("remediation-driver.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


def outcome(locker, after):
    hit = locker.wait_for(lambda line: "lock-state: auth " in line and any(label in line for label in
        (" -> success ", " -> failed ", " -> prompting reason=pam-prompt", " -> error ")),
        20, after=after)
    if hit is None:
        raise RuntimeError("no bounded authentication outcome")
    return hit[1].strip()


def finalize_cycle(session, result, password):
    try:
        if password is not None and (session.output / "locker.log").exists():
            if password.decode() in (session.output / "locker.log").read_text():
                result.update(status="FAIL", error="fixture response leaked into locker.log")
    except Exception:
        result.update(status="FAIL", error="credential log check failed")
    finally:
        try:
            session.cleanup()
        except Exception as error:
            result.update(status="FAIL", cleanup_error=str(error))
            raise
        finally:
            (session.output / "handoff.json").write_text(json.dumps(result, indent=2) + "\n")


def cycle(args, case):
    session = driver.Session(SimpleNamespace(root=args.root, vm_record=args.vm_record,
        backend="niri-drm", outputs=2, lane="debug", case="auth-handoff-" + args.ime + "-" + case,
        recover_owned_failures=False, pointer_helper=None))
    result = dict(case=case, ime=args.ime, status="INCOMPLETE")
    password = None
    try:
        session.start()
        session.config(media=False)
        # Preserve the real default theme and observe native focus through a
        # test-only component wrapper, not sleeps or compositor IPC completion.
        session.shell = session.output / "observed-shell"
        shutil.copytree(session.stage / "share/aerial-lock", session.shell)
        shutil.copyfile(Path(__file__).with_name("auth-handoff-theme.qml"),
                        session.shell / "Themes/AuthHandoffProbe.qml")
        config_path = session.output / "config/aerial-lock/config.json"
        config = json.loads(config_path.read_text())
        config["theme"] = "AuthHandoffProbe.qml"
        config_path.write_text(json.dumps(config))
        names = sorted(session.drm_original_outputs)
        assert len(names) == 2, "exactly two native outputs required"
        session.start_pointer(names[0])
        if case == "theme-file-edit":
            theme = session.output / "config/aerial-lock/themes/AuthHandoffProbe.qml"
            theme.parent.mkdir()
            content = (session.shell / "Themes/AuthHandoffProbe.qml").read_text()
            content = content.replace('"../Services"', '"root:/Services"')
            content = content.replace('"../Themes"', '"root:/Themes"')
            theme.write_text(content)
        if case.startswith("zero-start"):
            session.pointer_command("unmap", b"VIRTUAL_POINTER_UNMAPPED")
            for name in names:
                session.drm_output(name, "off")
            session.wait_drm_outputs(set())
        supervisor = session.stage / "bin/aerial-lock-supervisor"
        session.owned.check_binary(supervisor)
        env = dict(session.env, WAYLAND_DEBUG="1", AERIAL_LOCK_SHELL=str(session.shell),
            AERIAL_LOCK_RUNNER=str(session.stage / "libexec/aerial-lock/aerial-lock-run"),
            AERIAL_LOCK_PLUGIN_ROOT=str(session.stage / "lib/aerial-lock/qt-plugins"))
        session.locker = driver.ObservedProcess([str(supervisor)], env, session.output / "locker.log")
        locker = session.locker
        locker.wait_line(lambda line: "lock requested -> engaged reason=compositor-confirmed" in line)
        locker.wait_line(lambda line: "ext_session_lock_v1" in line and ".locked()" in line)

        def keys(value, enter=False):
            session.assert_control()
            key_env = session.env.copy()
            key_env.pop("WAYLAND_DEBUG", None)
            if value:
                driver.run(["wtype", "-d", "1", "-"], key_env, input=value,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            stamp = time.monotonic_ns()
            if enter:
                driver.run(["wtype", "-k", "Return"], key_env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return stamp

        def zero_and_return(flap, parked_after=0):
            assert locker.wait_for(lambda line: "HANDOFF_INPUT_PARKED" in line, after=parked_after) is not None
            stamp = time.monotonic_ns()
            session.drm_output(names[0], "on")
            session.wait_drm_outputs({names[0]})
            session.pointer_command("map " + names[0], b"VIRTUAL_POINTER_MAPPED")
            assert locker.wait_for(lambda line: "HANDOFF_INPUT_READY" in line, after=stamp) is not None
            if flap:
                parked = time.monotonic_ns()
                session.drm_output(names[0], "off")
                session.wait_drm_outputs(set())
                assert locker.wait_for(lambda line: "HANDOFF_INPUT_PARKED" in line, after=parked) is not None
                stamp = time.monotonic_ns()
                session.drm_output(names[0], "on")
                session.wait_drm_outputs({names[0]})
                session.pointer_command("map " + names[0], b"VIRTUAL_POINTER_MAPPED")
                assert locker.wait_for(lambda line: "HANDOFF_INPUT_READY" in line, after=stamp) is not None

        if case.startswith("zero-start"):
            # The actual candidate must be nominated while there is no surface.
            locker.wait_line(lambda line: "HANDOFF_THEME_NOMINATED" in line)
            zero_and_return(case != "zero-start-return")
        if case == "theme-file-edit":
            locker.wait_line(lambda line: "HANDOFF_THEME_NOMINATED" in line)
        fixture = session.owned.auth_fixture()
        password = Path(fixture["secret_file"]).read_bytes().removesuffix(b"\n")
        assert password and len(password) > 1
        if case == "zero-start-wrong-right":
            wrong = outcome(locker, keys(b"deliberately-invalid-fixture-response", enter=True))
            assert " -> failed reason=pam-failed" in wrong, wrong
            assert locker.process.poll() is None
            result["wrong_rejected"] = True
        if case in ("remove-focused-typing", "zero-during-typing", "flap-during-typing", "theme-file-edit"):
            split = len(password) // 2
            keys(password[:split])
            if case == "remove-focused-typing":
                ready = time.monotonic_ns()
                session.drm_output(names[0], "off")
                session.wait_drm_outputs({names[1]})
                session.pointer_command("map " + names[1], b"VIRTUAL_POINTER_MAPPED")
                assert locker.wait_for(lambda line: "HANDOFF_INPUT_READY" in line, after=ready) is not None
            elif case in ("zero-during-typing", "flap-during-typing"):
                parked = time.monotonic_ns()
                session.pointer_command("unmap", b"VIRTUAL_POINTER_UNMAPPED")
                for name in names:
                    session.drm_output(name, "off")
                session.wait_drm_outputs(set())
                zero_and_return(case == "flap-during-typing", parked_after=parked)
            else:
                with theme.open("a") as stream:
                    stream.write("\n// Edited during lock: file reload must remain disabled.\n")
            stamp = keys(password[split:], enter=True)
        else:
            if case == "waited":
                time.sleep(3)
            stamp = keys(password, enter=True)
        final = outcome(locker, stamp)
        assert " -> success reason=pam-success" in final, final
        locker.wait_line(lambda line: ".unlock_and_destroy()" in line)
        locker.wait_line(lambda line: " -> released reason=compositor-unlocked" in line)
        assert locker.finish(20) == 0
        log = locker.log.read_text()
        assert password.decode() not in log, "fixture response leaked"
        assert log.count("lock idle -> requested reason=startup") == 1, "lock unexpectedly reloaded"
        assert "reason=external-release" not in log
        assert " -> prompting reason=pam-prompt" not in log, "unanswered extra password prompt"
        session.locker = None
        session.desktop()
        result.update(status="PASS", first_correct_submit=True, draft_preserved=True,
                      natural_exit=0, desktop_input=True, external_release=False)
    except Exception as error:
        result.update(status="FAIL", error=str(error))
        if session.locker and session.locker.process.poll() is None:
            # Explicit cleanup through the owned supervisor, never a killed qs
            # or a retry counted as success. The failed result stays failed.
            session.locker.process.send_signal(signal.SIGUSR1)
            session.locker.finish(20)
            result["cleanup_external_release"] = True
        session.locker = None
    finally:
        finalize_cycle(session, result, password)
    print(json.dumps(dict(result=result, evidence=str(session.output))), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--vm-record", required=True)
    parser.add_argument("--ime", required=True, choices=("none", "fcitx5"))
    args = parser.parse_args()
    assert os.environ.get("TMPDIR") == str(Path(args.root) / "tmp")
    record = json.loads(Path(args.vm_record).read_text())
    os.environ.update(record["environment"])
    os.environ["PATH"] = str(Path(args.root) / "tools/bin") + ":" + os.environ["PATH"]
    live_ime = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", "fcitx5"],
                              stdout=subprocess.DEVNULL, timeout=5).returncode == 0
    assert live_ime == (args.ime == "fcitx5"), "input-method mode does not match actual daemon"
    cases = ("zero-start-return", "zero-start-flap", "zero-start-wrong-right", "immediate", "waited",
             "remove-focused-typing", "zero-during-typing", "flap-during-typing", "theme-file-edit")
    results = [cycle(args, case) for case in cases]
    return 0 if all(result["status"] == "PASS" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
