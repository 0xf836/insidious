#!/usr/bin/env bash
set -euo pipefail
mkdir -p /results
printf 'Temporary cancellation acceptance probe: %s\n' "$1" > /results/build.log
sleep 900
