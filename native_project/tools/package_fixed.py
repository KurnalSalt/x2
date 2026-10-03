"""Preserve the original APK, replace two repaired libraries, and add a native GM dex."""
import hashlib
import copy
import json
import shutil
import struct
import zipfile
import sys
from pathlib import Path
from binary_patches import patch_unity
from patch_wish_thumbnails import build as patch_thumbnails

# APK signing requires classic ZIP offsets. Python's conservative 2 GiB
# ZIP64 threshold is smaller than the format's unsigned 4 GiB offset range.
zipfile.ZIP64_LIMIT = (1 << 32) - 1

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
source = next(WORKSPACE.glob("*.apk"))
outdir = WORKSPACE / "output"
outdir.mkdir(exist_ok=True)
unsigned = outdir / "x2_fixed_unsigned.apk"
replacements = {"lib/arm64-v8a/libx2offline.so": ROOT / "build/libx2offline.so"}
replacements["lib/arm64-v8a/libumeng-spy.so"] = ROOT / "build/libumeng-spy.so"
if '--render-compat' in sys.argv:
    from render_compat import patch
    with zipfile.ZipFile(source) as original_apk:
        asset=ROOT/'build/globalgamemanagers.compat'
        asset.write_bytes(patch(original_apk.read('assets/bin/Data/globalgamemanagers')))
    replacements['assets/bin/Data/globalgamemanagers']=asset
with zipfile.ZipFile(source) as archive:
    il2cpp_original = ROOT / "build/libil2cpp.original.so"
    il2cpp_original.write_bytes(archive.read("lib/arm64-v8a/libil2cpp.so"))
    il2cpp_fixed = ROOT / "build/libil2cpp.so"
    patch_thumbnails(il2cpp_original, il2cpp_fixed)
    replacements["lib/arm64-v8a/libil2cpp.so"] = il2cpp_fixed
    original_unity = archive.read("lib/arm64-v8a/libunity.so")
    patched_unity, binary_patches = patch_unity(original_unity)
    unity_path = ROOT / "build/libunity.so"
    unity_path.write_bytes(patched_unity)
    replacements["lib/arm64-v8a/libunity.so"] = unity_path
    with zipfile.ZipFile(unsigned, "w", allowZip64=True) as destination:
        assert "classes5.dex" not in archive.namelist()
        destination.writestr("classes5.dex", (ROOT / "build/gm_dex/classes.dex").read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
        for info in archive.infolist():
            if info.filename.startswith("META-INF/"):
                continue
            if info.filename in replacements:
                destination.writestr(info, replacements[info.filename].read_bytes())
            else:
                # Copy original compressed entries byte-for-byte. Avoid spending
                # minutes recompressing 2 GB of unchanged Unity resources.
                assert not (info.flag_bits & 8), "unexpected ZIP data descriptor"
                archive.fp.seek(info.header_offset)
                local = archive.fp.read(30)
                assert local[:4] == b"PK\x03\x04"
                name_size, extra_size = struct.unpack_from("<HH", local, 26)
                entry = copy.copy(info)
                entry.header_offset = destination.fp.tell()
                destination.fp.write(local)
                remaining = name_size + extra_size + info.compress_size
                while remaining:
                    chunk = archive.fp.read(min(4 * 1024 * 1024, remaining))
                    if not chunk: raise ValueError("truncated original ZIP entry")
                    destination.fp.write(chunk)
                    remaining -= len(chunk)
                destination.filelist.append(entry)
                destination.NameToInfo[entry.filename] = entry
                destination.start_dir = destination.fp.tell()
                destination._didModify = True
with zipfile.ZipFile(source) as original, zipfile.ZipFile(unsigned) as fixed:
    changed = []
    for info in fixed.infolist():
        if info.filename == "classes5.dex":
            changed.append(info.filename)
            continue
        old = original.getinfo(info.filename)
        if (old.CRC, old.file_size) != (info.CRC, info.file_size):
            changed.append(info.filename)
    assert sorted(changed) == sorted([*replacements, "classes5.dex"])
    assert fixed.testzip() is None
manifest = {"source": source.name, "modified_entries": changed, "binary_patches": binary_patches,
            "libraries": {n: hashlib.sha256(p.read_bytes()).hexdigest() for n, p in replacements.items()}}
(outdir / "build_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(unsigned, "only", changed, flush=True)
