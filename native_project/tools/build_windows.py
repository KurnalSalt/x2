"""Build the ARM64 library and device regression tests with a workspace NDK."""
import concurrent.futures
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
NDK = WORKSPACE / "toolchain/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64"
BIN = NDK / "bin"
OUT = ROOT / "build"
OUT.mkdir(exist_ok=True)

def run(args):
    result = subprocess.run([str(x) for x in args], cwd=str(ROOT), capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(" ".join(str(x) for x in args) + "\n" + result.stdout + result.stderr)

def extract_blob():
    data = (WORKSPACE / "inspection/libx2offline.so").read_bytes()
    shoff = struct.unpack_from("<Q", data, 40)[0]
    size, count = struct.unpack_from("<HH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * size) for i in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 11:
            continue
        strings = sections[section[6]]
        strings = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, _, _, index, value, _ = struct.unpack_from("<IBBHQQ", data, offset)
            name = strings[name:strings.find(b"\0", name)].decode(errors="replace")
            if name in ("_binary_tables_x2data_start", "_binary_tables_x2data_end"):
                symbols[name] = value
    start, end = (symbols["_binary_tables_x2data_" + x] for x in ("start", "end"))
    section = next(s for s in sections if s[3] <= start < s[3] + s[5] and s[1] == 1)
    offset = section[4] + start - section[3]
    blob = data[offset:offset + end - start]
    assert blob.startswith(b"X2DATA1")
    (ROOT / "data").mkdir(exist_ok=True)
    (ROOT / "data/tables.x2data").write_bytes(blob)
    (OUT / "tables.x2data").write_bytes(blob)
    run([BIN / "llvm-objcopy.exe", "-I", "binary", "-O", "elf64-littleaarch64", "-B", "aarch64",
         OUT / "tables.x2data", OUT / "tables.o"])
    # llvm-objcopy derives symbol names from its input path. Use a relative basename.
    result = subprocess.run([str(BIN / "llvm-objcopy.exe"), "-I", "binary", "-O", "elf64-littleaarch64", "-B", "aarch64",
                             "tables.x2data", "tables.o"], cwd=str(OUT), capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))

extract_blob()
sources = re.findall(r"\$\{X2_ROOT\}/(src/[^\s)]+\.(?:cpp|c))", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
sources = list(dict.fromkeys(sources))
common = ["--target=aarch64-linux-android21", "-O2", "-g", "-fPIC", "-ffunction-sections", "-fdata-sections", "-Wno-format-security",
          "-I" + str(ROOT / "src"), "-I" + str(ROOT / "src/third_party/database/sqlite"),
          "-I" + str(ROOT / "src/third_party/database/sqlite_orm"), "-I" + str(ROOT / "src/third_party/frida-gum")]
header_time = max(p.stat().st_mtime for p in (ROOT / "src").rglob("*.hpp"))

def compile_one(source):
    src = ROOT / source
    obj = OUT / (source.replace("/", "_") + ".o")
    cpp = src.suffix == ".cpp"
    flags = ["-std=c++23"] if cpp else ["-w", "-DSQLITE_THREADSAFE=2", "-DSQLITE_OMIT_LOAD_EXTENSION", "-DSQLITE_DEFAULT_MEMSTATUS=0"]
    if not obj.exists() or obj.stat().st_mtime < max(src.stat().st_mtime, header_time, Path(__file__).stat().st_mtime):
        run([BIN / ("clang++.exe" if cpp else "clang.exe"), *common, *flags, "-c", src, "-o", obj])
    print("compiled", source, flush=True)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
    objects = list(executor.map(compile_one, sources))

libraries = [ROOT / "src/third_party/frida-gum/libfrida-gum.a", "-llog", "-ldl", "-lm", "-lz"]
link = [BIN / "clang++.exe", "--target=aarch64-linux-android21", "-static-libstdc++", "-Wl,-z,max-page-size=16384", "-Wl,--gc-sections"]
run([*link, "-shared", "-Wl,-soname,libx2offline.so", *objects, OUT / "tables.o", *libraries, "-o", OUT / "libx2offline.so"])
print("linked libx2offline.so", flush=True)
test_obj = compile_one("tests/regression.cpp")
test_persist = OUT / "test_persist.o"
# The test executable uses a database relative to its disposable working directory.
# The application library above retains its original Android private save path.
run([BIN / "clang++.exe", *common, "-std=c++23", "-U__ANDROID__", "-c",
     ROOT / "src/persist/account_db.cpp", "-o", test_persist])
test_objects = [o for s, o in zip(sources, objects) if not s.startswith("src/android/") and
                s not in ("src/runtime/backend.cpp", "src/persist/account_db.cpp")]
test_objects.append(test_persist)
run([*link, test_obj, *test_objects, *libraries, "-o", OUT / "x2_regression"])
print("linked x2_regression", flush=True)
