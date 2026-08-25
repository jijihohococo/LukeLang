# LukeLang Auth

> **Status:** secure-path beachhead + **auth-as-types** + FLOW / LIMIT / REVEAL / rewind-audit spikes  
> **Track:** [`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md) · [`TaskList.md`](../TaskList.md)

## Thesis — auth as a language property

Auth is not a library bolt-on. The compiler knows the DB→pixel graph; the Live Graph is reactive; causal history already exists for time-travel. Exploit those three:

| Pillar | Killer capability |
|--------|-------------------|
| Compiler-known graph | **Auth-as-types** + **FLOW totality** + **REVEAL** — unauthorized / impossible states = **compile error** |
| Reactive engine | **Live revoke** + **LIMIT.remaining** push + resumable FLOW cells |
| Time-travel / causal log | **WHO SAW** / **SCRUB TO access** — rewind toward the breach; hash-chained audit ring |

> In LukeLang: unauthorized access is a compile error; auth flow impossible states are compile errors; rate-limit remaining is reactive; breach access can be scrubbed; secret egress is `REVEAL` only — **secure by compiler**.

Password hashing is table-stakes. This is the differentiator.

## Rules

1. **No homegrown crypto.** Passwords: libsodium **Argon2id** (`crypto_pwhash_str` / `crypto_pwhash_str_verify`). Random: `randombytes_buf`. CSRF compare: `sodium_memcmp`. Audit chain: `crypto_generichash`.
2. **Password hashing ≠ encryption.** We store hashes only. Field-level / at-rest encryption is a separate, explicit decision (key management).
3. **Ship the whole path together:** hash + verify + HttpOnly/SameSite session cookie + CSRF. Hash-only is worse than nothing (false confidence).
4. **Live Graph must be permission-scoped.** Unscoped `WATCH` + push = data leak. Use `WATCH … FOR CURRENT USER` (binds `user_id = ?`; no shared IVM across tenants).
5. **SECRET is information-flow, not OOP `PRIVATE`.** Binding or watching SECRET data without a CURRENT-USER scope is a **compile error**.
6. **Declassify only via `REVEAL`.** Masked / partial egress is an auditable statement — greppable, logged.

## Surface

```luke
import std/auth
import std/sqlite
import std/server
authInit(db)
authCreateAccount(db, email, password)
authLogin(db, req, email, password)
require login on req with db
print(current_user)
authCheckCsrf(db, req)
authLogout(db, req)
watch note from db as "SELECT body FROM notes WHERE user_id = ?" for current_user
raw "# Auth-as-types"
secret signal ssn: str
watch ssn from db as "SELECT ssn FROM profiles WHERE user_id = ?" for current_user
bind("ssn", ssn)
raw "WHO SAW ssn"
raw "WHO SAW ssn SINCE \"last week\""
raw "SCRUB TO access OF ssn"
raw "REVOKE ACCESS"
raw "# Declarative FLOW — VERIFY before DONE or compile error"
raw "FLOW signup DO"
raw "COLLECT email, password"
raw "VERIFY email BY CODE"
raw "DONE → CREATE ACCOUNT"
raw "END FLOW"
raw "ADVANCE FLOW signup"
raw "CREATE ACCOUNT FROM FLOW signup WITH db"
raw "# Rate-limit as language policy + reactive remaining cell"
raw "LIMIT login TO 5 PER MINUTE PER ip"
raw "REFRESH LIMIT login WITH db, email"
bind("attempts_left", login.remaining)
raw "# Sole declassification escape"
raw "REVEAL last 4 OF ssn AS masked"
bind("masked", masked)
```

Production HTTPS: set `LUKE_AUTH_SECURE=1` so session cookies also get `Secure`.

`ASK authAssume WITH uid` sets `THE CURRENT USER` without HTTP — **CLI/tests only**.

`ASK authSawVerify` checks the in-memory audit hash chain (tamper-evident beachhead).

### Compile-error examples

| File | Rejects |
|------|---------|
| `auth_secret_bad_watch.luke` | SECRET + unscoped `WATCH` |
| `auth_secret_bad_bind.luke` | SECRET + `BIND` without `FOR CURRENT USER` |
| `auth_flow_bad.luke` | `FLOW` `DONE` without `VERIFY` |

### Happy-path examples

`auth_unit.luke`, `auth_api.luke`, `auth_scoped.luke`, `auth_secret_ok.luke`, `auth_flow_ok.luke`, `auth_lang_ok.luke` (LIMIT + REVEAL + SINCE + SCRUB).

## Research-grade vs spike

| Vision | Spike now |
|--------|-----------|
| Full IFC lattices + indirect flows | `SECRET` + scoped `WATCH`/`BIND`; `REVEAL last N` |
| FLOW proves every path through VERIFY | Declaration totality (`DONE` without `VERIFY` → compile error) + `ADVANCE` / `CREATE ACCOUNT FROM FLOW` |
| Distributed LIMIT via Live Graph | `LIMIT` + `login.remaining` cell + `REFRESH LIMIT` (email key; `PER ip` accepted, shared counter later) |
| Rewind causal DB→pixel chain | `WHO SAW SINCE` + `SCRUB TO access` on hash-chained saw ring; IVM scrub still separate |
| Tamper-evident compliance log | `crypto_generichash` chain + `authSawVerify` |

## Still open

Password reset, real 2FA/OAuth code verify, `PER ip` enforcement, persist audit into IVM causal log, full label lattices, DevTools↔server scrub.
