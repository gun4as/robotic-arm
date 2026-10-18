# PlatformIO pirms-būves skripts: web/index.html -> gzip -> src/web_index.h
# Tā web lapa ir firmware iekšā, nav jāaugšupielādē atsevišķa failu sistēma.
import gzip
import os

Import("env")  # noqa: F821  (PlatformIO SCons vide)

project = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(project, "web", "index.html")
dst = os.path.join(project, "src", "web_index.h")

with open(src, "rb") as f:
    raw = f.read()
gz = gzip.compress(raw, compresslevel=9, mtime=0)

lines = [
    "// AUTOMĀTISKI ĢENERĒTS no web/index.html (tools/embed_web.py). Nelabo ar roku!",
    "#pragma once",
    "#include <Arduino.h>",
    f"static const size_t INDEX_HTML_GZ_LEN = {len(gz)};",
    "static const uint8_t INDEX_HTML_GZ[] PROGMEM = {",
]
for i in range(0, len(gz), 20):
    lines.append("  " + ",".join(str(b) for b in gz[i:i + 20]) + ",")
lines.append("};")
out = "\n".join(lines) + "\n"

old = None
if os.path.exists(dst):
    with open(dst, "r", encoding="utf-8") as f:
        old = f.read()
if old != out:
    with open(dst, "w", encoding="utf-8") as f:
        f.write(out)
    print(f"embed_web: {len(raw)} B -> {len(gz)} B gzip")
