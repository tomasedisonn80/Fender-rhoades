#!/usr/bin/env bash
set -euo pipefail
readonly probe_root=/home/agent/rhoades-9a04c9726e31/w3-052
readonly repo_root=/dev/shm/rhoades-9a04c9726e31/Fender-rhoades
if [[ -e "$probe_root/native.started" ]]; then
  printf 'Refusing a second probe.\n' >&2
  exit 2
fi
source /home/agent/rhoades-9a04c9726e31/gui-env.sh
export TMPDIR="$probe_root/tmp"
export XDG_CACHE_HOME="$probe_root/xdg-cache"
export FONTCONFIG_FILE="$probe_root/fonts.conf"
ulimit -c 0
cd "$probe_root"
native_command=(
  "$(command -v python3)"
  "$repo_root/tools/run_xvfb_test.py"
  --xvfb "$(command -v Xvfb)"
  --xauth "$(command -v xauth)"
  --xdpyinfo "$(command -v xdpyinfo)"
  --startup-timeout 10
  --test-timeout 30
  --
  "$probe_root/snapshot/rhodes_vst3_photo_lifecycle_tests"
  "$probe_root/snapshot/RhodesResearch.vst3"
  remove_begin
)
printf '%q ' "${native_command[@]}" > "$probe_root/native-command.sh"
printf '\n' >> "$probe_root/native-command.sh"
date -u +%Y-%m-%dT%H:%M:%SZ > "$probe_root/native.started"
set +e
"${native_command[@]}" > "$probe_root/native.stdout" 2> "$probe_root/native.stderr"
native_status=$?
set -e
printf '%s\n' "$native_status" > "$probe_root/native.exit"
date -u +%Y-%m-%dT%H:%M:%SZ > "$probe_root/native.finished"
exit "$native_status"
