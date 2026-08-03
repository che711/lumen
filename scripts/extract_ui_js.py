#!/usr/bin/env python3
"""Достаёт <script> из data/www/index.html — чтобы прогнать его node'ом.

    python3 scripts/extract_ui_js.py /tmp/ui.js && node test/ui/uitest.js /tmp/ui.js
"""
import re
import sys
from pathlib import Path

src = Path(__file__).resolve().parent.parent / "data" / "www" / "index.html"
out = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/ui.js")

html = src.read_text(encoding="utf-8")
match = re.search(r"<script>\n(.*?)\n</script>", html, re.S)
if not match:
    sys.exit("В index.html не найден блок <script>")

out.write_text(match.group(1), encoding="utf-8")
print(f"{out}: {len(match.group(1))} байт")
