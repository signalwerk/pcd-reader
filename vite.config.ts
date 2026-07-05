import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { buildWasm } from "./scripts/build-wasm.mjs";

// Builds the native/ C++ -> WebAssembly module before dev/build starts, so
// `npm run dev` and `npm run build` are self-contained (no separate manual
// step) as long as Emscripten (emcc) is installed. Skips the rebuild if the
// compiled output is already newer than every native/vendor source file.
function wasmBuildPlugin(): Plugin {
  return {
    name: "pcd-wasm-build",
    buildStart() {
      buildWasm();
    },
  };
}

export default defineConfig({
  base: "./",
  plugins: [wasmBuildPlugin(), react()],
});
