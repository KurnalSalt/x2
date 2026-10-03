"""Build and sign an APK using the workspace NDK and the sibling Java tools."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
TOOLS = WORKSPACE.parent / 'X2-TEST/build_tools'

for script in ('build_compat.py', 'build_windows.py', 'build_gm_android.py', 'package_fixed.py'):
    subprocess.run([sys.executable, str(ROOT / 'tools' / script)], cwd=str(WORKSPACE), check=True)

subprocess.run([str(TOOLS / 'jdk-21/bin/java.exe'), '-jar', str(TOOLS / 'uber-apk-signer.jar'),
                '--apks', str(WORKSPACE / 'output/x2_fixed_unsigned.apk'),
                '--out', str(WORKSPACE / 'output/release_signed'),
                '--allowResign', '--skipZipAlign'], check=True)
print('Signed APK: ' + str(WORKSPACE / 'output/release_signed/x2_fixed_unsigned-debugSigned.apk'))
