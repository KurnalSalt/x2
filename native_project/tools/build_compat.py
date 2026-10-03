"""Build the two-method offline analytics compatibility library for ARM64."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
compiler = ROOT.parent / 'toolchain/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
subprocess.run([str(compiler), '--target=aarch64-linux-android21', '-shared', '-fPIC', '-O2',
                '-Wl,-z,max-page-size=16384', '-Wl,-soname,libumeng-spy.so',
                str(ROOT / 'android/compat/umeng_spy.c'), '-o', str(ROOT / 'build/libumeng-spy.so')], check=True)
