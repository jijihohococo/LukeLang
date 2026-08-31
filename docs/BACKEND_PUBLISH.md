# Backend-first publish plan (LukeLang)

LukeLang's first market surface is **backend language + reactive full-stack substrate**, not generic scripting.

## Positioning sentence

Build backend services with a familiar technical syntax and systems-level runtime cost:

- native binaries (`luke BUILD`)
- no GC in shipped path
- built-in HTTP, SQL bind APIs, auth primitives, reactive graph

## What is already production-facing

- `std/server`: HTTP listen/serve, route matching, query/header/cookie/body helpers
- `std/sqlite`: pooled connections + statement cache + bind APIs
- `std/pg`: Postgres path with bind APIs and Slipstream execution
- `std/auth`: login/session/CSRF helpers
- `WATCH`/`PUSH WATCH`: Live Graph bridge

Examples:

- `examples/build/backend_api.luke`
- `examples/build/backend_form_errors.luke`
- `examples/build/pg_api.luke`

## Publish checklist for backend adoption

1. **Stable commands**
   - `luke BUILD <app.luke> -o <bin>`
   - `luke TEST <test.luke>`
2. **Strong diagnostics**
   - parser/type errors should point to Luke source lines with actionable text
3. **Canonical backend templates**
   - route handler, auth/session flow, DB bind examples
4. **Editor tooling**
   - syntax highlighting, snippets, keyword completion, LSP/DAP integration
5. **Operational docs**
   - deploy topology, TLS reverse proxy, env vars, resource knobs

## Minimal backend demo commands

From repo root:

```bash
cd vm && make
./build/luke BUILD ../examples/build/backend_api.luke -o build/backend_api
./build/backend_api
```

Then hit:

- `GET /ok`
- `GET /user/1?tag=demo`
- `POST /login` with JSON body

## Guardrails while publishing

- Keep Build path as canonical truth.
- Do not regress no-GC guarantees in native backend binaries.
- Prefer adding diagnostics/tests over speculative syntax churn.
