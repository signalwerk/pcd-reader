# PCD Converter

A static, client-side web app that converts Kodak Photo CD (`.pcd`) files to
color-managed **JPEG** or **TIFF**, entirely in the browser via WebAssembly.
No files are ever uploaded — decoding and encoding happen locally.

- Full PhotoCD decode (Base/16 through 16Base) with proper color management
  (sRGB, D65/D50 white balance, monochrome mode). Defaults to TIFF output at
  the highest resolution setting (64Base); since that always needs a separate
  Overview Pac file a standalone `.pcd` doesn't include, it transparently
  falls back to 16Base — the app surfaces this once, not per file (see below).
- Both output formats embed the sRGB ICC profile for accurate color
  reproduction in any color-managed viewer.
- TIFF output uses ZIP (Adobe Deflate) compression with a horizontal
  differencing predictor.
- Save results directly to a folder via the File System Access API
  (Chromium browsers), or fall back to per-file downloads everywhere else.
- Progress indicator; a single deduplicated notice for any decoder warnings
  (shown once above the file list, not repeated per file) plus per-file error
  reporting.

## Architecture

```
native/            our C++ WASM bridge: bridge.cpp, jpeg_writer.cpp, tiff_writer.cpp
vendor/pcdtojpeg/   git submodule: the vendored PhotoCD decoder (pcdDecode.{h,cpp})
scripts/            build-wasm.mjs (emcc build), test-wasm.mjs (Node smoke test)
src/                the React + TypeScript app
  lib/pcdCodec.ts    JS <-> WASM bridge (Emscripten MEMFS in/out)
  lib/fileSave.ts    File System Access API + download fallback
  wasm/              generated: pcd-codec.mjs / pcd-codec.wasm (gitignored)
test/               sample .PCD files for manual/automated testing
```

The only PhotoCD decoding logic is the vendored
[`pcdtojpeg`](https://sourceforge.net/projects/pcdtojpeg/) library
(`vendor/pcdtojpeg`, GPLv2+, by Sandy McGuffog). That project only ever
produced JPEG output; **TIFF support is new code** written for this project
(`native/src/tiff_writer.cpp`) and is not derived from the vendor library.

Both encoders embed the classic HP/Microsoft "sRGB IEC61966-2.1" ICC profile
(the same one pcdtojpeg's own JPEG output used), extracted once into
`native/src/icc_srgb.h` — see the license notice in that file and in
`native/src/jpeg_writer.cpp`.

> **Submodule note:** the SourceForge project page only offers zip file
> releases (no git/svn repo to point a submodule at). `vendor/pcdtojpeg` is
> instead pinned to the `v1.0.17` tag of
> [`signalwerk/pcdtojpeg`](https://github.com/signalwerk/pcdtojpeg), a git
> mirror of that same release. The vendored source is kept pristine —
> unmodified — see the 64Base note below for why.

## Setup

Requires [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
(`emcc` on `PATH`) to compile the native code to WebAssembly.

```sh
git clone --recurse-submodules <this repo>
npm install
npm run dev      # builds the WASM module (if stale), then starts Vite
```

`npm run dev` and `npm run build` both run `scripts/build-wasm.mjs`
automatically via a Vite plugin (skipped if the compiled output is already
newer than the native/vendor sources). To build the WASM module on its own:

```sh
npm run build:wasm          # only rebuilds if native/vendor sources changed
npm run build:wasm -- --force
```

## Testing

```sh
npm run test:wasm    # decodes every test/*.PCD file to .jpg and .tif in .wasm-test-out/
```

This is a smoke test, not a unit test suite: it exercises the real decoder
and both encoders against the sample files in `test/` and reports
dimensions, output sizes, and any decoder warnings. Verify output validity
with `exiftool` / ImageMagick's `identify`, e.g.:

```sh
exiftool .wasm-test-out/IMG0002.tif   # Compression: Adobe Deflate, Predictor: Horizontal differencing, embedded ICC profile
```

For UI changes, `npm run build && npx vite preview` and drive it in a real
browser (drop a file from `test/`, convert, save/download).

## Deployment

`.github/workflows/deploy-pages.yml` builds and publishes `dist/` to GitHub
Pages on every push to `main` (or manually via `workflow_dispatch`). Since
the build needs Emscripten, the workflow installs it with
[`emscripten-core/setup-emsdk`](https://github.com/emscripten-core/setup-emsdk)
(pinned to `5.0.7`, the version this project was built/tested against) before
running `npm run build`, and checks out `vendor/pcdtojpeg` with
`submodules: recursive`. The repo's **Settings → Pages → Source** must be set
to "GitHub Actions" (usually a one-time step the first time Pages is enabled).

## Browser support

Saving straight to a folder needs the File System Access API
(`showDirectoryPicker`), currently Chromium-only (Chrome, Edge, etc.). On any
other browser the app automatically falls back to triggering a normal
download per file.

## Notes / possible follow-ups

- Conversion runs synchronously on the main thread; for very large batches of
  16Base/64Base images, moving the WASM call into a Web Worker would keep the
  UI fully responsive during each individual conversion.
- 64Base (4096×6144) images are stored as a Base Image Pac plus a separate
  Overview Pac (IPE) file on the original Photo CD; this app only accepts a
  single file per image, so it never has that companion file. Rather than
  asking the vendored decoder for 64Base with no IPE file (its internal
  `parseICFile()` indexes into that filename unconditionally whenever the
  requested resolution reaches 64Base, which isn't safe to do with none
  supplied), `native/src/bridge.cpp` clamps the request to 16Base itself and
  reports why. This keeps the vendored source untouched — see the submodule
  note above.

## License

This repository as a whole is licensed under the **GNU GPL v2 (or later)**
(see `LICENSE`), because it statically links the GPLv2+ `pcdtojpeg` decoder
(`vendor/pcdtojpeg`, © Sandy McGuffog and contributors) into the compiled
WASM module. The embedded sRGB ICC profile is © 1998 Hewlett-Packard Company,
used under HP's royalty-free sRGB Profile Licensing Agreement (see
`native/src/icc_srgb.h`).

## Sources

[Ted Felix · Kodak Photo CD](http://www.tedfelix.com/PhotoCD/index.html)
