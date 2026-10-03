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

518 ARM64 regression checks passed on Sony G8441 / XZ1C running custom Android 14, using tables extracted from the supplied modified APK. Checks include coupon purchases, failed-purchase isolation, duplicate purchase rejection, skin ownership/hero/type checks, offline listing fields, SQLite reload, terminal operations and save import/export.

The XZ1C's transparent character with colored horizontal lines remains reproducible with Unity P1–P11 disabled. All 106 appearance rows for playable hero IDs 1003–1039 reference existing model bundles. An **experimental** `package_fixed.py --render-compat` candidate disables only `m_MTRendering` and `mobileMTRenderingBaked` in the exact shipped Unity globalgamemanagers asset (two checked bytes). This is disabled by default and is **not a verified rendering fix**. Installing the candidate was blocked by insufficient device storage. Do not claim this issue resolved until visual device testing succeeds.

## Build inputs

Do not commit APKs, proprietary game tables/bundles, user saves, signing keys, NDKs or generated binaries. This addition records the source from the Windows repair workspace, whose scripts currently expect the workspace layout and Java/signing tools in the sibling `X2-TEST/build_tools` directory. Prepare the supplied original modified APK, extracted offline library, Android API JAR, NDK r27d and Frida Gum ARM64 development kit before using those scripts. The upstream CMake/build.sh workflow is also preserved. A clean checkout by itself does not contain the proprietary inputs required to build a runnable APK.

The candidate render settings patch is tied to globalgamemanagers SHA256 `3fa92efa91d5a1f3b5b73a1421168f048e9ba0b69d1a671c773f60bdeb589591`; an unknown asset is rejected rather than patched by offset.
