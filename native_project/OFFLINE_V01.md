# Android offline V0.1

This branch adds the C++ embedded backend and Java GM window used by the locally modified APK alongside the existing Python server. The native base was adapted from PackageInstaller/X2Eclipse_Simulator, revision `7c4eeef6f6f40f14fb84470aababfdd9295fd6fd`; its MIT license is included. Existing Python server files are unchanged.

## Skin purchase repairs

- Expose an explicit skin-coupon grant in the GM resource page using the original currency item 1237923.
- Keep original crystal/coupon prices, remove unsupported real-money alternatives and payment SDK identifiers, and make offline listings permanent.
- Keep price and currency arrays aligned, reject unsupported currencies without silently charging another wallet, and treat zero-quantity inventory entries as unowned.
- Validate owned heroes, appearance ownership, hero mapping, model path and requested wear type before saving a skin. On login, clear invalid saved appearance selections to the default.

## Existing offline improvements

Native GM V0.1 with two pages, independent developer profile and first-page save import/export through the Android system document picker. Import validates format and integrity, checks profile type, backs up the previous account and requires a restart. Terminal wish tasks, letters, moments, gifts and wish pool repairs are included.

## Validation and remaining issue

526 ARM64 regression checks passed on Sony G8441 / XZ1C running custom Android 14, using tables extracted from the supplied modified APK. Checks include coupon purchases, failed-purchase isolation, duplicate purchase rejection, skin ownership/hero/type checks, offline listing fields, SQLite reload, terminal operations, save import/export and chapter DP query isolation.

The repaired APK was installed and chapter 3 section 2110201 was physically entered through the map, party selection, opening dialogue and playable battle scene. Chapter entry was blocked by the missing C2L_GameTask type 7 handler: the client queries the preceding chapter's DP before opening its map. The reply now contains the chapter ID, task stages, actual DP and total capacity. Normal profiles reconstruct only provable hard-stage clears and current item/relic ownership; kill, NPC, shopping and run-quality DP events are not yet ported. Developer profiles explicitly bypass DP gates for the previously requested story-skipping tests; normal profiles do not receive this override. Chapter reward boxes remain outside this change.

The existing APK's coupon grant/purchase path was physically checked: grant 1000 coupons, purchase one 560-coupon skin, remaining 440, crystals unchanged. The V0.1 Android document picker exported a valid X2SAVE-V1 file, and the import picker opened successfully; no user save was overwritten in the picker test.

The XZ1C's transparent character with colored horizontal lines remains reproducible on a cold start with Unity P1–P11 disabled. All 106 playable appearance model bundles exist. Both the standard and experimental single-thread renderer recover after a real background/foreground cycle, so the single-thread patch is not established as the cause of recovery. It remains optional and disabled in the packaged APK. Java additionally attempts a one-time Unity pause/resume, surface and focus refresh on G8441 / Android 14+ after login, but this has not eliminated the cold-start defect and must not be described as a verified rendering fix. Main-hall cold-start rendering remains an open issue; the chapter 3 battle character and map rendered normally.

## Build inputs

Do not commit APKs, proprietary game tables/bundles, user saves, signing keys, NDKs or generated binaries. This addition records the source from the Windows repair workspace, whose scripts currently expect the workspace layout and Java/signing tools in the sibling `X2-TEST/build_tools` directory. Prepare the supplied original modified APK, extracted offline library, Android API JAR, NDK r27d and Frida Gum ARM64 development kit before using those scripts. The upstream CMake/build.sh workflow is also preserved. A clean checkout by itself does not contain the proprietary inputs required to build a runnable APK.

The candidate render settings patch is tied to globalgamemanagers SHA256 `3fa92efa91d5a1f3b5b73a1421168f048e9ba0b69d1a671c773f60bdeb589591`; an unknown asset is rejected rather than patched by offset.
