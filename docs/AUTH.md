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
IMPORT std/auth
IMPORT std/sqlite
IMPORT std/server

ASK authInit WITH db
ASK authCreateAccount WITH db, email, password
ASK authLogin WITH db, req, email, password
REQUIRE LOGIN ON req WITH db
SPEAK THE CURRENT USER
ASK authCheckCsrf WITH db, req
ASK authLogout WITH db, req

WATCH note FROM db AS "SELECT body FROM notes WHERE user_id = ?" FOR CURRENT USER

# Auth-as-types
SECRET REMEMBER ssn AS TEXT
WATCH ssn FROM db AS "SELECT ssn FROM profiles WHERE user_id = ?" FOR CURRENT USER
BIND "ssn" TO ssn
WHO SAW ssn
WHO SAW ssn SINCE "last week"
SCRUB TO access OF ssn
REVOKE ACCESS

# Declarative FLOW — VERIFY before DONE or compile error
FLOW signup DO
  COLLECT email, password
  VERIFY email BY CODE
  DONE → CREATE ACCOUNT
END FLOW
ADVANCE FLOW signup
CREATE ACCOUNT FROM FLOW signup WITH db

# Rate-limit as language policy + reactive remaining cell
LIMIT login TO 5 PER MINUTE PER ip
REFRESH LIMIT login WITH db, email
BIND "attempts_left" TO login.remaining

# Sole declassification escape
REVEAL last 4 OF ssn AS masked
BIND "masked" TO masked
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
