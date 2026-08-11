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
| Method-aware dispatch | 🟡 | `httpMethod` + `httpMatch` in handler (declarative table later) |
| Query string → MAP | ✅ | `httpQueryMap` |
| Headers / cookies | ✅ | `httpHeader` / `httpCookie` / `httpSetCookie` |
| JSON body | 🟡 | `httpBody` + `jsonParse` (stdlib sugar later) |
| Form body (`application/x-www-form-urlencoded`) | ⬜ | |
| Auth / session / login | 🟡 | opaque cookie session + SQLite store example |
| Middleware / filters | ⬜ | |
| Declarative route table syntax | ⬜ | |
| SSE channel auth / backpressure | ⬜ | see Live Graph wire hardening |
| Migrations / schema helpers | ⬜ | |

---

## War cry surface (this beachhead)

```luke
IMPORT std/server
IMPORT std/sqlite
IMPORT std/json

THIS IS FUNCTION handle WITH req AS REQUEST DO
  MY NAME IS method SET TO ASK httpMethod WITH req
  MY NAME IS path SET TO ASK httpPath WITH req
  MY NAME IS params AS MAP

  IF method EQUALS "GET" DO
    IF ASK httpMatch WITH path, "/user/:id", params DO
      MY NAME IS id SET TO GET "id" FROM params
      MY NAME IS q SET TO ASK httpQueryMap WITH req
      MY NAME IS binds AS LIST
      ADD id TO binds
      MY NAME IS name SET TO ASK dbQueryBind WITH db, "SELECT name FROM users WHERE id = ?", binds
      ASK httpReply WITH req, 200, "text/plain", name
    END IF
  END IF

  IF method EQUALS "POST" DO
    IF ASK httpMatch WITH path, "/login", params DO
      MY NAME IS body SET TO ASK httpJson WITH req
      // … verify, then:
      ASK httpSetCookie WITH req, "luke_sid", sid
      ASK httpReply WITH req, 200, "text/plain", "ok"
    END IF
  END IF
END FUNCTION
```

Examples: `examples/build/sql_bind.luke`, `examples/build/backend_api.luke`.

---

## Sequencing

1. **Secure data path** — parameterized binds (done); migrate demos off string-concat SQL.
2. **Request shape** — match + query map + headers/cookies (done); form + richer JSON helpers next.
3. **Session / auth** — opaque sid cookie + DB table (beachhead); password hashing / OAuth later.
4. **Declarative routes** — `WHEN GET "/user/:id" DO` or route table IR (stops `IF` trees).
5. **Hardening** — middleware, request limits, SSE auth, structured errors.

---

## Related

- [`LIVE_GRAPH.md`](./LIVE_GRAPH.md) — reactive DB→pixel
- [`BUILD_MODE.md`](./BUILD_MODE.md) — `std/server` / `std/sqlite` inventory
- [`STRATEGY.md`](./STRATEGY.md) — why Backend is the next track
- [`TaskList.md`](../TaskList.md) — cross-track checklist
