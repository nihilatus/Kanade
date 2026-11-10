# Fail when include/secrets.h is tracked. PlatformIO loads this as a pre script.
# CI can also run: python tools/check_secrets_untracked.py
import subprocess
import sys

env = None
try:
    Import("env")
except NameError:
    pass


def secrets_are_tracked():
    try:
        result = subprocess.run(
            ["git", "ls-files", "--", "include/secrets.h"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return False
    if result.returncode != 0:
        return False
    return result.stdout.strip() != ""


if secrets_are_tracked():
    sys.stderr.write("include/secrets.h is tracked by git. Remove it before building.\n")
    if env is not None:
        env.Exit(1)
    else:
        sys.exit(1)
