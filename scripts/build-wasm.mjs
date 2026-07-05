#!/usr/bin/env node
// Compiles the native/ C++ bridge (+ the vendored pcdDecode library) to a
// WebAssembly ES module via Emscripten. Run manually with `npm run
// build:wasm`, or automatically from vite.config.ts before dev/build.
//
// Requires Emscripten (emcc) on PATH: https://emscripten.org/docs/getting_started/downloads.html

import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = dirname(dirname(fileURLToPath(import.meta.url)));
const nativeDir = join(rootDir, "native", "src");
const vendorDir = join(rootDir, "vendor", "pcdtojpeg", "src");
const outDir = join(rootDir, "src", "wasm");
const outBase = join(outDir, "pcd-codec");

const sources = [
  join(nativeDir, "bridge.cpp"),
  join(nativeDir, "image_metadata.cpp"),
  join(nativeDir, "jpeg_writer.cpp"),
  join(nativeDir, "tiff_writer.cpp"),
  join(vendorDir, "pcdDecode.cpp"),
];

const headers = [
  join(nativeDir, "image_metadata.h"),
  join(nativeDir, "jpeg_writer.h"),
  join(nativeDir, "tiff_writer.h"),
  join(nativeDir, "tiff_bytes.h"),
  join(nativeDir, "icc_srgb.h"),
  join(vendorDir, "pcdDecode.h"),
];

function newestMtime(paths) {
  return Math.max(...paths.map((p) => statSync(p).mtimeMs));
}

function isUpToDate() {
  const jsOut = `${outBase}.mjs`;
  const wasmOut = `${outBase}.wasm`;
  if (!existsSync(jsOut) || !existsSync(wasmOut)) return false;
  const outputMtime = Math.min(statSync(jsOut).mtimeMs, statSync(wasmOut).mtimeMs);
  return outputMtime > newestMtime([...sources, ...headers]);
}

function checkEmcc() {
  try {
    execFileSync("emcc", ["--version"], { stdio: "pipe" });
  } catch {
    console.error(
      "\n[build-wasm] emcc (Emscripten) was not found on PATH.\n" +
        "Install it from https://emscripten.org/docs/getting_started/downloads.html\n" +
        "or (macOS) `brew install emscripten`, then re-run `npm run build:wasm`.\n"
    );
    process.exit(1);
  }
}

export function buildWasm({ force = false } = {}) {
  if (!force && isUpToDate()) {
    console.log("[build-wasm] up to date, skipping (pass --force to rebuild)");
    return;
  }

  checkEmcc();
  mkdirSync(outDir, { recursive: true });

  const args = [
    ...sources,
    "-I", nativeDir,
    "-I", vendorDir,
    "-DmNoPThreads", // avoid pthreads: no COOP/COEP headers required, works on any static host
    "-O3",
    "-sUSE_ZLIB=1",
    "-sUSE_LIBJPEG=1",
    "-sMODULARIZE=1",
    "-sEXPORT_ES6=1",
    "-sEXPORT_NAME=createPcdCodecModule",
    "-sENVIRONMENT=web,worker,node",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=64MB",
    "-sFILESYSTEM=1",
    "-sEXPORTED_FUNCTIONS=_pcd_convert,_malloc,_free",
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,getValue,setValue,UTF8ToString",
    "-sNODEJS_CATCH_EXIT=0",
    "-sNODEJS_CATCH_REJECTION=0",
    "-o", `${outBase}.mjs`,
  ];

  console.log("[build-wasm] emcc " + args.join(" "));
  execFileSync("emcc", args, { stdio: "inherit", cwd: rootDir });
  console.log(`[build-wasm] wrote ${outBase}.mjs / .wasm`);
}

const isMain = process.argv[1] === fileURLToPath(import.meta.url);
if (isMain) {
  buildWasm({ force: process.argv.includes("--force") });
}
