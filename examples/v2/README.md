# Syntax v2 golden corpus

Hand-written v2 twins of ten representative programs. These are the **acceptance tests for
Phase 2** of [`docs/SYNTAX_V2_PLAN.md`](../../docs/SYNTAX_V2_PLAN.md) and the executable examples
for [`docs/SYNTAX_V2_SPEC.md`](../../docs/SYNTAX_V2_SPEC.md).

Phase 4 also landed auto-migrated `.lk` copies beside every `examples/build/*.luke` file. This
directory remains the **hand-written** golden set (not mechanically regenerated).

| v2 file | v1 twin | Covers |
| --- | --- | --- |
| `hello.lk` | `examples/build/hello.luke` | print, let, concat |
| `functions.lk` | `examples/build/functions.luke` | `fn`, params, return, calls |
| `oop.lk` | `examples/build/oop.luke` | `struct`, inheritance, `init`, `super` |
| `collections.lk` | `examples/build/collections.luke` | list/map, `try`/`catch`/`throw` |
| `collections_test.lk` | `examples/build/collections_test.luke` | `test`, `assert` |
| `reactive_core.lk` | `examples/build/reactive_core.luke` | `signal`, `derived`, `batch` |
| `live_graph_server.lk` | `examples/build/live_graph_server.luke` | `watch … from`, `push watch`, `while` |
| `backend_api.lk` | `examples/build/backend_api.luke` | routing, bind SQL, JSON, cookies |
| `auth_api.lk` | `examples/build/auth_api.luke` | `require login`, `current_user`, CSRF |
| `frontend_widgets.lk` | `examples/build/frontend_widgets.luke` | layout/UI — **provisional**, spec §6 |

## Rules these files follow

1. **Semantics identical to the twin.** The Phase 2 gate is byte-identical stdout, so control
   flow, evaluation order, and side effects must match exactly.
2. **String literals are never translated.** `print("still here after ATTEMPT")` keeps the word
   `ATTEMPT` — output is compared byte for byte, so touching literals breaks the gate. Only
   *syntax* is translated, never data.
3. **Identifiers keep their v1 spelling.** `httpReply`, `dbQueryBind`, `authCsrf` are stdlib API
   names, not syntax. Renaming them is a separate breaking change.
4. **`let` vs `var` follows mutation analysis**, not a mechanical rule — see spec §2.1.
5. **`frontend_widgets.lk` is provisional.** The frontend track is parked; its spelling must not
   block backend publish.

## Verify twins exist

```bash
python3 scripts/syntax_v2_corpus_check.py
cd vm && make test-syntax-v2
```
