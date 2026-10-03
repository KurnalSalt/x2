"""Build the standalone native GM window into an additional APK dex file."""
from pathlib import Path
import subprocess
import zipfile
ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path('E:/Desktop/X2-TEST/build_tools')
OUT = ROOT / 'build/gm_java'
OUT.mkdir(exist_ok=True)
def run(args):
    subprocess.run([str(x) for x in args], check=True)
sources = list((ROOT / 'android').rglob('*.java'))
run([TOOLS / 'jdk-21/bin/javac.exe', '-encoding', 'UTF-8', '-source', '8', '-target', '8',
     '-classpath', ROOT.parent / 'toolchain/android-35.jar', '-d', OUT, *sources])
jar = ROOT / 'build/gm_window.jar'
with zipfile.ZipFile(jar, 'w') as archive:
    for source in OUT.rglob('*.class'): archive.write(source, source.relative_to(OUT).as_posix())
dex = ROOT / 'build/gm_dex'
dex.mkdir(exist_ok=True)
run([TOOLS / 'jdk-21/bin/java.exe', '-cp', TOOLS / 'd8.jar', 'com.android.tools.r8.D8', '--min-api', '21',
     '--lib', ROOT.parent / 'toolchain/android-35.jar', '--output', dex, jar])
print(dex / 'classes.dex')
