#!/usr/bin/env python3
"""Run an exhaustive, disjoint portion of the shared companion Qt probe."""
import argparse
import re
import subprocess


def suite_for(function):
    if function.startswith("serviceCli") or function == "compatibilityFrontend":
        return "control"
    if function.startswith("policy"):
        return "policy"
    return "transfer"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", choices=("transfer", "policy", "control"))
    parser.add_argument("probe")
    parser.add_argument("decoder")
    args = parser.parse_args()
    result = subprocess.run([args.probe, args.decoder, "-functions"],
                            capture_output=True, text=True, timeout=10, check=True)
    functions = []
    for line in result.stdout.splitlines():
        match = re.fullmatch(r"([A-Za-z_]\w*)\(\)", line)
        if not match:
            raise RuntimeError("Unrecognized Qt test function: " + line)
        functions.append(match[1])
    assert functions and len(functions) == len(set(functions)), "Missing or repeated Qt test functions"
    selected = [function for function in functions if suite_for(function) == args.suite]
    assert selected, "Empty companion suite: " + args.suite
    print("COMPANION_SUITE " + args.suite + " " + " ".join(selected), flush=True)
    return subprocess.run([args.probe, args.decoder, *selected], timeout=300).returncode


if __name__ == "__main__":
    raise SystemExit(main())
