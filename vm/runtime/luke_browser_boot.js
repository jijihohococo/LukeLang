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
    fetch: function (urlPtr, urlLen, outPtr, outCap, outLenPtr) {
      const url = readText(urlPtr, urlLen);
      var body = "";
      try {
        if (typeof XMLHttpRequest !== "undefined") {
          var xhr = new XMLHttpRequest();
          xhr.open("GET", url, false);
          xhr.send(null);
          if (xhr.status >= 200 && xhr.status < 300) body = xhr.responseText || "";
        } else if (opts.onJsFetch) {
          body = String(opts.onJsFetch(url) || "");
        } else {
          body = "";
        }
      } catch (e) {
        body = "";
      }
      const mem = getMemory();
      const u8 = new Uint8Array(mem.buffer);
      const view = new DataView(mem.buffer);
      const bytes = new TextEncoder().encode(body);
      const n = Math.min(bytes.length, outCap > 0 ? outCap - 1 : 0);
      u8.set(bytes.subarray(0, n), outPtr);
      if (outCap > 0) u8[outPtr + n] = 0;
      view.setUint32(outLenPtr, n, true);
    },
    on_click: function (idPtr, idLen, targetPtr, targetLen, msgPtr, msgLen) {
      const id = readText(idPtr, idLen);
      const target = readText(targetPtr, targetLen);
      const message = readText(msgPtr, msgLen);
      if (typeof document !== "undefined") {
        const el = document.getElementById(id);
        if (el) {
          el.addEventListener("click", function () {
            const t = document.getElementById(target);
            if (t) t.textContent = message;
          });
        }
      } else if (opts.onJsOnClick) {
        opts.onJsOnClick(id, target, message);
      } else {
        console.log("[jsOnClick #" + id + " -> #" + target + "] " + message);
      }
    },
    add_style: function (cssPtr, cssLen) {
      const css = readText(cssPtr, cssLen);
      if (typeof document !== "undefined") {
        const style = document.createElement("style");
        style.setAttribute("data-luke", "wear-style");
        style.textContent = css;
        document.head.appendChild(style);
      } else if (opts.onJsAddStyle) opts.onJsAddStyle(css);
      else console.log("[jsAddStyle] " + css.slice(0, 120) + (css.length > 120 ? "…" : ""));
    },
    load_font: function (hrefPtr, hrefLen) {
      const href = readText(hrefPtr, hrefLen);
      if (typeof document !== "undefined") {
        const link = document.createElement("link");
        link.rel = "stylesheet";
        link.href = href;
        link.setAttribute("data-luke", "bring-font");
        document.head.appendChild(link);
      } else if (opts.onJsLoadFont) opts.onJsLoadFont(href);
      else console.log("[jsLoadFont] " + href);
    },
    set_title: function (titlePtr, titleLen) {
      const title = readText(titlePtr, titleLen);
      if (typeof document !== "undefined") document.title = title;
      else if (opts.onJsSetTitle) opts.onJsSetTitle(title);
      else console.log("[jsSetTitle] " + title);
    },
    route_go: function (pathPtr, pathLen) {
      var path = readText(pathPtr, pathLen).replace(/^\//, "");
      if (typeof location !== "undefined") {
        var next = "#/" + path;
        if (location.hash !== next) location.hash = next;
        else if (typeof globalThis.__lukeDispatchRoute === "function")
          globalThis.__lukeDispatchRoute();
      } else console.log("[route_go]", path);
    },
    argus_clear: function () {
      if (typeof document === "undefined") return;
      var root = document.getElementById("root");
      if (root) root.innerHTML = "";
    },
    argus_upsert: function (idPtr, idLen, kind) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_upsert]", id, kind);
        return;
      }
      var root = document.getElementById("root");
      if (!root) return;
      if (!root.getAttribute("data-argus")) {
        root.setAttribute("data-argus", "1");
        root.style.position = "relative";
        root.style.width = "100vw";
        root.style.height = "100vh";
        root.style.overflow = "hidden";
        root.style.margin = "0";
      }
      var el = document.getElementById(id);
      if (!el) {
        var k = kind | 0;
        var tag = k === 2 ? "button" : k === 4 ? "input" : "div";
        el = document.createElement(tag);
        el.id = id;
        el.setAttribute("data-argus-node", String(k));
        el.style.position = "absolute";
        el.style.boxSizing = "border-box";
        el.style.margin = "0";
        el.style.border = "0";
        if (k === 2) {
          el.type = "button";
          el.style.cursor = "pointer";
          el.style.font = "inherit";
        }
        if (k === 3) {
          el.style.backgroundSize = "cover";
          el.style.backgroundPosition = "center";
          el.style.backgroundRepeat = "no-repeat";
        }
        if (k === 4) {
          el.type = "text";
          el.style.font = "inherit";
          el.style.padding = "0 12px";
        }
        root.appendChild(el);
      }
    },
    argus_frame: function (idPtr, idLen, x, y, w, h, opacity) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_frame]", id, x, y, w, h, opacity);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.style.left = x + "px";
      el.style.top = y + "px";
      el.style.width = w + "px";
      el.style.height = h + "px";
      el.style.opacity = String(opacity);
    },
    argus_text: function (idPtr, idLen, textPtr, textLen) {
      const id = readText(idPtr, idLen);
      const text = readText(textPtr, textLen);
      if (typeof document === "undefined") {
        console.log("[argus_text]", id, text);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.textContent = text;
    },
    argus_image: function (idPtr, idLen, srcPtr, srcLen) {
      const id = readText(idPtr, idLen);
      const src = readText(srcPtr, srcLen);
      if (typeof document === "undefined") {
        console.log("[argus_image]", id, src);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.style.backgroundImage = src ? 'url("' + src.replace(/"/g, '\\"') + '")' : "";
    },
    argus_input: function (idPtr, idLen, phPtr, phLen) {
      const id = readText(idPtr, idLen);
      const ph = readText(phPtr, phLen);
      if (typeof document === "undefined") {
        console.log("[argus_input]", id, ph);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.placeholder = ph;
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
        wireLukeWhens(instance, opts && opts.whens);
        return { code: e.exitCode, instance: instance };
      }
      throw e;
    }
    wireLukeWhens(instance, opts && opts.whens);
    return { code: 0, instance: instance };
  });
}

