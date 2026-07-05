# AGENTS.md

Living source of truth for coding agents working in this repo. Keep it
concise, accurate, and current — see "Keeping this file updated" at the
bottom; that instruction is not optional.

For user-facing feature docs, see [README.md](README.md). This file is
agent-oriented: things you need to know _before_ editing, that aren't
obvious from reading one file in isolation.

## What this project is

A static, client-side web app (React + TypeScript + Vite) that converts
Kodak Photo CD (`.pcd`) files to color-managed JPEG or TIFF entirely in the
browser via WebAssembly. No server, no upload — decode/encode both happen in
WASM compiled from C++.

## Architecture

```
native/             our C++ WASM bridge (all new code, not vendored):
  bridge.cpp          entry point exported to JS (pcd_convert)
  jpeg_writer.cpp/.h   libjpeg encode + ICC APP2 + Exif APP1 injection
  tiff_writer.cpp/.h   hand-rolled baseline-plus TIFF writer (no libtiff)
  image_metadata.cpp/.h  PCD metadata -> EXIF/TIFF tag mapping
  tiff_bytes.h         shared little-endian/IFD byte-packing helpers
  icc_srgb.h           embedded sRGB ICC profile (HP-licensed, see below)
vendor/pcdtojpeg/   git submodule: vendored PhotoCD decoder (pcdDecode.{h,cpp}).
                    GPLv2+. PRISTINE — see "Never edit vendor/" below.
scripts/
  build-wasm.mjs      emcc build (also invoked from vite.config.ts as a plugin)
  test-wasm.mjs       Node smoke test, see Testing below
src/
  App.tsx             the whole UI (single component)
  lib/pcdCodec.ts      JS <-> WASM bridge (Emscripten MEMFS in/out)
  lib/fileSave.ts      triggers a browser download for the converted file
                        (no folder-picker / File System Access API — removed;
                        every conversion is downloaded individually)
  wasm/               generated: pcd-codec.mjs / pcd-codec.wasm (gitignored,
                      rebuilt by build-wasm.mjs — don't hand-edit)
test/               sample .PCD files for manual/automated testing —
                    **gitignored**, not part of the repo (see Testing below)
```

## Build & commands

```sh
npm install
npm run dev          # rebuilds WASM if stale, then starts Vite
npm run build         # tsc -b && vite build (also rebuilds WASM if stale)
npm run build:wasm    # standalone WASM build; only rebuilds if native/vendor sources changed
npm run build:wasm -- --force   # force rebuild regardless of mtimes
npm run lint          # eslint .
npm run test:wasm     # Node smoke test against test/*.PCD (see Testing)
npm run preview       # vite preview, serves the production build
```

`npm run dev`/`build` auto-run `build-wasm.mjs` via a Vite plugin
([vite.config.ts](vite.config.ts)), comparing mtimes of the compiled
`.mjs`/`.wasm` against every file in `sources`/`headers` in
[scripts/build-wasm.mjs](scripts/build-wasm.mjs). **If you add a new native
`.cpp` file, add it to that `sources` array** or it silently won't compile.

