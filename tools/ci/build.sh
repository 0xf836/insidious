#!/usr/bin/env bash
set -euo pipefail
mkdir -p /results
printf 'Temporary hosted acceptance probe: %s\n' "$1" > /results/build.log
if [[ "$1" == arch ]]; then
    echo 'Intentional Arch job failure for aggregate acceptance' | tee -a /results/build.log
    exit 42
fi
