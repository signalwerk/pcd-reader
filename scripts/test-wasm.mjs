#!/usr/bin/env node
// Sanity check: runs the compiled WASM codec against every .pcd/.PCD file in
// /test, producing a .jpg and .tif for each in a scratch output directory.
// Not a unit test suite -- a quick end-to-end smoke test you can eyeball
// (dimensions, warnings, output file sizes) after touching the native code.

import { readFileSync, readdirSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, basename, extname } from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = dirname(dirname(fileURLToPath(import.meta.url)));
const testDir = join(rootDir, "test");
const outDir = process.argv[2] || join(rootDir, ".wasm-test-out");

const { default: createPcdCodecModule } = await import(join(rootDir, "src", "wasm", "pcd-codec.mjs"));

mkdirSync(outDir, { recursive: true });

const Module = await createPcdCodecModule();

const FORMAT_JPEG = 0;
const FORMAT_TIFF = 1;
const RESOLUTION_16BASE = 4;
const WHITE_BALANCE_D65 = 0;

function convert(pcdBytes, format, quality) {
  Module.FS.writeFile("/in.pcd", pcdBytes);
  const outPath = format === FORMAT_TIFF ? "/out.tif" : "/out.jpg";

  const outWidthPtr = Module._malloc(4);
  const outHeightPtr = Module._malloc(4);
  const msgLen = 512;
  const msgPtr = Module._malloc(msgLen);

  const rc = Module.ccall(
    "pcd_convert",
    "number",
    ["string", "string", "number", "number", "number", "number", "number", "number", "number", "number", "number"],
    ["/in.pcd", outPath, format, RESOLUTION_16BASE, WHITE_BALANCE_D65, 0, quality, outWidthPtr, outHeightPtr, msgPtr, msgLen]
  );

  const width = Module.getValue(outWidthPtr, "i32");
  const height = Module.getValue(outHeightPtr, "i32");
  const message = Module.UTF8ToString(msgPtr);

  Module._free(outWidthPtr);
  Module._free(outHeightPtr);
  Module._free(msgPtr);

  if (rc !== 0) {
    Module.FS.unlink("/in.pcd");
    throw new Error(`pcd_convert failed (rc=${rc}): ${message}`);
  }

  const data = Module.FS.readFile(outPath);
  Module.FS.unlink("/in.pcd");
  Module.FS.unlink(outPath);

  return { data, width, height, warning: message };
}

const files = readdirSync(testDir).filter((f) => extname(f).toLowerCase() === ".pcd");
if (files.length === 0) {
  console.error(`No .PCD files found in ${testDir}`);
  process.exit(1);
}

let failures = 0;
for (const file of files) {
  const name = basename(file, extname(file));
  const pcdBytes = new Uint8Array(readFileSync(join(testDir, file)));
  console.log(`\n=== ${file} (${(pcdBytes.length / 1024).toFixed(0)} KB) ===`);

  try {
    const jpeg = convert(pcdBytes, FORMAT_JPEG, 92);
    const jpegPath = join(outDir, `${name}.jpg`);
    writeFileSync(jpegPath, jpeg.data);
    console.log(
      `  JPEG: ${jpeg.width}x${jpeg.height}, ${(jpeg.data.length / 1024).toFixed(0)} KB -> ${jpegPath}` +
        (jpeg.warning ? ` [warning: ${jpeg.warning}]` : "")
    );
  } catch (err) {
    console.error(`  JPEG FAILED: ${err.message}`);
    failures++;
  }

  try {
    const tiff = convert(pcdBytes, FORMAT_TIFF, 0);
    const tiffPath = join(outDir, `${name}.tif`);
    writeFileSync(tiffPath, tiff.data);
    console.log(
      `  TIFF: ${tiff.width}x${tiff.height}, ${(tiff.data.length / 1024).toFixed(0)} KB -> ${tiffPath}` +
        (tiff.warning ? ` [warning: ${tiff.warning}]` : "")
    );
  } catch (err) {
    console.error(`  TIFF FAILED: ${err.message}`);
    failures++;
  }
}

console.log(`\n${files.length} file(s) processed, ${failures} failure(s). Output in ${outDir}`);
process.exit(failures > 0 ? 1 : 0);
