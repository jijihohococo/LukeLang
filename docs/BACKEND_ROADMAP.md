# LukeLang Backend Roadmap

> **Wedge:** reactive full-stack web — see [`STRATEGY.md`](./STRATEGY.md).  
> **Track:** Backend (this document) — unlocks full-stack reactivity (likely the true signature).  
> **Prior track:** Frontend ([`FRONTEND_ROADMAP.md`](./FRONTEND_ROADMAP.md))  
> **Master checklist:** [`TaskList.md`](../TaskList.md)

Live Graph (DB → wire → pixel) is the reactive spine; this track is the **HTTP/app framework** that lets you build real servers without hand-rolling every `IF path EQUALS`.

---

## Thesis

Luke apps should declare **routes, binds, and sessions** the same way they declare reactive cells — conversational surface, native Build cost, no glue frameworks.

---

## Gap status

| Gap | Status | Beachhead |
|-----|--------|-----------|
| Parameterized SQL (`?` binds) | ✅ | `dbExecBind` / `dbQueryBind` + `LIST` of TEXT |
| Path params + match | ✅ | `httpMatch path, "/user/:id", params` |
| Method-aware dispatch | ✅ | `SERVE ROUTES ON server WITH n` codegen from `ROUTES` + `HANDLE` |
| Query string → MAP | ✅ | `httpQueryMap` |
| Headers / cookies | ✅ | `httpHeader` / `httpCookie` / `httpSetCookie` |
| JSON body | 🟡 | `httpBody` + `jsonParse` (stdlib sugar later) |
| Form body (`application/x-www-form-urlencoded`) | ✅ | `httpFormMap` + `FORM` / `VALIDATE FORM` → `form_*_error` / `form_*_ok` cells |
| Auth / session / login | 🟡 | `std/auth` — Argon2id, sessions, CSRF, scoped `WATCH`, **SECRET**, **FLOW**, **LIMIT**/`remaining`, **REVEAL**, **WHO SAW SINCE**, **SCRUB TO access** |
| Middleware / filters | 🟡 | `REQUIRE LOGIN` / `REQUIRE CSRF`; `MIDDLEWARE ORDER AUTH THEN RATE LIMIT` (order compile check) |
| Declarative route table syntax | ✅ | `ROUTES` / `HANDLE` / `LINK TO` / `SERVE ROUTES` — broken link / missing HANDLE / SECRET without auth = compile error |
| SSE channel auth / backpressure | 🟡 | `PUSH WATCH` of SECRET requires `FOR CURRENT USER`; scheduler lanes = backpressure story |
| Password reset / 2FA / OAuth | 🟡 | `FLOW` + `VERIFY BY CODE\|TOTP\|OAUTH` wrapper only — **no invented providers** (interop/conformance) |
| Declarative `LIMIT` + reactive remaining | 🟡 | `LIMIT login TO N PER …` + `login.remaining` + `REFRESH LIMIT` |
| Migrations / schema helpers | ✅ | `SCHEMA` / `ENSURE SCHEMA`; `MIGRATION` / `MIGRATE` / `REWIND` + `luke_schema_migrations` |
| C10K concurrency (pool / evented I/O) | ✅ | Worker-side parse + poll accept; keep-alive; timeouts; graceful SIGTERM — see [`DEPLOY.md`](./DEPLOY.md) |
| HTTP/1.1 keep-alive / chunked / streaming | ✅ | keep-alive reuse; `httpChunkOpen`/`httpChunk`/`httpChunkEnd`; SSE unchanged |
| TLS | ✅ | **Reverse proxy** (Caddy/nginx) — no invented in-process TLS; `LUKE_TRUST_PROXY` + `httpClientIp` |

### Auth rules (non-negotiable)

