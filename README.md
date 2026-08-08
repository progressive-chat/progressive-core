# progressive-core

Canonical **Qt-free Matrix core** — E2EE (Olm/Megolm), `/sync`, and the CS API —
shared by all progressive frontends:

- [progressive-desktop](https://github.com/MaurerAnton/progressive-desktop) — Qt6 desktop + PineTab 2
- [progressive-cli](https://github.com/progressive-chat/progressive-cli) — C++20 CLI/TUI + REST API

## Why this repo exists

Logic (encryption, sync, key sharing, media) is developed and tested **headlessly
in the core first** — every bug gets a scenario test here — and the frontends
only wire UI to the same core. No more duplicate cores drifting apart.

## Layout

```
src/core/        the canonical core (matrix_client, sync_engine, decryptor, …)
native/          vendored progressive_native (libolm-based modules, pre-patched)
native_shim/     android/log.h shim (progressive_compat.h) for Linux builds
tests/           Qt-free unit + integration tests (incl. live-Synapse E2EE)
scripts/         audit_modules.py (native module classification A/B/C/D)
cmake/           progressive_native.cmake (FetchContent: libolm, simdjson, sqlite)
```

## Build & test

```bash
cmake -S . -B build -G Ninja
cmake --build build -j4
ctest --test-dir build
```

Unity mode (3-5x faster cold rebuilds; used by CI):
`cmake -S . -B build -G Ninja -DPROGRESSIVE_CORE_UNITY=ON`

Dependencies: libcurl, OpenSSL, libsodium, SQLite3 (libolm + simdjson are
fetched by CMake; SQLite amalgamation is downloaded at configure time).

## Prebuilt artifacts

CI builds + tests every `main` push (unity) for x86_64 and aarch64 (hosted
ARM runners) and publishes the two static libraries as a per-commit release
`core-<sha>` (`progressive-core-<arch>.tar.gz`). Consumers set
`PROGRESSIVE_CORE_USE_ARTIFACT=ON` to download the matching archive instead
of compiling the core (automatic source-build fallback while CI catches up).
`scripts/unity_exclude.py` (run at configure time) keeps files that cannot
merge into unity groups as standalone TUs.

## Refreshing `native/`

`native/` is a snapshot of the `progressive-android-experiments` submodule
(`progressive/src/main/cpp`) with `third_party/patches/*` applied and the
JNI/TLS glue excluded. To refresh:

```bash
# in a checkout of progressive-desktop (patches applied at configure time):
rsync -a third_party/progressive-android-experiments/progressive/src/main/cpp/src/ <core>/native/src/
rsync -a third_party/progressive-android-experiments/progressive/src/main/cpp/include/ <core>/native/include/
rm -f <core>/native/src/{jni_bridge,jni_stubs_part*,tls_bridge,decryptor_utils}.cpp
```

Keep the desktop and the CLI consuming this repo via submodule/FetchContent —
never copy `src/core` again.
