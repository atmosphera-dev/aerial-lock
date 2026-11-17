#!/usr/bin/env python3
"""Installed-lock/real-login1 acceptance in an operator-recorded disposable VM.

Run from the recorded active graphical session (e.g. compositor spawn), not
an SSH/root subject. Terminal actions publish durable pre-dispatch records;
their OS outcomes must be attested by the UUID-bound outer lifecycle operator.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import sys
import time
from types import SimpleNamespace

from fixture_files import publish
from owned_instance import OwnedInstance

spec = importlib.util.spec_from_file_location("remediation", Path(__file__).with_name("remediation-driver.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

THEMES = ("Windows7", "MaterialYou", "Enfield", "Clockwork")
CAPABILITIES = ("canSuspend", "canHibernate", "canReboot", "canPowerOff")
ACTION_CAPABILITY = dict(zip(("suspend", "hibernate", "reboot", "powerOff"), CAPABILITIES))
ENDPOINT = ["org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager"]


def call(method):
    return subprocess.check_output(["busctl", "call", *ENDPOINT, method], text=True, timeout=10).strip()


class Acceptance:
    def __init__(self, args):
        backend = "niri-drm" if args.backend == "niri" else "hyprland"
        self.s = driver.Session(SimpleNamespace(root=args.root, vm_record=args.vm_record,
            backend=backend, case="power-" + args.case, outputs=1, pointer_helper=None,
            recover_owned_failures=False))
        self.args = args
        self.sequence = 0
        self.cycle = 0
        self.records = []
        self.s.env["PATH"] = str(self.s.root / "tools/bin") + os.pathsep + self.s.env["PATH"]
        self.s.outer_env["PATH"] = self.s.env["PATH"]
        # This test has no desktop-portal workload. Avoid registering the same
        # application connection twice on the minimal guest's portal service.
        self.s.env["QT_NO_XDG_DESKTOP_PORTAL"] = "1"
        self.s.outer_env["QT_NO_XDG_DESKTOP_PORTAL"] = "1"
        if not (self.s.root / "tools/bin/wtype").is_file():
            raise RuntimeError("workspace-local TMPDIR-only wtype required")
        # login1 identifies the process session, not its display environment.
        session = subprocess.check_output(["loginctl", "show-session", "self", "-p", "Id", "-p", "Active",
            "-p", "Remote", "-p", "Seat"], text=True, timeout=10)
        if "Active=yes" not in session or "Remote=no" not in session or "Seat=seat0" not in session:
            raise RuntimeError("launch the driver from the owned active graphical session, not SSH")
        (self.s.output / "logind-subject.txt").write_text(session)
        self.s.start()
        if args.backend == "niri":
            driver.run(["niri", "msg", "action", "focus-monitor", self.s.drm_original_outputs[0]],
                       self.s.env, stdout=subprocess.DEVNULL)
        self.monitor = driver.ObservedProcess(["sudo", "busctl", "monitor", "org.freedesktop.login1"],
            self.s.env, self.s.output / "login1.log")
        self.monitor.wait_line(lambda line: "Monitoring bus message stream" in line)
        self.agent_monitor = driver.ObservedProcess(["sudo", "busctl", "monitor",
            "--match=type='method_call',interface='org.freedesktop.PolicyKit1.AuthenticationAgent',member='BeginAuthentication'"],
            self.s.env, self.s.output / "authentication-agent.log")
        self.agent_monitor.wait_line(lambda line: "Monitoring bus message stream" in line)

    def record(self, cell, **values):
        row = dict(cell=cell, **values)
        self.records.append(row)
        publish(self.s.output / "acceptance.json", json.dumps(self.records, indent=2))
        print(json.dumps(row), flush=True)

    def state(self, predicate=lambda state: True, after=None, seconds=15):
        if after is None:
            after = time.monotonic_ns()
        result = None
        def match(line):
            nonlocal result
            if "POWER_VM_STATE " not in line:
                return False
            state = json.loads(line.split("POWER_VM_STATE ", 1)[1])
            if predicate(state):
                result = state
                return True
            return False
        if not self.s.locker.wait_for(match, seconds, after=after, newest_first=True):
            raise RuntimeError("power consumer state NOT_EXERCISED; see " + str(self.s.output))
        return result

    def command(self, action, **arguments):
        self.sequence += 1
        publish(self.command_path, json.dumps(dict(sequence=self.sequence, action=action, **arguments)))
        if action == "stop":
            self.s.locker.wait_line(lambda line: "POWER_VM_OBSERVATION_STOPPED" in line)
        else:
            self.state(lambda state: state["sequence"] == self.sequence)

    def retained(self):
        # The complete flushed log, not the bounded observer buffer.
        text = self.s.locker.log.read_text()
        assert self.s.locker.process.poll() is None, "locker exited during power interaction"
        assert ".locked()" in text
        assert not re.search(r"unlock_and_destroy| -> unlock-requested| -> released|reason=pam-success", text), \
            "power interaction released the secure lock"
        return text

    def dispatches(self):
        text = self.monitor.log.read_text()
        return re.findall(r"Member=(Suspend|Hibernate|Reboot|PowerOff)WithFlags", text)

    def launch(self, theme="Windows7", enabled=True):
        self.cycle += 1
        self.sequence = 0
        self.command_path = self.s.output / f"command-{self.cycle}.json"
        publish(self.command_path, json.dumps(dict(sequence=0, action="")))
        self.s.config(media=False)
        config = self.s.output / "config/aerial-lock/config.json"
        (config.parent / "i18n").mkdir(exist_ok=True)
        (config.parent / "i18n/en.json").write_text("{}")
        config.write_text(json.dumps(dict(sources=[], preferredResolution=1080,
            theme="PowerVmProbe.qml", powerActions=dict(enabled=enabled))))
        self.s.shell = self.s.output / f"shell-{self.cycle}"
        shutil.copytree(self.s.stage / "share/aerial-lock", self.s.shell)
        shutil.copyfile(Path(__file__).with_name("power-actions-vm-theme.qml"),
                        self.s.shell / "Themes/PowerVmProbe.qml")
        self.s.env.update(POWER_VM_THEME=theme, POWER_VM_COMMAND=str(self.command_path))
        self.s.launch(self.cycle)
        self.state(lambda state: state["theme"] == theme and state["attached"])

    def unlock(self):
        self.retained()
        self.command("cancel")
        self.command("stop")
        driver.run(["wtype", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"],
            self.s.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        locker = self.s.locker
        self.s.type_and_submit()
        locker.wait_line(lambda line: ".unlock_and_destroy()" in line, 20)
        assert locker.finish(20) == 0, "independent PAM cleanup did not exit naturally"
        self.s.locker = None
        if self.args.backend == "niri":
            self.s.desktop()
        text = locker.log.read_text()
        warnings = [line for line in text.splitlines() if re.search(r"\b(?:WARN|ERROR|TypeError|ReferenceError)\b", line)
                    and "power-action:" not in line and not (self.args.case == "outputs" and any(
                        line.endswith(f'aerial-lock: focused output "{removed}" has no usable lock surface; using {survivor}')
                        for removed in self.s.drm_original_outputs for survivor in self.s.drm_original_outputs
                        if removed != survivor))]
        assert not warnings, "unexpected locker warning: " + repr(warnings)
        assert text.count("POWER_VM_THEME_CREATED") == text.count("POWER_VM_THEME_DESTROYED") == 1

    def presentation(self, name):
        # Secure-state evidence is consumer data/protocol logs. Final visual
        # evidence is rendered separately in normal VM windows with scripted
        # context, never described as a screenshot of the live lock.
        self.record(name, state=self.state())

    def themes(self):
        for theme in (*THEMES, "Macintosh"):
            self.launch(theme)
            baseline = self.state(lambda state: state["canReboot"] and state["canPowerOff"])
            self.retained()
            if theme in THEMES:
                self.presentation(theme + "-normal")
                before = len(self.dispatches())
                self.command("reboot")
                def settled_confirmation(state):
                    controls = [item for item in state["items"] if item["name"] in
                                ("powerCancel", "powerConfirm", "powerMessage")]
                    return state["confirming"] and len(controls) == 3 and any(
                        item["name"] == "powerCancel" and item["focused"] for item in controls) and all(
                        item["width"] > 0 and item["height"] > 0 and item["x"] >= -.75 and item["y"] >= -.75
                        and item["x"] + item["width"] <= state["width"] + .75
                        and item["y"] + item["height"] <= state["height"] + .75 for item in controls)
                confirmation = self.state(settled_confirmation)
                controls = [item for item in confirmation["items"] if item["name"] in
                            ("powerCancel", "powerConfirm", "powerMessage")]
                assert len(controls) == 3
                for item in controls:
                    assert item["width"] > 0 and item["height"] > 0
                    assert item["x"] >= -.75 and item["y"] >= -.75
                    assert item["x"] + item["width"] <= confirmation["width"] + .75
                    assert item["y"] + item["height"] <= confirmation["height"] + .75
                self.presentation(theme + "-confirm")
                self.command("cancel")
                self.state(lambda state: not state["confirming"] and not state["pending"])
                assert len(self.dispatches()) == before
            self.record("cold-load-" + theme, state=baseline, lock_retained=True)
            self.unlock()
        self.launch(enabled=False)
        hidden = self.state(lambda state: not state["enabled"])
        assert not any(hidden[key] for key in CAPABILITIES)
        self.command("reboot")
        self.state(lambda state: not state["confirming"] and not state["pending"])
        self.record("config-disabled", state=hidden)
        self.unlock()

    def inhibitor(self, what, mode):
        process = subprocess.Popen(["systemd-inhibit", "--what=" + what, "--mode=" + mode,
            "--who=Firefox", "--why=Downloading file", "sh", "-c", "printf 'READY\\n'; read -r line"],
            env=self.s.env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert select.select([process.stdout], [], [], 5)[0], "inhibitor startup timeout"
        assert process.stdout.readline() == b"READY\n", process.stderr.read().decode()
        return process

    @staticmethod
    def release_inhibitor(process):
        process.stdin.write(b"done\n")
        process.stdin.close()
        assert process.wait(timeout=5) == 0

    def inhibited(self):
        self.launch()
        self.state(lambda state: state["canReboot"])
        for what, mode in (("shutdown", "block"), ("shutdown", "block-weak"), ("sleep", "block"),
                           ("sleep", "block-weak")):
            # Open confirmation first so a changed displayed capability cannot
            # turn this into a mere unavailable-control test.
            action = "reboot" if what == "shutdown" else "suspend"
            if action == "reboot":
                self.command(action)
                self.state(lambda state: state["confirming"])
            process = self.inhibitor(what, mode)
            try:
                manager = call("ListInhibitors")
                before = len(self.dispatches())
                self.command("confirm" if action == "reboot" else action)
                state = self.state(lambda state: bool(state["error"]) and state["who"] == "Firefox")
                assert state["who"] == "Firefox" and state["why"] == "Downloading file"
                assert state["error"] == ("Restart" if action == "reboot" else "Suspend") + \
                    " is blocked by Firefox: Downloading file"
                assert any(item["name"] == "powerMessage" and item["text"] == state["error"]
                    and item["format"] == 0 for item in state["items"]), "inhibitor prose not rendered literally"
                assert len(self.dispatches()) == before
                self.retained()
                self.presentation(what + "-" + mode)
                self.record(what + "-" + mode, state=state, inhibitors=manager, dispatches=0, lock_retained=True)
                self.command("dismiss")
            finally:
                self.release_inhibitor(process)
            self.state(lambda state: state[ACTION_CAPABILITY[action]])
        self.unlock()

    def policy(self):
        self.launch()
        self.state(lambda state: state["canReboot"])
        helper = ["sudo", "python3", str(Path(__file__).with_name("power-actions-vm-fixtures.py")),
                  "--root", str(self.s.root), "--vm-record", str(self.s.owned.path)]
        for policy in ("challenge", "no"):
            try:
                driver.run([*helper, "policy", policy], self.s.env)
                sessions = json.loads(subprocess.check_output(["loginctl", "list-sessions", "--json=short"],
                    text=True, timeout=10))
                other = [session for session in sessions if session["user"] == "aerial-power-other"
                         and session["class"] == "user"]
                assert other, "throwaway other-user login1 session not registered"
                state = self.state(lambda state: not state["canReboot"] and not state["canPowerOff"])
                assert state["rebootReason"] == ("This action needs additional authorization and is unavailable while locked."
                    if policy == "challenge" else "The system did not allow this action.")
                before = len(self.dispatches())
                for action in ACTION_CAPABILITY:
                    self.command(action)
                final = self.state()
                assert not final["pending"] and not final["confirming"]
                assert len(self.dispatches()) == before
                self.retained()
                assert "Member=BeginAuthentication" not in self.agent_monitor.log.read_text()
                self.record("policy-" + policy, state=state, other_user_sessions=other,
                            lock_retained=True, dispatches=0, authentication_agent_prompts=0)
            finally:
                driver.run([*helper, "policy", "restore"], self.s.env)
            self.state(lambda state: state["canReboot"] and state["canPowerOff"])
        self.unlock()

    def error(self):
        self.launch()
        self.state(lambda state: state["canReboot"])
        self.command("reboot")
        self.state(lambda state: state["confirming"])
        # Preserve the exact prior target state and restore only our runtime mask.
        target = Path("/run/systemd/system/reboot.target")
        assert not target.exists() and not target.is_symlink(), "pre-existing runtime target override"
        before = len(self.dispatches())
        driver.run(["sudo", "systemctl", "mask", "--runtime", "reboot.target"], self.s.env)
        try:
            self.command("confirm")
            state = self.state(lambda state: bool(state["error"]))
            self.retained()
            sent = len(self.dispatches()) - before
            self.presentation("real-dispatch-error")
            self.record("error", status="PASS" if sent == 1 else "UNAVAILABLE", state=state,
                        dispatches=sent, lock_retained=True)
            if sent != 1:
                raise RuntimeError("mask caused capability refusal, not required real dispatch error")
        finally:
            driver.run(["sudo", "systemctl", "unmask", "--runtime", "reboot.target"], self.s.env)
            assert not target.exists() and not target.is_symlink()
        self.unlock()

    def outputs(self):
        assert self.args.backend == "niri", "native niri outputs required"
        self.launch()
        baseline = self.state(lambda state: state["canReboot"])
        driver.run(["wtype", "-",], self.s.env, input=b"power-vm-draft", stdout=subprocess.DEVNULL)
        saved = self.state(lambda state: state["syntheticDraft"])
        self.command("reboot")
        self.state(lambda state: state["confirming"])
        names = sorted(self.s.drm_original_outputs)
        other = next(name for name in names if name != baseline["output"])
        self.s.start_pointer(other)
        self.s.pointer_command("click", b"VIRTUAL_POINTER_CLICKED")
        driver.run(["wtype", "-k", "Return", "-k", "a"], self.s.env, stdout=subprocess.DEVNULL)
        self.state(lambda state: state["confirming"] and state["syntheticDraft"])
        self.s.drm_output(baseline["output"], "off")
        self.s.wait_drm_outputs({other})
        self.state(lambda state: state["output"] == other and state["confirming"] and state["syntheticDraft"])
        self.s.drm_output(other, "off")
        self.s.wait_drm_outputs(set())
        parked = self.state(lambda state: not state["attached"] and state["confirming"] and state["syntheticDraft"])
        self.s.drm_output(baseline["output"], "on")
        self.s.wait_drm_outputs({baseline["output"]})
        self.s.pointer_command("map " + baseline["output"], b"VIRTUAL_POINTER_MAPPED")
        recovered = self.state(lambda state: state["attached"] and state["confirming"] and state["syntheticDraft"])
        assert all(recovered[key] == saved[key] == parked[key] for key in ("cursor", "anchor", "revision"))
        assert not self.dispatches()
        self.retained()
        self.record("outputs", saved=saved, parked=parked, recovered=recovered, dispatches=0, lock_retained=True)
        self.s.restore_drm_outputs()
        blocker = self.inhibitor("shutdown", "block")
        try:
            self.command("confirm")
            failed = self.state(lambda state: state["who"] == "Firefox" and bool(state["error"]))
            self.s.drm_output(recovered["output"], "off")
            survivor = next(name for name in names if name != recovered["output"])
            self.s.wait_drm_outputs({survivor})
            self.s.pointer_command("map " + survivor, b"VIRTUAL_POINTER_MAPPED")
            moved = self.state(lambda state: state["output"] == survivor and state["error"] == failed["error"]
                               and state["syntheticDraft"])
            assert not self.dispatches()
            self.record("error-output-migration", before=failed, after=moved, dispatches=0, lock_retained=True)
        finally:
            self.release_inhibitor(blocker)
            self.s.restore_drm_outputs()
        self.unlock()

    def supported(self, action):
        self.launch("MaterialYou" if action == "suspend" else "Windows7")
        state = self.state(lambda state: state["canReboot"])
        capability = call("Can" + {"suspend": "Suspend", "hibernate": "Hibernate", "reboot": "Reboot",
                                  "powerOff": "PowerOff"}[action])
        if not state[ACTION_CAPABILITY[action]]:
            self.record(action, status="UNAVAILABLE", capability=capability, state=state)
            self.unlock()
            return "UNAVAILABLE"
        if action in ("reboot", "powerOff"):
            self.command(action)
            self.state(lambda state: state["confirming"])
            self.command("cancel")
            self.state(lambda state: not state["pending"])
            assert not self.dispatches()
            self.retained()
        driver.run(["wtype", "-"], self.s.env, input=b"power-vm-draft", stdout=subprocess.DEVNULL)
        self.state(lambda state: state["syntheticDraft"])
        if action in ("reboot", "powerOff"):
            self.command(action)
            self.state(lambda state: state["confirming"])
            self.retained()
        self.record(action + "-before-dispatch", status="ARMED", boot_id=self.s.owned.record["guest"]["boot_id"],
            uuid=self.s.owned.record["uuid"], capability=capability, locker_pid=self.s.locker.process.pid,
            log=str(self.s.locker.log), lock_retained=True)
        os.sync()
        delay = self.inhibitor("sleep", "delay") if action == "suspend" else None
        self.command("confirm" if action in ("reboot", "powerOff") else action)
        if delay:
            try:
                delayed = self.state(lambda state: state["state"] == "waiting" and not state["error"])
                self.retained()
                self.record("delay-inhibitor", state=delayed, inhibitors=call("ListInhibitors"),
                            dispatches=len(self.dispatches()), lock_retained=True)
            finally:
                self.release_inhibitor(delay)
        # Sleep action sends on its first selection, with no confirmation.
        if action in ("suspend", "hibernate"):
            resumed = self.state(lambda state: state["state"] == "idle" and not state["pending"]
                                 and not state["error"] and state["syntheticDraft"], seconds=120)
            self.retained()
            assert Path("/proc/sys/kernel/random/boot_id").read_text().strip() == self.s.owned.record["guest"]["boot_id"]
            self.record(action + "-resumed", state=resumed, lock_retained=True,
                        note="requires outer PM-state attestation")
            self.unlock()
        else:
            accepted = self.state(lambda state: state["state"] == "waiting" or bool(state["error"]))
            if accepted["error"]:
                self.record(action + "-dispatch-error", state=accepted, lock_retained=True)
                raise RuntimeError("real terminal action was not accepted")
            # No success claim: the OS may terminate us; only the host attests it.
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                self.retained()
                time.sleep(.1)
            self.s.blocked("terminal OS outcome NOT_EXERCISED within 90 seconds; do not unlock a pending action")

    def finish(self):
        # Independent ordinary PAM cleanup never converts failed acceptance to PASS.
        try:
            if self.s.locker and self.s.locker.process.poll() is None:
                self.unlock()
        finally:
            if not self.s.locker or self.s.locker.process.poll() is not None:
                for observer in (self.monitor, self.agent_monitor):
                    if observer.process.poll() is None:
                        observer.process.terminate()
                    observer.finish(5)
                self.s.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--vm-record", required=True)
    parser.add_argument("--backend", choices=("niri", "hyprland"), required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--case", choices=("controller", "themes", "outputs", "policy", "inhibited", "error",
                        "supported-suspend", "supported-hibernate", "supported-reboot", "supported-poweroff"))
    args = parser.parse_args()
    if args.prepare:
        owned = OwnedInstance(args.vm_record, args.root)
        owned.check(require_binaries=True)
        owned.check_session(owned.record["session"], os.environ)
        owned.auth_fixture()
        print("PREPARED " + owned.record["domain"])
        return 0
    if not args.case:
        parser.error("--case or --prepare required")
    acceptance = Acceptance(args)
    result = dict(case=args.case, status="INCOMPLETE", results=str(acceptance.s.output))
    try:
        if args.case == "controller":
            driver.run([sys.executable, str(Path(__file__).with_name("test-power-controller.py")),
                "--build", str(acceptance.s.owned.paths["build"])], acceptance.s.env)
        elif args.case.startswith("supported-"):
            action = args.case.removeprefix("supported-")
            status = acceptance.supported("powerOff" if action == "poweroff" else action)
            if status == "UNAVAILABLE":
                result["status"] = status
        else:
            getattr(acceptance, args.case)()
        if result["status"] == "INCOMPLETE":
            result["status"] = "PASS"
    except Exception as error:
        result.update(status="FAIL", error=repr(error))
        raise
    finally:
        publish(acceptance.s.output / "result.json", json.dumps(result, indent=2))
        try:
            acceptance.finish()
        except Exception as error:
            live = acceptance.s.locker and acceptance.s.locker.process.poll() is None
            result.update(status="BLOCKED" if live else "FAIL", cleanup_error=repr(error))
            publish(acceptance.s.output / "result.json", json.dumps(result, indent=2))
            if acceptance.s.locker and acceptance.s.locker.process.poll() is None:
                acceptance.s.blocked("power acceptance failed and ordinary PAM cleanup did not finish")
            raise
        print(result["status"] + " " + args.case + " " + str(acceptance.s.output), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
