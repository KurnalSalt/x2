"""Export readable changes against the pinned upstream native project."""
from pathlib import Path
import concurrent.futures
import difflib
import json
import urllib.request
ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
REV = json.loads((WORKSPACE / 'inspection/native_tree.json').read_text(encoding='utf-8'))['sha']
CACHE = WORKSPACE / 'inspection/native_baseline'
changed = ['src/offline/npc_social.cpp','src/offline/npc_social.hpp','CMakeLists.txt','src/server/protocol_ids.hpp','src/server/router.hpp','src/server/router.cpp',
           'src/offline/shop.hpp','src/offline/shop.cpp','src/offline/account_repository.cpp',
           'src/runtime/backend.cpp','src/android/entry.cpp','src/offline/mission.cpp','src/offline/cheat.cpp',
           'src/offline/mission.hpp','src/offline/account_state.hpp','src/offline/protocol_builders.hpp',
           'src/offline/protocol_builders.cpp','src/android/il2cpp.cpp']
added = ['src/server/gm.cpp','src/offline/jewel_compose.hpp','src/offline/jewel_compose.cpp','tests/regression.cpp','src/offline/playable_heroes.hpp']
added += ['src/offline/gift_packages.hpp','src/offline/gift_packages.cpp','src/offline/recommend_banners.hpp',
          'src/runtime/battle_control.hpp','src/android/combat_hooks.cpp','src/android/api21_compat.c']
added += ['src/android/unity_test_patches.cpp']
added += ['src/android/offline_recharge.cpp']
added += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'android').rglob('*.java')]
added += ['android/compat/umeng_spy.c']
added += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'tools').glob('*.py') if not p.stem.endswith('_edit')]
def baseline(path):
    target = CACHE / path
    if not target.exists():
        target.parent.mkdir(parents=True,exist_ok=True)
        data = urllib.request.urlopen('https://raw.githubusercontent.com/PackageInstaller/X2Eclipse_Simulator/'+REV+'/'+path,timeout=30).read()
        target.write_bytes(data)
    return path, target.read_text(encoding='utf-8').splitlines(keepends=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    old = dict(pool.map(baseline,changed))
patch = ''
for path in changed + added:
    before = old.get(path,[])
    after = (ROOT / path).read_text(encoding='utf-8').splitlines(keepends=True)
    patch += ''.join(difflib.unified_diff(before,after,fromfile='a/'+path if before else '/dev/null',tofile='b/'+path))+'\n'
(WORKSPACE / 'output/fixes.patch').write_text(patch,encoding='utf-8')
print('Patch exported against',REV)
