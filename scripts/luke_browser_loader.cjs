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
    fetch_start: function (idPtr, idLen, methodPtr, methodLen, urlPtr, urlLen, bodyPtr, bodyLen) {
      var id = readText(idPtr, idLen);
      var method = (readText(methodPtr, methodLen) || "GET").toUpperCase();
      var url = readText(urlPtr, urlLen);
      var body = readText(bodyPtr, bodyLen);
      if (!globalThis.__lukeFetchJobs) globalThis.__lukeFetchJobs = {};
      globalThis.__lukeFetchJobs[id] = { ready: false, status: 0, body: "" };
      console.log("[fetch_start]", id, method, url);
      var finish = function (status, text) {
        globalThis.__lukeFetchJobs[id] = { ready: true, status: status | 0, body: text || "" };
        console.log("[fetch_ready]", id, status);
        if (typeof globalThis.__lukeDispatchFetch === "function") globalThis.__lukeDispatchFetch(id);
      };
      /* Deterministic stub for tests / offline — luke://path */
      if (url.indexOf("luke://") === 0) {
        var path = url.slice("luke://".length);
        var payload = '{"ok":true,"path":"' + path.replace(/"/g, '\\"') + '"}';
        setTimeout(function () {
          finish(200, payload);
        }, 0);
        return;
      }
      if (typeof fetch === "function") {
        var opts = { method: method };
        if (method === "POST" || method === "PUT" || method === "PATCH") opts.body = body;
        fetch(url, opts)
          .then(function (r) {
            return r.text().then(function (t) {
              finish(r.status, t);
            });
          })
          .catch(function () {
            finish(0, "");
          });
      } else if (typeof XMLHttpRequest !== "undefined") {
        try {
          var xhr = new XMLHttpRequest();
          xhr.open(method, url, true);
          xhr.onload = function () {
            finish(xhr.status, xhr.responseText || "");
          };
          xhr.onerror = function () {
            finish(0, "");
          };
          xhr.send(method === "GET" || method === "HEAD" ? null : body);
        } catch (e) {
          finish(0, "");
        }
      } else {
        setTimeout(function () {
          finish(200, "[fetch stub] " + url);
        }, 0);
      }
    },
    fetch_ready: function (idPtr, idLen) {
      var id = readText(idPtr, idLen);
      var job = globalThis.__lukeFetchJobs && globalThis.__lukeFetchJobs[id];
      return job && job.ready ? 1 : 0;
    },
    fetch_status: function (idPtr, idLen) {
      var id = readText(idPtr, idLen);
      var job = globalThis.__lukeFetchJobs && globalThis.__lukeFetchJobs[id];
      return job ? job.status : 0;
    },
    fetch_body: function (idPtr, idLen, outPtr, outCap, outLenPtr) {
      var id = readText(idPtr, idLen);
      var job = globalThis.__lukeFetchJobs && globalThis.__lukeFetchJobs[id];
      var value = job && job.ready ? String(job.body || "") : "";
      const mem = getMemory();
      const u8 = new Uint8Array(mem.buffer);
      const view = new DataView(mem.buffer);
      const bytes = new TextEncoder().encode(value);
      const n = Math.min(bytes.length, outCap > 0 ? outCap - 1 : 0);
      u8.set(bytes.subarray(0, n), outPtr);
      if (outCap > 0) u8[outPtr + n] = 0;
      view.setUint32(outLenPtr, n, true);
    },
    timeline_start: function (idPtr, idLen, ms) {
      var id = readText(idPtr, idLen);
      var dur = ms > 0 ? ms : 300;
      console.log("[timeline_start]", id, dur);
      setTimeout(function () {
        var inst = globalThis.__lukeInstance;
        if (!inst) return;
        var start = Date.now();
        function tick() {
          var t = (Date.now() - start) / dur;
          if (t >= 1) {
            if (typeof inst.exports.luke_timeline_finish_export === "function")
              inst.exports.luke_timeline_finish_export();
            if (typeof globalThis.__lukeDispatch === "function")
              globalThis.__lukeDispatch(id, "timeline");
            return;
          }
          if (typeof inst.exports.luke_timeline_progress === "function")
            inst.exports.luke_timeline_progress(t);
          setTimeout(tick, 16);
        }
        tick();
      }, 0);
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
        var tag = "div";
        if (k === 2) tag = "button";
        else if (k === 4) tag = "input";
        else if (k === 5) tag = "select";
        else if (k === 6) tag = "table";
        else if (k === 7) tag = "div";
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
          el.setAttribute("role", "img");
        }
        if (k === 4) {
          el.type = "text";
          el.style.font = "inherit";
          el.style.padding = "0 12px";
          el.setAttribute("data-argus-input", "1");
        }
        if (k === 5) {
          el.style.font = "inherit";
          el.style.padding = "0 8px";
        }
        if (k === 6) {
          el.style.borderCollapse = "collapse";
          el.style.background = "transparent";
          el.style.color = "inherit";
        }
        if (k === 7) {
          el.setAttribute("role", "dialog");
          el.setAttribute("aria-modal", "true");
          el.style.background = "rgba(16,24,32,0.96)";
          el.style.padding = "16px";
          el.style.zIndex = "1000";
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
      if (src) el.setAttribute("aria-label", src);
    },
    argus_input: function (idPtr, idLen, phPtr, phLen, inputType) {
      const id = readText(idPtr, idLen);
      const ph = readText(phPtr, phLen);
      var t = inputType | 0;
      var type =
        t === 1 ? "password" : t === 2 ? "email" : t === 3 ? "checkbox" : t === 4 ? "radio" : "text";
      if (typeof document === "undefined") {
        console.log("[argus_input]", id, type, ph);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.type = type;
      if (t === 3 || t === 4) {
        if (ph) el.setAttribute("aria-label", ph);
        el.removeAttribute("placeholder");
      } else {
        el.placeholder = ph;
        if (ph) el.setAttribute("aria-label", ph);
      }
    },
    argus_a11y: function (idPtr, idLen, rolePtr, roleLen, labelPtr, labelLen) {
      const id = readText(idPtr, idLen);
      const role = readText(rolePtr, roleLen);
      const label = readText(labelPtr, labelLen);
      if (typeof document === "undefined") {
        console.log("[argus_a11y]", id, role, label);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      if (role) el.setAttribute("role", role);
      if (label) el.setAttribute("aria-label", label);
      if (role === "dialog") el.setAttribute("aria-modal", "true");
    },
    argus_select: function (idPtr, idLen, optPtr, optLen) {
      const id = readText(idPtr, idLen);
      const options = readText(optPtr, optLen);
      if (typeof document === "undefined") {
        console.log("[argus_select]", id, options);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.innerHTML = "";
      var parts = options.split("|");
      for (var i = 0; i < parts.length; i++) {
        var o = document.createElement("option");
        o.value = parts[i];
        o.textContent = parts[i];
        el.appendChild(o);
      }
    },
    argus_table: function (idPtr, idLen, cellPtr, cellLen) {
      const id = readText(idPtr, idLen);
      const cells = readText(cellPtr, cellLen);
      if (typeof document === "undefined") {
        console.log("[argus_table]", id, cells);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.innerHTML = "";
      var rows = cells.split(";");
      for (var r = 0; r < rows.length; r++) {
        if (!rows[r]) continue;
        var tr = document.createElement("tr");
        var cols = rows[r].split("|");
        for (var c = 0; c < cols.length; c++) {
          var cell = document.createElement(r === 0 ? "th" : "td");
          cell.textContent = cols[c];
          cell.style.padding = "6px 10px";
          cell.style.borderBottom = "1px solid rgba(255,255,255,0.12)";
          tr.appendChild(cell);
        }
        el.appendChild(tr);
      }
    },
    measure_text: function (textPtr, textLen) {
      const text = readText(textPtr, textLen);
      if (typeof document === "undefined") return text.length * 8;
      if (!measure_text._canvas) {
        measure_text._canvas = document.createElement("canvas");
        measure_text._ctx = measure_text._canvas.getContext("2d");
      }
      var ctx = measure_text._ctx;
      ctx.font = "16px sans-serif";
      return ctx.measureText(text || "").width || 0;
    },
    viewport_width: function () {
      if (typeof window === "undefined") return 1280;
      return window.innerWidth || 1280;
    },
    viewport_height: function () {
      if (typeof window === "undefined") return 720;
      return window.innerHeight || 720;
    },
    now_ms: function () {
      if (typeof performance !== "undefined" && performance.now) return performance.now();
      return Date.now();
    },
    argus_fade: function (idPtr, idLen, from, to, ms) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_fade]", id, from, to, ms);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      var startFrom = from;
      if (!(startFrom >= 0)) {
        startFrom = parseFloat(el.style.opacity || "1");
        if (!(startFrom >= 0)) startFrom = 1;
      }
      var dur = ms > 0 ? ms : 1;
      var start = typeof performance !== "undefined" && performance.now ? performance.now() : Date.now();
      function easeOutCubic(t) {
        return 1 - Math.pow(1 - t, 3);
      }
      function tick(now) {
        var t = Math.min(1, (now - start) / dur);
        el.style.opacity = String(startFrom + (to - startFrom) * easeOutCubic(t));
        if (t < 1 && typeof requestAnimationFrame !== "undefined") requestAnimationFrame(tick);
        else el.style.opacity = String(to);
      }
      if (typeof requestAnimationFrame !== "undefined") requestAnimationFrame(tick);
      else el.style.opacity = String(to);
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
  function dispatchFetch(jobId) {
    console.log("[fetch_dispatch]", jobId);
    dispatch(jobId, "fetch");
  }
  function dispatchViewport() {
    for (var i = 0; i < whens.length; i++) {
      var w = whens[i];
      if ((w.event || "") === "viewport") runExport(w.export);
    }
  }
  globalThis.__lukeDispatchRoute = dispatchRoute;
  globalThis.__lukeDispatchFetch = dispatchFetch;
  globalThis.__lukeDispatchViewport = dispatchViewport;
  globalThis.__lukeDispatch = dispatch;

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
  if (typeof window !== "undefined" && !window.__lukeViewportWired) {
    window.__lukeViewportWired = true;
    var resizeTimer = null;
    window.addEventListener("resize", function () {
      if (resizeTimer) clearTimeout(resizeTimer);
      resizeTimer = setTimeout(dispatchViewport, 80);
    });
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
    console.error("usage: luke_browser_loader.cjs <file.wasm> [--dispatch id:event] [--wait-ms N]");
    process.exit(1);
  }
  var fs = require("fs");
  var dispatchSpec = null;
  var waitMs = 0;
  for (var ai = 3; ai < process.argv.length; ai++) {
    if (process.argv[ai] === "--dispatch" && process.argv[ai + 1]) {
      dispatchSpec = process.argv[++ai];
    } else if (process.argv[ai] === "--wait-ms" && process.argv[ai + 1]) {
      waitMs = parseInt(process.argv[++ai], 10) || 0;
    }
  }
  var whens = [];
  var htmlPath = path.replace(/\.wasm$/i, ".html");
  if (fs.existsSync(htmlPath)) {
    var html = fs.readFileSync(htmlPath, "utf8");
    var m = html.match(/var LUKE_WHENS = (\[[\s\S]*?\]);/);
    if (m) {
      try {
        whens = Function("return (" + m[1] + ");")();
      } catch (e) {
        console.error("failed to parse LUKE_WHENS", e);
      }
    }
  }
  runLukeWasm(fs.readFileSync(path), { whens: whens })
    .then(function (res) {
      if (dispatchSpec && typeof globalThis.__lukeDispatch === "function") {
        var parts = String(dispatchSpec).split(":");
        var id = parts[0] || "";
        var ev = parts[1] || "click";
        console.log("[dispatch]", id, ev);
        globalThis.__lukeDispatch(id, ev);
      }
      var code = (res && res.code) || 0;
      if (waitMs > 0) {
        return new Promise(function (resolve) {
          setTimeout(function () {
            resolve(code);
          }, waitMs);
        });
      }
      return code;
    })
    .then(function (code) {
      process.exit(code);
    })
    .catch(function (e) {
      console.error(e);
      process.exit(typeof e.exitCode === "number" ? e.exitCode : 1);
    });
}
