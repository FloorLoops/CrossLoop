"""Link verified experimental Xtensa archives into this isolated local build."""
from pathlib import Path
import hashlib
import json

Import("env")

if env.subst("$PIOENV") != "x4pro-native-probe":
    raise RuntimeError("Native browser probe is restricted to its local X4 environment")

native = Path("D:/crossloop-netsurf-native-20261114")
receipt = json.loads((native / "reader-link-inputs-lto.json").read_text(encoding="utf-8"))
if Path(receipt["reader_root"]).resolve() != Path(env.subst("$PROJECT_DIR")).resolve():
    raise RuntimeError("Native browser archives were built for a different reader checkout")
for item in receipt["interface_headers"]:
    path = Path(item["path"])
    if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise RuntimeError(f"Native browser interface changed: {path}; rebuild and verify archives first")
for item in receipt["archives"]:
    path = Path(item["path"])
    if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise RuntimeError(f"Native browser archive changed: {path.name}; refresh verification first")
env.Append(CPPPATH=receipt["include_dirs"])
env.Append(LIBS=[env.File(item["path"]) for item in receipt["archives"]])
