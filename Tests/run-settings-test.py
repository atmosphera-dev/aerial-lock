#!/usr/bin/env python3
"""Settings Qt probes: fail on warnings anywhere, not just the last test case."""
import os
import re
import subprocess
import sys
result = subprocess.run(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=os.environ)
text = result.stdout.decode(errors="replace")
print(text, end="")
sys.exit(result.returncode or int(bool(re.search(r"^QWARN\s*:|QQmlApplicationEngine failed|ReferenceError:|TypeError:", text, re.M))))
