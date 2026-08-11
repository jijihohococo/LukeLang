# LukeLang Auth

> **Status:** secure-path beachhead + **auth-as-types** narrow spike  
> **Track:** [`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md) · [`TaskList.md`](../TaskList.md)

## Thesis — auth as a language property

Auth is not a library bolt-on. The compiler knows the DB→pixel graph; the Live Graph is reactive; causal history already exists for time-travel. Exploit those three:

| Pillar | Killer capability |
|--------|-------------------|
| Compiler-known graph | **Auth-as-types** — unauthorized access = **compile error** (information-flow / SECRET) |
| Reactive engine | **Live revoke** — `REVOKE ACCESS` clears session + SECRET cells; pixels go blank |
| Time-travel / causal log | **Free audit** — every SECRET touch → `WHO SAW` trail |

> In LukeLang: unauthorized access is a compile error; permission change live-revokes data; access can be free audit — **secure by compiler**, not only secure by default library checks.

Password hashing is table-stakes. This is the differentiator.

## Rules

1. **No homegrown crypto.** Passwords: libsodium **Argon2id** (`crypto_pwhash_str` / `crypto_pwhash_str_verify`). Random: `randombytes_buf`. CSRF compare: `sodium_memcmp`.
2. **Password hashing ≠ encryption.** We store hashes only. Field-level / at-rest encryption is a separate, explicit decision (key management).
3. **Ship the whole path together:** hash + verify + HttpOnly/SameSite session cookie + CSRF. Hash-only is worse than nothing (false confidence).
4. **Live Graph must be permission-scoped.** Unscoped `WATCH` + push = data leak. Use `WATCH … FOR CURRENT USER` (binds `user_id = ?`; no shared IVM across tenants).
5. **SECRET is information-flow, not OOP `PRIVATE`.** Binding or watching SECRET data without a CURRENT-USER scope is a **compile error**.

## Surface

```luke
IMPORT std/auth
IMPORT std/sqlite
IMPORT std/server

ASK authInit WITH db
ASK authCreateAccount WITH db, email, password
ASK authLogin WITH db, req, email, password   # sets luke_sid cookie; returns csrf via authCsrf
REQUIRE LOGIN ON req WITH db
SPEAK THE CURRENT USER
ASK authCheckCsrf WITH db, req                # X-CSRF-Token header
ASK authLogout WITH db, req

WATCH note FROM db AS "SELECT body FROM notes WHERE user_id = ?" FOR CURRENT USER

# Auth-as-types spike
SECRET REMEMBER ssn AS TEXT
WATCH ssn FROM db AS "SELECT ssn FROM profiles WHERE user_id = ?" FOR CURRENT USER
BIND "ssn" TO ssn                             # OK — scoped
WHO SAW ssn                                   # audit trail
REVOKE ACCESS                                 # live clear CURRENT USER + SECRET cells

ASK authAttemptsLeft WITH db, email           # reactive rate-limit UX beachhead
```

Production HTTPS: set `LUKE_AUTH_SECURE=1` so session cookies also get `Secure`.

`ASK authAssume WITH uid` sets `THE CURRENT USER` without HTTP — **CLI/tests only**; production uses login / `REQUIRE LOGIN`.

### Compile-error examples

- `examples/build/auth_secret_bad_watch.luke` — SECRET + unscoped `WATCH` → reject  
- `examples/build/auth_secret_bad_bind.luke` — SECRET + `BIND` without `FOR CURRENT USER` → reject  
- `examples/build/auth_secret_ok.luke` — scoped path + audit + revoke + attempts-left

## Research-grade (crown jewel) vs spike

Full information-flow typing (Jif/LIO-style) over every DB→pixel path is the long vision. **Now:** narrow lint→compile-error — `SECRET` cell/field + `BIND` / unscoped `WATCH` rejected unless `WATCH … FOR CURRENT USER` scopes it.

## Next ergonomics (Live Graph ready)

| Item | Status | Notes |
|------|--------|-------|
| Declarative `FLOW signup` (2FA/OAuth/reset as state machines) | ⬜ | correct-by-construction expiry / rate-limit |
| Declarative `LIMIT login TO 5 PER MINUTE PER ip` + `BIND` remaining | 🟡 | `authAttemptsLeft` beachhead; full LIMIT syntax later |
| Persist audit into causal/time-travel log | 🟡 | in-memory `WHO SAW` ring now |
| Deeper IFC (label lattices, declassification) | ⬜ | research track |

## Still open

Password reset, 2FA, OAuth, account enumeration polish beyond dummy-hash verify, declarative `CREATE ACCOUNT WITH PASSWORD` / `FLOW`.
