#!/usr/bin/env bash
# Open the project in UE 5.5 and run Scripts/Checkpoints/build_cpXX.py.
# Usage: Scripts/run_checkpoint.sh 01      (override engine with UE_ROOT=/path/to/UE_5.5)
set -euo pipefail
CP="${1:?usage: run_checkpoint.sh <01..05>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$ROOT/HaGiangGTA.uproject"
SCRIPT="$ROOT/Scripts/Checkpoints/build_cp${CP}.py"
[ -f "$SCRIPT" ] || { echo "No such script: $SCRIPT" >&2; exit 1; }

case "$(uname -s)" in
  Darwin)
    UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.5}"
    EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" ;;
  *)
    UE_ROOT="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.5}"
    EDITOR="$UE_ROOT/Engine/Binaries/Win64/UnrealEditor.exe" ;;
esac
[ -x "$EDITOR" ] || { echo "UnrealEditor not found at $EDITOR (set UE_ROOT)" >&2; exit 1; }

exec "$EDITOR" "$PROJECT" -ExecutePythonScript="$SCRIPT"
