import os
import subprocess
import sys


EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


def main() -> int:
    files = [f for f in sys.argv[1:] if os.path.splitext(f)[1].lower() in EXTS]
    if not files:
        return 0

    for path in files:
        subprocess.check_call(["clang-format", "-i", path])

    diff = subprocess.check_output(["git", "diff", "--name-only", "--"] + files, text=True)
    if diff.strip():
        print("clang-format updated files. Please stage the changes.")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