function lukeCurrentRoute() {
  if (typeof location === "undefined") return "home";
  var h = (location.hash || "").replace(/^#\/?/, "");
  return h || "home";
}

function wireLukeWhens(instance, whens) {
  if (!instance || !whens || !whens.length) return;
  globalThis.__lukeInstance = instance;
  globalThis.__lukeWhens = whens;

  function runExport(name) {
    var fn = instance.exports[name];
    if (typeof fn !== "function") return;
    try {
      fn();
    } catch (e) {
      console.error(e);
    }
  }

  function dispatch(id, event) {
    for (var i = 0; i < whens.length; i++) {
      var w = whens[i];
      var ev = w.event || "click";
      if (w.id === id && ev === event) runExport(w.export);
    }
  }

  function dispatchRoute() {
    var route = lukeCurrentRoute();
    console.log("[route]", route);
    dispatch(route, "route");
  }
  globalThis.__lukeDispatchRoute = dispatchRoute;

  if (typeof document === "undefined") {
    /* Node smoke: run home route handler if present */
    dispatch("home", "route");
    return;
  }

  var root = document.getElementById("root");
  if (root && !root.getAttribute("data-luke-events")) {
    root.setAttribute("data-luke-events", "1");
    root.addEventListener("click", function (e) {
      var t = e.target;
      while (t && t !== root) {
        if (t.id) dispatch(t.id, "click");
        t = t.parentNode;
      }
    });
    root.addEventListener("change", function (e) {
      if (e.target && e.target.id) dispatch(e.target.id, "change");
    });
    root.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && e.target && e.target.id) dispatch(e.target.id, "submit");
    });
  }
  if (typeof window !== "undefined" && !window.__lukeHashWired) {
    window.__lukeHashWired = true;
    window.addEventListener("hashchange", dispatchRoute);
  }
  dispatchRoute();
}

function browserBootstrap(wasmUrl, opts) {
  opts = opts || {};
  return fetch(wasmUrl)
    .then(function (r) {
      return r.arrayBuffer();
    })
    .then(function (b) {
      return runLukeWasm(new Uint8Array(b), {
        whens: opts.whens || [],
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
  window.wireLukeWhens = wireLukeWhens;
  window.browserBootstrap = browserBootstrap;
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = {
    createLukeWasi: createLukeWasi,
    createLukeJs: createLukeJs,
    runLukeWasm: runLukeWasm,
    wireLukeWhens: wireLukeWhens,
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
    .then(function (res) {
      process.exit((res && res.code) || 0);
    })
    .catch(function (e) {
      console.error(e);
      process.exit(typeof e.exitCode === "number" ? e.exitCode : 1);
    });
}