1. **No homegrown crypto** — passwords via libsodium Argon2id (`crypto_pwhash_str` / `_verify`); randomness via `randombytes_buf`; compares via `sodium_memcmp`; audit chain via `crypto_generichash`.
2. **Password = hash, not encryption** — plaintext never stored; “auto encrypt everything” is out of scope (key management is the hard part).
3. **Secure path together** — hash + timing-safe verify + session cookie (HttpOnly, SameSite=Lax; `LUKE_AUTH_SECURE=1` adds Secure) + CSRF. Not hash-only.
4. **Live Graph + auth** — `WATCH … FOR CURRENT USER` binds `user_id = ?` per request (no shared IVM across tenants).
5. **Auth-as-types** — `SECRET` on an unscoped path is a **compile error**; `FLOW` `DONE` without `VERIFY` is a **compile error**; declassify via **`REVEAL`**. See [`AUTH.md`](./AUTH.md).
6. **Whole-stack compile gates (beachheads)** — broken `LINK TO`, SECRET route without auth, middleware order inversion, OAuth without `VERIFY BY OAUTH`, unknown SCHEMA types → **compile error**.

Examples: `auth_unit.luke`, `auth_api.luke`, `auth_scoped.luke`, `auth_secret_ok.luke`, `auth_flow_ok.luke`, `auth_lang_ok.luke`, `backend_lang_ok.luke`, `backend_routes_serve.luke`, `backend_form_errors.luke`, `backend_migrate_ok.luke` (+ negatives `backend_routes_bad_*.luke`, `backend_mw_bad_order.luke`, `backend_flow_oauth_bad.luke`).

---

## War cry surface (this beachhead)

```luke
IMPORT std/server
IMPORT std/sqlite

THIS IS FUNCTION health WITH req AS REQUEST DO
  ASK httpReply WITH req, 200, "text/plain", "ok"
END FUNCTION

THIS IS FUNCTION show_user WITH req AS REQUEST DO
  MY NAME IS path SET TO ASK httpPath WITH req
  MY NAME IS params AS MAP
  ASK httpMatch WITH path, "/user/:id", params
  MY NAME IS id SET TO GET "id" FROM params
  ASK httpReply WITH req, 200, "text/plain", id
END FUNCTION

ROUTES DO
  GET "/ok" HANDLE health
  GET "/user/:id" AS INTEGER HANDLE show_user
END ROUTES

MY NAME IS server SET TO ASK httpListen WITH 8799
SERVE ROUTES ON server WITH 8
```

Form validation feeds reactive cells (BIND in UI without a second declaration):

```luke
FORM login DO
  HAS email AS EMAIL
  HAS password AS PASSWORD
END FORM
VALIDATE FORM login FROM params
# → form_login_email_error / form_login_ok cells
```

Migrations are versioned UP/DOWN SQL (rewind = apply DOWN):

```luke
MIGRATION app DO
  VERSION 1 UP "CREATE TABLE items(id INTEGER PRIMARY KEY)" DOWN "DROP TABLE items"
END MIGRATION
MIGRATE app ON db TO 1
REWIND app ON db TO 0
```

Examples: `examples/build/sql_bind.luke`, `examples/build/backend_api.luke`, `examples/build/backend_routes_serve.luke`.

---

## Sequencing

1. **Secure data path** — parameterized binds (done); migrate demos off string-concat SQL.
2. **Request shape** — match + query map + headers/cookies + form cells (done); richer JSON helpers next.
3. **Session / auth** — opaque sid cookie + DB table (beachhead); **do not invent** OAuth/TOTP providers — wrap textbook flows only.
4. **Declarative routes** — `ROUTES` + `HANDLE` + `SERVE ROUTES` codegen (done); no clever auto-dispatch invention.
5. **Schema migrate/rewind** — `MIGRATION` / `MIGRATE` / `REWIND` (done).
6. **Hardening** — middleware depth, request limits, SSE auth, structured errors.

---

## Related

- [`LIVE_GRAPH.md`](./LIVE_GRAPH.md) — reactive DB→pixel
- [`BUILD_MODE.md`](./BUILD_MODE.md) — `std/server` / `std/sqlite` inventory
- [`STRATEGY.md`](./STRATEGY.md) — why Backend is the next track
- [`TaskList.md`](../TaskList.md) — cross-track checklist
- [`AUTH.md`](./AUTH.md) — auth-as-types / FLOW / SECRET