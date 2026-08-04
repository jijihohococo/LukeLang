#!/usr/bin/env node
/**
 * Minimal browser / Node loader for Luke Build WASM (WASI + optional lukejs).
 * Usage:
 *   node scripts/luke_browser_loader.cjs path/to/app.wasm
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
    args_sizes_get: function (argcPtr, bufSizePtr) {
      const mem = getMemory();
      const view = new DataView(mem.buffer);
      // Provide a single argv[0] = "luke"
      view.setUint32(argcPtr, 1, true);
      view.setUint32(bufSizePtr, 5, true); // "luke\0"
      return 0;
    },
    args_get: function (argvPtr, argvBufPtr) {
      const mem = getMemory();
      const view = new DataView(mem.buffer);
      const u8 = new Uint8Array(mem.buffer);
      view.setUint32(argvPtr, argvBufPtr, true);
      u8[argvBufPtr] = 108; // l
      u8[argvBufPtr + 1] = 117; // u
      u8[argvBufPtr + 2] = 107; // k
      u8[argvBufPtr + 3] = 101; // e
      u8[argvBufPtr + 4] = 0;
      return 0;
    },
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

function createLukeJs(getMemory, opts) {
  opts = opts || {};
  const decoder = new TextDecoder();
  function readText(ptr, len) {
    const mem = getMemory();
    return decoder.decode(new Uint8Array(mem.buffer, ptr, len));
  }
  return {
    set_text: function (idPtr, idLen, textPtr, textLen) {
      const id = readText(idPtr, idLen);
      const text = readText(textPtr, textLen);
      if (typeof document !== "undefined") {
        const el = document.getElementById(id);
        if (el) el.textContent = text;
      } else if (opts.onJsSetText) opts.onJsSetText(id, text);
      else console.log("[jsSetText #" + id + "] " + text);
    },
    set_html: function (idPtr, idLen, htmlPtr, htmlLen) {
      const id = readText(idPtr, idLen);
      const html = readText(htmlPtr, htmlLen);
      if (typeof document !== "undefined") {
        const el = document.getElementById(id);
        if (el) el.innerHTML = html;
      } else if (opts.onJsSetHtml) opts.onJsSetHtml(id, html);
      else console.log("[jsSetHtml #" + id + "] " + html);
    },
    get_value: function (idPtr, idLen, outPtr, outCap, outLenPtr) {
      const id = readText(idPtr, idLen);
      var value = "";
      if (typeof document !== "undefined") {
        const el = document.getElementById(id);
        if (el) value = el.value != null ? String(el.value) : el.textContent || "";
      }
      const mem = getMemory();
      const u8 = new Uint8Array(mem.buffer);
      const view = new DataView(mem.buffer);
      const bytes = new TextEncoder().encode(value);
      const n = Math.min(bytes.length, outCap > 0 ? outCap - 1 : 0);
      u8.set(bytes.subarray(0, n), outPtr);
      if (outCap > 0) u8[outPtr + n] = 0;
      view.setUint32(outLenPtr, n, true);
    },
  };
}

function runLukeWasm(wasmBytes, opts) {
  var memory;
  var getMemory = function () {
    return memory;
  };
  var imports = {
    wasi_snapshot_preview1: createLukeWasi(getMemory, opts),
    lukejs: createLukeJs(getMemory, opts),
  };
  return WebAssembly.instantiate(wasmBytes, imports).then(function (result) {
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
  window.createLukeJs = createLukeJs;
  window.runLukeWasm = runLukeWasm;
  window.browserBootstrap = browserBootstrap;
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = {
    createLukeWasi: createLukeWasi,
    createLukeJs: createLukeJs,
    runLukeWasm: runLukeWasm,
    browserBootstrap: browserBootstrap,
  };
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
