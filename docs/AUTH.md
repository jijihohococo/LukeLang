# LukeLang Auth

> **Status:** secure-path beachhead (libsodium only)  
> **Track:** [`BACKEND_ROADMAP.md`](./BACKEND_ROADMAP.md) · [`TaskList.md`](../TaskList.md)

## Rules

1. **No homegrown crypto.** Passwords: libsodium **Argon2id** (`crypto_pwhash_str` / `crypto_pwhash_str_verify`). Random: `randombytes_buf`. CSRF compare: `sodium_memcmp`.
2. **Password hashing ≠ encryption.** We store hashes only. Field-level / at-rest encryption is a separate, explicit decision (key management).
3. **Ship the whole path together:** hash + verify + HttpOnly/SameSite session cookie + CSRF. Hash-only is worse than nothing (false confidence).
4. **Live Graph must be permission-scoped.** Unscoped `WATCH` + push = data leak. Use `WATCH … FOR CURRENT USER` (binds `user_id = ?`; no shared IVM across tenants).

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
```

Production HTTPS: set `LUKE_AUTH_SECURE=1` so session cookies also get `Secure`.

## Still open

Password reset, rate-limit UX, 2FA, OAuth, account enumeration polish beyond dummy-hash verify, declarative `CREATE ACCOUNT WITH PASSWORD`.
