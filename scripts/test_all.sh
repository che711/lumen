#!/usr/bin/env bash
# Все тесты, которые можно прогнать без ESP32.
set -euo pipefail
cd "$(dirname "$0")/.."
TMP=${TMPDIR:-/tmp}

echo "=== ядро планировщика ==="
g++ -std=c++17 -Wall -Wextra -I lib/schedule lib/schedule/*.cpp \
    test/test_schedule/test_main.cpp -o "$TMP/lumen_sched"
"$TMP/lumen_sched"

echo
echo "=== движок эффектов ==="
scripts/test_effects.sh

echo
echo "=== интерфейс ==="
python3 scripts/extract_ui_js.py "$TMP/lumen_ui.js" >/dev/null
node test/ui/uitest.js "$TMP/lumen_ui.js"
