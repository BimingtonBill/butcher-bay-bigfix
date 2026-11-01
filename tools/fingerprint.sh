#!/bin/sh
# Read-only collector; Python 3.9+ is required.
set -eu
tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for python_cmd in python3 python; do
    if command -v "$python_cmd" >/dev/null 2>&1 &&
       "$python_cmd" -c 'import sys; sys.exit(sys.version_info < (3,9))' >/dev/null 2>&1; then
        exec "$python_cmd" "$tool_dir/fingerprint.py" "$@"
    fi
done
printf '%s\n' 'Fingerprint collection needs Python 3.9 or newer on PATH.' >&2
exit 1
