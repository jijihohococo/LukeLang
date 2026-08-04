#!/usr/bin/env node
/**
 * Minimal browser / Node loader for Luke Build WASM (WASI preview1 subset).
 * Usage:
 *   node scripts/luke_browser_loader.cjs path/to/app.wasm
 *   — or open the generated .html which includes this file (as .js).
 */
"use strict";

function createLukeWasi(getMemory, opts) {
  opts = opts || {};
  const lineBuf = { 1: "", 2: "" };
  const output =
    opts.onWrite ||
    function (fd, text) {
      if (typeof process !== "undefined" && process.stdout) {
        if (fd === 2) process.stderr.write(text);
        else process.stdout.write(text);
      } else {
        console.log(text.replace(/\n$/, ""));
      }
    };

  function writeIovs(fd, iovs, iovsLen, nwrittenPtr) {
    const mem = getMemory();
    const view = new DataView(mem.buffer);
    const u8 = new Uint8Array(mem.buffer);
    let written = 0;
    for (let i = 0; i < iovsLen; i++) {
      const ptr = view.getUint32(iovs + i * 8, true);
      const len = view.getUint32(iovs + i * 8 + 4, true);
      const chunk = u8.subarray(ptr, ptr + len);
      const text = new TextDecoder().decode(chunk);
      if (fd === 1 || fd === 2) {
        lineBuf[fd] += text;
        let idx;
        while ((idx = lineBuf[fd].indexOf("\n")) >= 0) {
          output(fd, lineBuf[fd].slice(0, idx + 1));
          lineBuf[fd] = lineBuf[fd].slice(idx + 1);
        }
      }
      written += len;
    }
    view.setUint32(nwrittenPtr, written, true);
    return 0;
  }

  return {
    fd_close: function () {
      return 0;
    },
    fd_fdstat_get: function (fd, statPtr) {
      const mem = getMemory();
      const view = new DataView(mem.buffer);
      view.setUint8(statPtr, 2);
      return 0;
    },
    fd_seek: function (fd, offset, whence, newOffsetPtr) {
      const mem = getMemory();
      const view = new DataView(mem.buffer);
      view.setBigUint64(newOffsetPtr, BigInt(0), true);
      return 0;
    },
    fd_write: writeIovs,
    proc_exit: function (code) {
      const err = new Error("exit " + code);
      err.exitCode = code;
      throw err;
    },
  };
}

function runLukeWasm(wasmBytes, opts) {
  var memory;
  var wasi = createLukeWasi(function () {
    return memory;
  }, opts);
  return WebAssembly.instantiate(wasmBytes, {
    wasi_snapshot_preview1: wasi,
  }).then(function (result) {
    var instance = result.instance;
    memory = instance.exports.memory;
    try {
      if (typeof instance.exports._start === "function") instance.exports._start();
      else if (typeof instance.exports.main === "function") instance.exports.main();
    } catch (e) {
      if (e && typeof e.exitCode === "number") {
        if (e.exitCode !== 0) throw e;
        return e.exitCode;
      }
      throw e;
    }
    return 0;
  });
}

function browserBootstrap(wasmUrl) {
  return fetch(wasmUrl)
    .then(function (r) {
      return r.arrayBuffer();
    })
    .then(function (b) {
      return runLukeWasm(new Uint8Array(b), {
        onWrite: function (fd, text) {
          var el = document.getElementById("luke-out");
          if (el) el.textContent += text;
          else console.log(text.replace(/\n$/, ""));
        },
      });
    });
}

if (typeof window !== "undefined") {
  window.createLukeWasi = createLukeWasi;
  window.runLukeWasm = runLukeWasm;
  window.browserBootstrap = browserBootstrap;
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = { createLukeWasi: createLukeWasi, runLukeWasm: runLukeWasm, browserBootstrap: browserBootstrap };
}

var isNode =
  typeof process !== "undefined" &&
  process.argv &&
  typeof require !== "undefined" &&
  require.main === module;

if (isNode) {
  var path = process.argv[2];
  if (!path) {
    console.error("usage: luke_browser_loader.cjs <file.wasm>");
    process.exit(1);
  }
  var fs = require("fs");
  runLukeWasm(fs.readFileSync(path))
    .then(function (code) {
      process.exit(code || 0);
    })
    .catch(function (e) {
      console.error(e);
      process.exit(typeof e.exitCode === "number" ? e.exitCode : 1);
    });
}
