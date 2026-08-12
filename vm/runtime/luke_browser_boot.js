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
  var FOCUSABLE =
    'a[href],button:not([disabled]),textarea,input:not([disabled]),select,[tabindex]:not([tabindex="-1"])';
  function closeModalById(id) {
    if (typeof document === "undefined") {
      console.log("[argus_modal_close]", id);
      return;
    }
    var el = document.getElementById(id);
    if (el && el.__lukeModalHandler) {
      el.removeEventListener("keydown", el.__lukeModalHandler);
      el.__lukeModalHandler = null;
    }
    if (el) {
      el.style.display = "none";
      el.setAttribute("aria-hidden", "true");
    }
    if (!globalThis.__lukeModalStack) return;
    if (!Object.prototype.hasOwnProperty.call(globalThis.__lukeModalStack, id)) return;
    var prev = globalThis.__lukeModalStack[id];
    delete globalThis.__lukeModalStack[id];
    if (prev && prev.focus) {
      try {
        prev.focus();
      } catch (e) {}
    }
  }
  function openModalById(id) {
    if (typeof document === "undefined") {
      console.log("[argus_modal_open]", id);
      return;
    }
    var el = document.getElementById(id);
    if (!el) return;
    el.style.display = "";
    el.removeAttribute("aria-hidden");
    if (!globalThis.__lukeModalStack) globalThis.__lukeModalStack = {};
    if (!Object.prototype.hasOwnProperty.call(globalThis.__lukeModalStack, id))
      globalThis.__lukeModalStack[id] = document.activeElement || null;
    el.setAttribute("tabindex", "-1");
    var focusable = el.querySelectorAll(FOCUSABLE);
    var first = focusable.length ? focusable[0] : el;
    try {
      first.focus();
    } catch (e0) {}
    if (el.__lukeModalHandler) el.removeEventListener("keydown", el.__lukeModalHandler);
    el.__lukeModalHandler = function (e) {
      if (e.key === "Escape") {
        closeModalById(id);
        return;
      }
      if (e.key !== "Tab") return;
      var nodes = el.querySelectorAll(FOCUSABLE);
      if (!nodes.length) {
        e.preventDefault();
        return;
      }
      var f = nodes[0];
      var l = nodes[nodes.length - 1];
      if (e.shiftKey && document.activeElement === f) {
        e.preventDefault();
        l.focus();
      } else if (!e.shiftKey && document.activeElement === l) {
        e.preventDefault();
        f.focus();
      }
    };
    el.addEventListener("keydown", el.__lukeModalHandler);
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
    subscribe_start: function (idPtr, idLen, urlPtr, urlLen) {
      var id = readText(idPtr, idLen);
      var url = readText(urlPtr, urlLen);
      if (!globalThis.__lukeSubscribeJobs) globalThis.__lukeSubscribeJobs = {};
      if (!globalThis.__lukeSubscribeHandles) globalThis.__lukeSubscribeHandles = {};
      /* Close any prior handle for this id before restarting. */
      try {
        var prev = globalThis.__lukeSubscribeHandles[id];
        if (prev) {
          if (typeof prev.close === "function") prev.close();
          else if (typeof prev.destroy === "function") prev.destroy();
          else if (typeof prev.abort === "function") prev.abort();
        }
      } catch (e0) {}
      globalThis.__lukeSubscribeJobs[id] = {
        ready: false,
        body: "",
        state: "connecting",
        retryMs: 500,
        lastId: null,
      };
      console.log("[subscribe_start]", id, url);
      function onMessage(data, eventId) {
        var job = globalThis.__lukeSubscribeJobs[id] || {};
        var nextId = eventId == null ? job.lastId : eventId;
        /* Ignore out-of-order / duplicate ids (reconnect resume, proxy buffering). */
        if (nextId != null && job.lastId != null) {
          var ni = parseInt(String(nextId), 10);
          var pi = parseInt(String(job.lastId), 10);
          if (!isNaN(ni) && !isNaN(pi) && ni <= pi) return;
        }
        job.ready = true;
        job.body = data == null ? "" : String(data);
        job.state = "open";
        job.retryMs = 500;
        if (eventId != null) job.lastId = eventId;
        globalThis.__lukeSubscribeJobs[id] = job;
        console.log("[subscribe_ready]", id);
        if (typeof globalThis.__lukeDispatchSubscribe === "function")
          globalThis.__lukeDispatchSubscribe(id);
      }
      function parseSseChunk(buf, chunk) {
        buf += chunk;
        var parts = buf.split(/\r?\n\r?\n/);
        buf = parts.pop() || "";
        for (var i = 0; i < parts.length; i++) {
          var lines = parts[i].split(/\r?\n/);
          var dataLines = [];
          var curId = null;
          for (var j = 0; j < lines.length; j++) {
            var line = lines[j];
            if (line.indexOf(":") === 0) continue; /* comment / heartbeat */
            if (line.indexOf("id:") === 0) {
              curId = line.slice(3).trim();
              continue;
            }
            if (line.indexOf("data:") === 0) dataLines.push(line.slice(5).replace(/^\s/, ""));
            else if (line.indexOf("retry:") === 0) {
              var ms = parseInt(line.slice(6).trim(), 10);
              if (ms > 0 && globalThis.__lukeSubscribeJobs[id])
                globalThis.__lukeSubscribeJobs[id].retryMs = ms;
            }
          }
          if (dataLines.length) onMessage(dataLines.join("\n"), curId);
        }
        return buf;
      }
      if (typeof EventSource !== "undefined") {
        try {
          var es = new EventSource(url);
          es.onmessage = function (ev) {
            onMessage(ev.data, ev.lastEventId || null);
          };
          es.onerror = function () {
            console.log("[subscribe_error]", id);
            if (globalThis.__lukeSubscribeJobs[id])
              globalThis.__lukeSubscribeJobs[id].state = "error";
          };
          globalThis.__lukeSubscribeHandles[id] = es;
          return;
        } catch (e) {
          console.log("[subscribe_eventsource_fail]", e);
        }
      }
      /* Node / no-EventSource: HTTP SSE reader with reconnect + idle timeout */
      try {
        var http = typeof require === "function" ? require("http") : null;
        var https = typeof require === "function" ? require("https") : null;
        if (!http) {
          console.log("[subscribe_stub]", id);
          return;
        }
        var closed = false;
        var idleTimer = null;
        var IDLE_MS = 15000;
        function clearIdle() {
          if (idleTimer) clearTimeout(idleTimer);
          idleTimer = null;
        }
        function bumpIdle(req) {
          clearIdle();
          idleTimer = setTimeout(function () {
            console.log("[subscribe_idle_timeout]", id);
            try {
              req.destroy();
            } catch (e3) {}
          }, IDLE_MS);
        }
        function connect() {
          if (closed) return;
          var u = new URL(url);
          var lib = u.protocol === "https:" ? https : http;
          var job = globalThis.__lukeSubscribeJobs[id] || {};
          var lastId = job.lastId;
          var headers = {};
          if (lastId != null) headers["Last-Event-ID"] = String(lastId);
          var req = lib.get(
            {
              hostname: u.hostname,
              port: u.port,
              path: u.pathname + (u.search || ""),
              timeout: 10000,
              headers: headers,
            },
            function (res) {
              var buf = "";
              res.setEncoding("utf8");
              bumpIdle(req);
              res.on("data", function (chunk) {
                bumpIdle(req);
                buf = parseSseChunk(buf, chunk);
              });
              res.on("end", function () {
                clearIdle();
                console.log("[subscribe_end]", id);
                scheduleReconnect();
              });
            }
          );
          req.on("timeout", function () {
            console.log("[subscribe_connect_timeout]", id);
            req.destroy();
          });
          req.on("error", function () {
            clearIdle();
            console.log("[subscribe_http_error]", id);
            scheduleReconnect();
          });
          globalThis.__lukeSubscribeHandles[id] = {
            close: function () {
              closed = true;
              clearIdle();
              try {
                req.destroy();
              } catch (e4) {}
            },
            destroy: function () {
              this.close();
            },
          };
        }
        function scheduleReconnect() {
          if (closed) return;
          var job = globalThis.__lukeSubscribeJobs[id] || {};
          job.state = "reconnect";
          var delay = job.retryMs || 500;
          if (delay > 8000) delay = 8000;
          job.retryMs = Math.min((job.retryMs || 500) * 2, 8000);
          globalThis.__lukeSubscribeJobs[id] = job;
          console.log("[subscribe_reconnect]", id, delay);
          setTimeout(connect, delay);
        }
        connect();
      } catch (e2) {
        console.log("[subscribe_fail]", e2);
      }
    },
    subscribe_ready: function (idPtr, idLen) {
      var id = readText(idPtr, idLen);
      var job = globalThis.__lukeSubscribeJobs && globalThis.__lukeSubscribeJobs[id];
      return job && job.ready ? 1 : 0;
    },
    subscribe_body: function (idPtr, idLen, outPtr, outCap, outLenPtr) {
      var id = readText(idPtr, idLen);
      var job = globalThis.__lukeSubscribeJobs && globalThis.__lukeSubscribeJobs[id];
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
          el.setAttribute("tabindex", "-1");
          el.setAttribute("aria-hidden", "true");
          el.style.display = "none"; /* closed until OPEN THE MODAL */
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
      el.style.position = "absolute";
      el.style.left = x + "px";
      el.style.top = y + "px";
      el.style.width = w + "px";
      el.style.height = h + "px";
      el.style.opacity = String(opacity);
      el.style.flex = "";
      el.style.minWidth = "";
      el.style.minHeight = "";
    },
    argus_parent: function (idPtr, idLen, parentPtr, parentLen) {
      const id = readText(idPtr, idLen);
      const parentId = readText(parentPtr, parentLen);
      if (typeof document === "undefined") {
        console.log("[argus_parent]", id, parentId);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      var parent = parentId ? document.getElementById(parentId) : document.getElementById("root");
      if (!parent) parent = document.getElementById("root");
      if (!parent) return;
      if (el.parentNode !== parent) parent.appendChild(el);
    },
    argus_flex: function (idPtr, idLen, dir, gap, pad, align, cross, wrap) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_flex]", id, dir, gap, pad, align, cross, wrap);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      var axis = dir | 0;
      var alignV = align | 0;
      var crossV = cross | 0;
      var toCss = function (v) {
        return v === 1 ? "center" : v === 2 ? "flex-end" : "flex-start";
      };
      el.style.display = "flex";
      el.style.flexDirection = axis === 2 ? "row" : "column";
      el.style.flexWrap = wrap ? "wrap" : "nowrap";
      el.style.gap = (gap || 0) + "px";
      el.style.padding = (pad || 0) + "px";
      el.style.justifyContent = toCss(alignV);
      el.style.alignItems = toCss(crossV);
      el.style.boxSizing = "border-box";
    },
    argus_flow_frame: function (idPtr, idLen, w, h, opacity, grow) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_flow_frame]", id, w, h, opacity, grow);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.style.position = "relative";
      el.style.left = "";
      el.style.top = "";
      el.style.opacity = String(opacity);
      el.style.minWidth = "0";
      el.style.boxSizing = "border-box";
      if (grow) {
        el.style.flex = "1 1 auto";
        el.style.width = w > 0 ? w + "px" : "auto";
        el.style.height = h > 0 ? h + "px" : "auto";
        el.style.maxWidth = "100%";
      } else {
        el.style.flex = "0 0 auto";
        el.style.width = w + "px";
        el.style.height = h + "px";
      }
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
      if (role === "dialog") {
        el.setAttribute("aria-modal", "true");
        if (!el.hasAttribute("tabindex")) el.setAttribute("tabindex", "-1");
      }
    },
    argus_focus_trap: function (idPtr, idLen) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_focus_trap]", id);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      if (!globalThis.__lukeFocusStack) globalThis.__lukeFocusStack = [];
      globalThis.__lukeFocusStack.push(document.activeElement || null);
      var focusable = el.querySelectorAll(FOCUSABLE);
      var first = focusable.length ? focusable[0] : el;
      if (!el.hasAttribute("tabindex")) el.setAttribute("tabindex", "-1");
      try {
        first.focus();
      } catch (e) {}
      if (el.__lukeTrapHandler) el.removeEventListener("keydown", el.__lukeTrapHandler);
      el.__lukeTrapHandler = function (e) {
        if (e.key === "Escape") {
          if (globalThis.__lukeFocusStack && globalThis.__lukeFocusStack.length) {
            var prev = globalThis.__lukeFocusStack.pop();
            if (prev && prev.focus) try { prev.focus(); } catch (err) {}
          }
          return;
        }
        if (e.key !== "Tab") return;
        var nodes = el.querySelectorAll(FOCUSABLE);
        if (!nodes.length) {
          e.preventDefault();
          return;
        }
        var f = nodes[0];
        var l = nodes[nodes.length - 1];
        if (e.shiftKey && document.activeElement === f) {
          e.preventDefault();
          l.focus();
        } else if (!e.shiftKey && document.activeElement === l) {
          e.preventDefault();
          f.focus();
        }
      };
      el.addEventListener("keydown", el.__lukeTrapHandler);
    },
    argus_focus_restore: function () {
      if (typeof document === "undefined") {
        console.log("[argus_focus_restore]");
        return;
      }
      if (!globalThis.__lukeFocusStack || !globalThis.__lukeFocusStack.length) return;
      var prev = globalThis.__lukeFocusStack.pop();
      if (prev && prev.focus) {
        try {
          prev.focus();
        } catch (e) {}
      }
    },
    argus_announce: function (textPtr, textLen) {
      const text = readText(textPtr, textLen);
      if (typeof document === "undefined") {
        console.log("[argus_announce]", text);
        return;
      }
      var live = document.getElementById("__luke_live");
      if (!live) {
        live = document.createElement("div");
        live.id = "__luke_live";
        live.setAttribute("role", "status");
        live.setAttribute("aria-live", "polite");
        live.setAttribute("aria-atomic", "true");
        live.style.position = "absolute";
        live.style.width = "1px";
        live.style.height = "1px";
        live.style.overflow = "hidden";
        live.style.clip = "rect(0 0 0 0)";
        document.body.appendChild(live);
      }
      live.textContent = "";
      setTimeout(function () {
        live.textContent = text;
      }, 20);
    },
    argus_modal_open: function (idPtr, idLen) {
      const id = readText(idPtr, idLen);
      openModalById(id);
    },
    argus_modal_close: function (idPtr, idLen) {
      const id = readText(idPtr, idLen);
      closeModalById(id);
    },
    argus_live: function (idPtr, idLen, level) {
      const id = readText(idPtr, idLen);
      var lvl = level | 0;
      if (typeof document === "undefined") {
        console.log("[argus_live]", id, lvl);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      if (lvl === 2) el.setAttribute("aria-live", "assertive");
      else if (lvl === 1) el.setAttribute("aria-live", "polite");
      else return;
      el.setAttribute("aria-atomic", "true");
    },
    argus_class: function (idPtr, idLen, clsPtr, clsLen) {
      const id = readText(idPtr, idLen);
      const classes = readText(clsPtr, clsLen);
      if (typeof document === "undefined") {
        console.log("[argus_class]", id, classes);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      el.setAttribute("class", classes);
    },
    argus_scroll: function (idPtr, idLen, on) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_scroll]", id, on);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      if (on) el.style.overflow = "auto";
    },
    argus_grid: function (idPtr, idLen, cols, gap, pad) {
      const id = readText(idPtr, idLen);
      if (typeof document === "undefined") {
        console.log("[argus_grid]", id, cols, gap, pad);
        return;
      }
      var el = document.getElementById(id);
      if (!el) return;
      var n = cols | 0;
      if (n < 1) n = 1;
      el.style.display = "grid";
      el.style.gridTemplateColumns = "repeat(" + n + ", 1fr)";
      el.style.gap = (gap || 0) + "px";
      el.style.padding = (pad || 0) + "px";
      el.style.boxSizing = "border-box";
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
      if (typeof globalThis.__lukeViewportWidth === "number") return globalThis.__lukeViewportWidth;
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
  function dispatchSubscribe(jobId) {
    console.log("[subscribe_dispatch]", jobId);
    dispatch(jobId, "subscribe");
  }
  function dispatchViewport() {
    if (typeof instance.exports.luke_viewport_relayout === "function") {
      try {
        instance.exports.luke_viewport_relayout();
      } catch (e) {
        console.error(e);
      }
    }
    var width = 1280;
    if (typeof globalThis.__lukeViewportWidth === "number") width = globalThis.__lukeViewportWidth;
    else if (typeof window !== "undefined" && window.innerWidth) width = window.innerWidth;
    for (var i = 0; i < whens.length; i++) {
      var w = whens[i];
      if ((w.event || "") !== "viewport") continue;
      var id = w.id || "";
      if (id.indexOf("below:") === 0) {
        var belowN = parseFloat(id.slice("below:".length));
        if (!isNaN(belowN) && width < belowN) runExport(w.export);
      } else if (id.indexOf("above:") === 0) {
        var aboveN = parseFloat(id.slice("above:".length));
        if (!isNaN(aboveN) && width > aboveN) runExport(w.export);
      } else if (!id) {
        runExport(w.export);
      }
    }
  }
  function parseBreakpointQuery(spec) {
    /* id encodes min:N, max:N, or min:A:max:B */
    var min = null;
    var max = null;
    if (!spec) return null;
    var parts = String(spec).split(":");
    for (var i = 0; i + 1 < parts.length; i += 2) {
      if (parts[i] === "min") min = parseFloat(parts[i + 1]);
      if (parts[i] === "max") max = parseFloat(parts[i + 1]);
    }
    var q = [];
    if (min != null && !isNaN(min)) q.push("(min-width: " + min + "px)");
    if (max != null && !isNaN(max)) q.push("(max-width: " + max + "px)");
    return q.length ? q.join(" and ") : null;
  }
  function dispatchBreakpoint(spec) {
    for (var i = 0; i < whens.length; i++) {
      var w = whens[i];
      if ((w.event || "") === "breakpoint" && (w.id || "") === spec) runExport(w.export);
    }
  }
  function wireBreakpoints() {
    if (typeof window === "undefined" || !window.matchMedia) return;
    for (var i = 0; i < whens.length; i++) {
      var w = whens[i];
      if ((w.event || "") !== "breakpoint") continue;
      var mq = parseBreakpointQuery(w.id || "");
      if (!mq) continue;
      (function (spec, query) {
        var mql = window.matchMedia(query);
        var fire = function () {
          if (mql.matches) dispatchBreakpoint(spec);
        };
        if (mql.addEventListener) mql.addEventListener("change", fire);
        else if (mql.addListener) mql.addListener(fire);
        fire();
      })(w.id || "", mq);
    }
  }
  globalThis.__lukeDispatchRoute = dispatchRoute;
  globalThis.__lukeDispatchFetch = dispatchFetch;
  globalThis.__lukeDispatchSubscribe = dispatchSubscribe;
  globalThis.__lukeDispatchViewport = dispatchViewport;
  globalThis.__lukeDispatchBreakpoint = dispatchBreakpoint;
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
  wireBreakpoints();
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
    console.error("usage: luke_browser_loader.cjs <file.wasm> [--dispatch id:event]");
    process.exit(1);
  }
  var fs = require("fs");
  var dispatchSpec = null;
  for (var ai = 3; ai < process.argv.length; ai++) {
    if (process.argv[ai] === "--dispatch" && process.argv[ai + 1]) {
      dispatchSpec = process.argv[++ai];
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
      process.exit((res && res.code) || 0);
    })
    .catch(function (e) {
      console.error(e);
      process.exit(typeof e.exitCode === "number" ? e.exitCode : 1);
    });
}
