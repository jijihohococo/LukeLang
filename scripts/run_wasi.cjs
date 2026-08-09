#!/usr/bin/env node
// Minimal WASI runner for Luke Build wasm outputs.
const fs = require("fs");
const { WASI } = require("wasi");
const path = process.argv[2];
if (!path) {
  console.error("usage: run_wasi.js <file.wasm>");
  process.exit(1);
}
const wasi = new WASI({
  version: "preview1",
  args: [path],
  env: process.env,
  preopens: { ".": process.cwd(), "/": "/" },
});
const wasm = fs.readFileSync(path);
WebAssembly.instantiate(wasm, wasi.getImportObject())
  .then(({ instance }) => {
    wasi.start(instance);
  })
  .catch((e) => {
    console.error(e);
    process.exit(1);
  });
