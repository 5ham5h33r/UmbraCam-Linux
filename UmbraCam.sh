#!/bin/sh
set -eu
SCRIPT=$(readlink -f -- "$0")
ROOT=$(CDPATH='' cd -- "$(dirname -- "$SCRIPT")" && pwd)
exec python3 "$ROOT/linux/umbracam.py" "$@"
