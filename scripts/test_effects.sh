#!/usr/bin/env bash
# Прогон движка эффектов на хосте, без ESP32.
set -euo pipefail
cd "$(dirname "$0")/.."

STUB=${TMPDIR:-/tmp}/lumen-ardstub
mkdir -p "$STUB"
cat > "$STUB/Arduino.h" <<'HDR'
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#ifndef PI
#define PI 3.14159265358979323846
#endif
HDR

g++ -std=c++17 -Wall -Wextra -I "$STUB" -I include -I src \
    src/led/effects.cpp test/effects/fx_smoke.cpp -o "${TMPDIR:-/tmp}/lumen_fx"
"${TMPDIR:-/tmp}/lumen_fx"