Requires [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
(`emcc` on `PATH`) — pinned to version `5.0.7` in CI
(`.github/workflows/deploy-pages.yml`), which is the version this native code
was built/tested against.

## Never edit `vendor/`

The only PhotoCD decoding logic is the vendored `pcdtojpeg` library
(GPLv2+, by Sandy McGuffog), pinned as a git submodule at
`vendor/pcdtojpeg` (tag `v1.0.17` of the `signalwerk/pcdtojpeg` mirror — the
original SourceForge project only ships zip releases, no repo to submodule).
It's kept **pristine and unmodified** on purpose. If you need new behavior
that touches decoded data, add it in `native/` (the bridge layer) instead —
see `native/src/bridge.cpp`'s 64Base-clamping code for the pattern: work
around a vendor limitation from the caller side rather than patching the
vendor source.

This also means: the whole repo is GPLv2+ licensed (see `LICENSE`) because
the compiled WASM statically links this GPL code. Don't add a dependency to
`native/` without checking its license is GPL-compatible.

## Metadata (EXIF/TIFF tags)

`native/src/image_metadata.cpp` maps a handful of the vendored decoder's
`getMetadata()`/`digitisationTime()` fields onto standard IFD0 tags
(`DateTime`, `Make`, `Model`, `Copyright`, `ImageDescription` for the rest as
free text) — see README's "Metadata (EXIF/TIFF tags)" section for the full
field table. Two things worth knowing before touching this:

- Values are rejected if they contain non-printable bytes before being
  written into an ASCII-typed tag. This isn't defensive-for-the-sake-of-it —
  Kodak's own `REFIMAGE.PCD` reference disc has a field left as raw `0xFF`
  filler that slips past the vendor library's own charset guard.
- TIFF already hand-writes its own IFD (`tiff_writer.cpp`), so new tags are
  just more `writeIfdEntry` calls in ascending tag-number order (TIFF spec
  requires ascending order; some readers care). JPEG has no native tag
  support in libjpeg, so `jpeg_writer.cpp` builds a whole separate
  self-contained mini-TIFF as an Exif APP1 segment for the same fields, using
  the same `tiff_bytes.h` helpers `tiff_writer.cpp` uses.

## Testing & verification

`npm run test:wasm` runs `scripts/test-wasm.mjs`, which converts every file
in `test/*.PCD` to both formats and reports dimensions/sizes/warnings. **`test/`
is gitignored** — a fresh clone has an empty (or missing) `test/` directory,
so this script has nothing to run against until sample `.pcd` files are
added locally. That's expected, not a bug; don't "fix" the `.gitignore`
entry without checking with the user first (likely due to the sample scans
being someone's actual photos, not license-clean test fixtures).

This is a smoke test, not a correctness test — `rc === 0` only proves the
decoder didn't error. To actually verify a native-code change:

```sh
exiftool -a -G1 <output file>      # tags, ICC profile, IFD0 metadata
tiffdump <output.tif>              # structural IFD sanity check for TIFF
identify -verbose <output file>    # ImageMagick full decode, catches structural corruption
```

For UI changes: `npm run build && npx vite preview`, then actually drive it
in a browser (drop a file, convert, download) — don't just trust the
build/typecheck passing.

## Conventions

- Minimal comments: only WHY (a hidden constraint, a workaround, a
  non-obvious invariant), never WHAT — see the existing native/ files for the
  house style (e.g. the 64Base-clamping comment in `bridge.cpp`).
- No speculative abstraction. Shared code (e.g. `tiff_bytes.h`) was factored
  out because two files already needed the _same_ nontrivial logic, not
  preemptively.
- TypeScript strict mode with `noUnusedLocals`/`noUnusedParameters` — dead
  state/props will fail `tsc -b`, not just lint.
- ESLint flat config (`eslint.config.js`): `@eslint/js` recommended +
  `typescript-eslint` recommended + `react-hooks` + `react-refresh`.

## Deployment

`.github/workflows/deploy-pages.yml` builds and publishes `dist/` to GitHub
Pages on every push to `main` (or manual `workflow_dispatch`), installing
Emscripten via `emscripten-core/setup-emsdk` and checking out the submodule
with `submodules: recursive`. The repo's **Settings → Pages → Source** must
be "GitHub Actions" (one-time setup step).

## Keeping this file updated

Update `AGENTS.md` whenever you make a meaningful change, clarify an
assumption, or discover project context that isn't already captured here —
new files/modules, changed build steps, new gotchas, resolved ambiguities,
removed features. Remove information that's gone stale (e.g. if a documented
workaround is no longer needed) rather than letting it accumulate. Treat
this as required upkeep for the change you're making, not a separate task.
