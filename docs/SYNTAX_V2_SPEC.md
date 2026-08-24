# Syntax v2 — normative spec

> **Status:** normative for core / reactive / backend. Layout-UI section is **provisional**.
> **Plan:** [`SYNTAX_V2_PLAN.md`](./SYNTAX_V2_PLAN.md) · **Golden corpus:** [`examples/v2/`](../examples/v2/)
> **Decisions:** all nine open items in the plan's §8 are resolved as recommended — recorded in [§1](#1-resolved-decisions).

This spec is the mapping contract for the v2 front-end. Phase 2 is complete when every form
below lowers to the v1 form in the same row and the golden corpus produces byte-identical
stdout to its v1 twins.

Coverage is verified mechanically, not by eye:

```bash
python3 scripts/syntax_v2_spec_check.py
```

That script fails if any live conversational phrase is missing from this document. Because this
is a solo project with no second reviewer, **the gates are the review** — every claim in this
spec is either machine-checked or explicitly marked provisional.

---

## 1. Resolved decisions

| # | Decision | Resolution |
| --- | --- | --- |
| 1 | Scope | Technical imperative core; **reactive keywords stay declarative** |
| 2 | Blocks | Braces `{ }` |
| 3 | Mutability | `let` (immutable) / `var` (mutable) |
| 4 | Type syntax | `x: int` |
| 5 | Numeric names | `int` (int64) / `float` (double) |
| 6 | String concat | `+` now; `"${x}"` interpolation deferred |
| 7 | Instantiation | `Ticket(10)` — no `new` |
| 8 | Type keyword | `struct` |
| 9 | Extension at flip | `.luke` becomes v2; `.lk` is transition-only and is deleted at flip |

### Lexical rules

- **Case sensitive.** v1 accepted any casing (`startsCI`); v2 keywords are lowercase.
  Identifiers keep their v1 spelling, so `httpReply` stays `httpReply`.
- **Statement terminator:** newline. No semicolons required; `;` permitted as a separator.
- **Comments:** `//` line, `/* */` block.
- **Strings:** `"..."` with `\n \t \" \\` escapes. Triple-quoted `"""..."""` blocks are retained
  verbatim for embedded CSS/HTML (see [§8](#8-embedded-foreign-content)).
- **Reserved words** are the lowercase keywords in this document. `signal`, `derived`, `effect`,
  `watch`, `bind` are reserved.

---

## 2. Core statements

| v1 | v2 | Notes |
| --- | --- | --- |
| `SPEAK x` / `SAY x` / `SHOUT x` | `print(x)` | `SAY`/`SHOUT` collapse into `print` |
| `MY NAME IS n SET TO e` | `let n = e` **or** `var n = e` | see [§2.1](#21-let-vs-var-requires-mutation-analysis) |
| `MY NAME IS n AS T SET TO e` | `let n: T = e` **or** `var n: T = e` | same |
| `MY NAME IS n AS T` | `var n: T` | declaration without initialiser is mutable |
| `SET n TO e` | `n = e` | |
| `SET SELF.f TO e` | `self.f = e` | |
| `INCREASE n BY e` | `n += e` | `DECREASE` → `-=` |
| `THIS IS FUNCTION f WITH a AS T GIVES BACK R DO … END FUNCTION` | `fn f(a: T) -> R { … }` | `GIVES BACK` optional in both |
| `GIVE BACK e` | `return e` | |
| `ASK f WITH a, b` | `f(a, b)` | statement and expression form |
| `ASK obj TO m` | `obj.m()` | |
| `ASK obj TO m WITH a` | `obj.m(a)` | |
| `IF c DO … END IF` | `if c { … }` | |
| `ELSE` | `} else {` | `ELSE IF` → `} else if c {` |
| `WHILE c DO … END WHILE` | `while c { … }` | |
| `FOR EACH x IN xs DO … ` | `for x in xs { … }` | |
| `IMPORT spec` | `import spec` | see [§7](#7-modules) |
| `IN ARENA DO … END ARENA` | `arena { … }` | |
| `ATTEMPT DO … OTHERWISE WITH e DO … END ATTEMPT` | `try { … } catch (e) { … }` | |
| `GIVE UP WITH e` | `throw e` | |
| `GET OUTTA HERE` | `break` | |
| `LET'S START` / `DONE` | *(removed)* | v1 program bookends; no v2 equivalent |
| `TEST "n" DO … END TEST` | `test "n" { … }` | |
| `MAKE SURE c` | `assert c` | |
| `RECORD BENCH SAMPLE e` | `bench.sample(e)` | |

### 2.1 `let` vs `var` requires mutation analysis

v1 has **no immutability distinction** — `MY NAME IS` declares everything, and any binding can
later be reassigned with `SET`. v2 splits this into `let` and `var`, so the mapping is **not
mechanical**. A one-to-one `MY NAME IS` → `let` rule produces code that will not compile.

The rule the migrator must implement, per binding, within its scope:

> Emit `var` if the name is ever the target of `SET`, `CHANGE`, `INCREASE`, or `DECREASE`.
> Otherwise emit `let`.

Concretely, from `examples/build/backend_api.luke`:

```luke
MY NAME IS answered AS FLAG SET TO FALSE   // later: SET answered TO TRUE
MY NAME IS path SET TO ASK httpPath WITH req   // never reassigned
```

```rust
var answered: bool = false   // mutated -> var
let path = httpPath(req)     // never mutated -> let
```

This is the first place a naive migrator breaks, and it is not detectable by the stdout
equivalence harness alone — bad output here fails to *compile*, which is the good case. The
dangerous case is the reverse: emitting `var` everywhere would compile fine and silently discard
the immutability guarantee that motivated decision 3. **Prefer `let` and let the compiler
complain.**

### Types

| v1 | v2 |
| --- | --- |
| `NUMBER` | `float` |
| `INTEGER` | `int` |
| `TEXT` | `str` |
| `FLAG` | `bool` |
| `JSON` | `json` |
| `LIST` | `list` |
| `MAP` | `map` |
| `SERVER` | `Server` |
| `REQUEST` | `Request` |
| `DATABASE` | `Db` |
| `BLUEPRINT Foo` (as a type) | `Foo` |

`AS` as a type-annotation particle becomes `:`. `AS` in `SLOT INPUT AS CHECKBOX` becomes a named
argument (see [§6](#6-layout-and-ui--provisional)).

### Operators

| v1 | v2 |
| --- | --- |
| `ADD a AND b` | `a + b` |
| `SUBTRACT b FROM a` / `a SUBTRACT b` | `a - b` |
| `a MULTIPLIED BY b` / `MULTIPLY` | `a * b` |
| `a DIVIDED BY b` / `DIVIDE` | `a / b` |
| `a MOD b` | `a % b` |
| `a EQUALS b` | `a == b` |
| `a IS NOT b` | `a != b` |
| `a IS GREATER THAN b` | `a > b` |
| `a IS LESS THAN b` | `a < b` |
| `NOT a` | `!a` |
| `a AND b` *(logical)* | `a && b` |
| `a OR b` | `a \|\| b` |
| `"x=" AND y` *(text concat)* | `"x=" + y` |
| `TRUE` / `FALSE` | `true` / `false` |

**The `AND` split is the headline fix.** v1 overloads one token for concatenation and
conjunction; v2 separates `+` from `&&`. A v1→v2 migrator must resolve this by operand type:
`str` operands lower to `+`, `bool` operands to `&&`.

Precedence (loosest to tightest): `||` · `&&` · `== != < > <= >=` · `+ -` · `* / %` · unary `! -`
· call/index/field. Parentheses group.

#### Associativity and parenthesisation

Verified against the v1 compiler rather than assumed:

- v1 arithmetic is **left-associative** (`10 SUBTRACT 3 SUBTRACT 2` = 5, `12 DIVIDED BY 3 DIVIDED BY 2` = 2).
- v1 concatenation (`AND`) is **right-associative**.

The lowerer therefore **always parenthesises binary operands**. That makes grouping explicit and
immunises non-associative operators (`-`, `/`) against any v1 associativity assumption. The
visible consequence is that `a + b + c` on strings produces a differently-shaped — but
semantically identical — tree than hand-written v1, so the equivalence harness treats a
generated-C difference on concatenation as expected, not a failure.

#### Not yet lowered

| Form | Status |
| --- | --- |
| `%` | no Build-mode equivalent; rejected with an error |
| `for i in 0..n` | range loops rejected; use `while` |
| non-empty `[…]` / `{…}` literals | rejected; build containers with `push` / index assignment |

---

## 3. Collections

| v1 | v2 |
| --- | --- |
| `MY NAME IS xs AS LIST` | `var xs: list = []` |
| `ADD e TO xs` | `xs.push(e)` |
| `ITEM i OF xs` | `xs[i]` |
| `LAST OF xs` | `xs.last()` |
| `HOW MANY IN xs` | `xs.len()` |
| `DELETE i FROM xs` | `xs.remove(i)` |
| `MY NAME IS m AS MAP` | `var m: map = {}` |
| `PUT k TO v IN m` | `m[k] = v` |
| `GET k FROM m` | `m[k]` |
| `HAS KEY k IN m` | `m.has(k)` |

---

## 4. Structs (was `BLUEPRINT`)

| v1 | v2 |
| --- | --- |
| `BLUEPRINT Foo DO … END CLASS` / `END BLUEPRINT` | `struct Foo { … }` |
| `CLASS Foo` | `struct Foo` |
| `BLUEPRINT Foo FOLLOWS Bar` / `EXTENDS` | `struct Foo : Bar { … }` |
| `HAS f AS T` | `f: T` |
| `HAS f SET TO e` | `f = e` |
| `WHEN BORN WITH a AS T DO … END BORN` | `init(a: T) { … }` |
| `METHOD m WITH a AS T DO … END METHOD` | `fn m(a: T) { … }` |
| `PRIVATE METHOD m` / `SECRET METHOD m` | `private fn m` |
| `PRIVATE f` | `private f: T` |
| `SELF.f` | `self.f` |
| `CALL PARENT m` | `super.m()` |
| `CALL PARENT m OF Anc WITH a` | `super<Anc>.m(a)` |
| `NEW Foo WITH a` | `Foo(a)` |
| `DESTROY x` | `drop(x)` |
| `CONTRACT` (Play-only interface) | `trait` |

`BORN` / `BORNED` are v1 `WHEN BORN` block enders; v2 uses `}`.

---

## 5. Reactive — declarative, by decision

Spelling changes; kind does not. These stay first-class statements, not library calls.

| v1 | v2 |
| --- | --- |
| `REMEMBER c AS e` | `signal c = e` |
| `SECRET REMEMBER c AS e` | `secret signal c = e` |
| `THE t IS expr` | `derived t = expr` |
| `CHANGE c TO e` | `c = e` |
| `WHEN REACTIVE c CHANGES DO … END WHEN REACTIVE` | `effect on c { … }` |
| `WHEN BACKGROUND REACTIVE c CHANGES DO …` | `effect background on c { … }` |
| `WHEN REACTIVE WEAK c CHANGES DO …` | `effect weak on c { … }` |
| `BEGIN REACTIVE BATCH … END REACTIVE BATCH` | `batch { … }` |
| `BIND "id" TO c` | `bind("id", c)` |
| `BIND LIST "id" TO xs` | `bind.list("id", xs)` |
| `BIND OPACITY "id" TO c` | `bind.opacity("id", c)` |
| `WATCH c FROM db WHERE "…"` | `watch c from db where "…"` |
| `WATCH c FROM "url"` | `watch c from "url"` |
| `PUSH WATCH c ON req` | `push watch c on req` |
| `PUSH WATCH c ON req FOR n BEATS EVERY m MILLISECONDS` | `push watch c on req for n beats every m ms` |
| `THE VALUE OF c` | `c.value()` |
| `THE WEAK VALUE OF c` | `c.weak()` |
| `THE DEP COUNT OF c` | `c.deps()` |
| `THE SUB COUNT OF c` | `c.subs()` |
| `THE NODE ID OF c` | `c.id()` |
| `TRACE WHY c` | `why(c)` |
| `THE WHY ROOT OF c` | `why(c).root()` |
| `THE WHY DEPTH OF c` | `why(c).depth()` |
| `REPORT REACTIVE FAILURE FOR c` | `reactive.fail(c)` |
| `REFRESH QUERY q` | `q.refresh()` |
| `QUERY ON …` | `query on …` |
| `SUBSCRIBE …` / `START SUBSCRIBE` | `subscribe …` / `subscribe.start()` |
| `FETCH …` / `START FETCH` | `fetch …` / `fetch.start()` |
| `THE STATUS OF FETCH f` | `f.status()` |
| `THE BODY OF FETCH f` | `f.body()` |
| `TIMELINE` / `START TIMELINE` / `RUN TIMELINE` | `timeline` / `timeline.start()` / `timeline.run()` |
| `THE TIMELINE STEP ID AT i` | `timeline.step(i).id()` |
| `THE TIMELINE STEP WAVE AT i` | `timeline.step(i).wave()` |
| `SCRUB TO ACCESS OF c` | `timeline.scrub(c)` |
| `WHO SAW c` | `who_saw(c)` |

---

## 6. Layout and UI — provisional

**Not normative.** The frontend track is parked (`TaskList.md` §7) and backend is the beachhead.
These mappings are a placeholder so `frontend_widgets` can exist in the golden corpus; settling
them must not block backend publish. Phase 4 revisits.

The shape: declarative blocks with braces and named arguments.

| v1 | v2 (provisional) |
| --- | --- |
| `BEGIN COLUMN AT x, y SIZE w, h PAD p GAP g` | `column { at: x, y; size: w, h; pad: p; gap: g }` |
| `BEGIN ROW` / `BEGIN STACK` / `BEGIN GRID` | `row { }` / `stack { }` / `grid { }` |
| `END COLUMN` | `}` |
| `ALIGN MAIN CENTER CROSS START` | `align: main(center), cross(start)` |
| `SLOT TEXT "id" SIZE w, h SAY "s"` | `text("id") { size: w, h; say: "s" }` |
| `SLOT BUTTON` / `SELECT` / `TABLE` / `MODAL` / `INPUT` | `button(…)` / `select(…)` / `table(…)` / `modal(…)` / `input(…)` |
| `SLOT INPUT AS CHECKBOX "id"` | `input("id") { kind: checkbox }` |
| `SIZE AUTO, 40` | `size: auto, 40` |
| `PLACE "id" AT x, y` | `place("id") { at: x, y }` |
| `LAY OUT THE SCREEN` | `layout()` |
| `PAINT THE SCREEN` | `paint()` |
| `NAME THE PAGE "t"` | `page.title("t")` |
| `WEAR STYLE """…"""` | `page.style("""…""")` |
| `FILL "id" WITH """…"""` | `fill("id", """…""")` |
| `BRING FONT "n" FROM "p"` | `page.font("n", "p")` |
| `BEGIN COMPONENT C` / `END COMPONENT` | `component C { … }` |
| `UNMOUNT COMPONENT c` / `DESTROY COMPONENT c` | `c.unmount()` / `c.destroy()` |
| `BEGIN ENTITY E` / `END ENTITY` / `ENTITY` | `entity E { … }` |
| `BEGIN ERROR BOUNDARY b` / `END ERROR BOUNDARY` | `boundary b { … }` |
| `ERROR BOUNDARY` / `RESET ERROR BOUNDARY b` | `boundary` / `b.reset()` |
| `THE BOUNDARY TRIPPED FOR b` | `b.tripped()` |
| `ANNOUNCE "m"` | `announce("m")` |
| `TRAP FOCUS IN "id"` / `RESTORE FOCUS` | `focus.trap("id")` / `focus.restore()` |
| `OPEN THE MODAL "id"` / `OPEN MODAL` | `modal.open("id")` |
| `FADE "id" TO v` | `fade("id", v)` |
| `SET THE OPACITY OF "id" TO v` | `opacity("id", v)` |
| `THE TEXT WIDTH OF "s"` | `text_width("s")` |
| `THE VIEWPORT WIDTH` | `viewport.width()` |
| `THE VIEWPORT CHANGES` | `viewport.changes()` |
| `THE VIEWPORT IS AT LEAST n` | `viewport.at_least(n)` |
| `THE VIEWPORT IS UNDER n` | `viewport.under(n)` |
| `THE VIEWPORT IS ABOVE n` | `viewport.above(n)` |
| `THE VIEWPORT IS BELOW n` | `viewport.below(n)` |
| `THE VIEWPORT IS BETWEEN a AND b` | `viewport.between(a, b)` |
| `WHEN "id" IS CLICKED DO … END WHEN` | `on click("id") { … }` |
| `ACTION a` | `action a` |
| `GO TO "/path"` | `route.go("/path")` |
| `LINK TO "/path"` | `link("/path")` |
| `THE ROUTE IS "/p"` | `route.is("/p")` |
| `AT`, `ON`, `UP`, `OF`, `WITH` | argument particles — become named args, `.`, or `()` |

---

## 7. Modules

| v1 | v2 |
| --- | --- |
| `IMPORT std/files` | `import std/files` |
| `IMPORT "./critter.luke"` | `import "./critter.lk"` |
| `IMPORT luke/greeter` | `import luke/greeter` |
| `IMPORT package:greeter` | `import luke/greeter` — `package:` alias dropped |
| `FOREIGN FUNCTION` / `FOREIGN` | `extern fn` |
| `c:` / `C:` FFI prefix | `extern "c"` |
| `VERSION` / `entry=` (in `luke.pkg`) | unchanged — manifest, not source syntax |

`std/` and `luke/` remain import namespace prefixes, unchanged.

---

## 8. Backend — declarative forms retained

| v1 | v2 |
| --- | --- |
| `ROUTES DO … END ROUTES` | `routes { … }` |
| `GET "/p" HANDLE h` | `get "/p" -> h` |
| `POST "/p" HANDLE h` | `post "/p" -> h` |
| `SERVE ROUTES ON port` | `serve routes on port` |
| `MIDDLEWARE ORDER a, b` | `middleware a, b` |
| `FORM f DO … END FORM` | `form f { … }` |
| `COLLECT a, b` | `collect a, b` |
| `VALIDATE FORM f` | `f.validate()` |
| `VERIFY …` | `verify …` |
| `SCHEMA s DO … END SCHEMA` | `schema s { … }` |
| `ENSURE SCHEMA s` | `ensure schema s` |
| `MIGRATION m DO … END MIGRATION` | `migration m { … }` |
| `MIGRATE` / `REWIND` | `migrate` / `rewind` |
| `UPDATE …` | `update …` |
| `REQUIRE LOGIN ON req WITH db` | `require login on req with db` |
| `THE CURRENT USER` | `current_user` |
| `CREATE ACCOUNT FROM FLOW f` | `create account from f` |
| `FLOW f DO … END FLOW` | `flow f { … }` |
| `ADVANCE FLOW f` | `f.advance()` |
| `SECRET x` | `secret x` |
| `REVEAL x` | `reveal x` |
| `LIMIT n PER MINUTE PER IP` | `limit n per minute per ip` |
| `REFRESH LIMIT` | `limit.refresh()` |

### Embedded foreign content

Triple-quoted blocks (`WEAR STYLE """…"""`, `FILL … WITH """…"""`) contain CSS/HTML, not Luke.
The lexer treats them as opaque and the migrator copies them **byte for byte**. This is why part
of the plan's `Raw` tail needs no structuring — see `SYNTAX_V2_PLAN.md` §2.2.

---

## 9. Dropped in v2

The 44 dead phrases (present in codegen, unused in the corpus) are **not ported**. Notable
deliberate removals beyond those:

| Dropped | Reason |
| --- | --- |
| `LET'S START` / `DONE` | program bookends with no semantics |
| `SAY` / `SHOUT` / `YELL` | aliases of `SPEAK`; v2 has one `print` |
| `SEND BACK` / `HAND BACK` | aliases of `GIVE BACK` |
| `MAKE FUNCTION` | alias of `THIS IS FUNCTION` |
| `package:` | alias of `luke/` |
| `BORNED` | alias of `BORN` |
| `CLOSE THE MODAL` / `HIDE MODAL` / `SHOW MODAL` | dead; `modal.open/close` covers it |

Collapsing aliases is a real simplification: v1's 216 phrases include many synonyms that exist
only because the surface was English.

---

## 10. Worked example

v1 — `examples/build/backend_api.luke` (excerpt):

```luke
IMPORT std/server
IMPORT std/sqlite

THIS IS FUNCTION handle WITH req AS REQUEST DO
  MY NAME IS db SET TO ASK dbOpen WITH "/tmp/luke_backend_api.db"
  MY NAME IS method SET TO ASK httpMethod WITH req
  MY NAME IS path SET TO ASK httpPath WITH req
  MY NAME IS params AS MAP
  IF method EQUALS "GET" DO
    IF ASK httpMatch WITH path, "/user/:id", params DO
      MY NAME IS id SET TO GET "id" FROM params
      MY NAME IS binds AS LIST
      ADD id TO binds
      MY NAME IS name SET TO ASK dbQueryBind WITH db, "SELECT name FROM users WHERE id = ?", binds
      ASK httpReply WITH req, 200, "text/plain", name
    END IF
  END IF
  ASK dbClose WITH db
END FUNCTION
```

v2 — `examples/v2/backend_api.lk`:

```rust
import std/server
import std/sqlite

fn handle(req: Request) {
  let db = dbOpen("/tmp/luke_backend_api.db")
  let method = httpMethod(req)
  let path = httpPath(req)
  var params: map = {}
  if method == "GET" {
    if httpMatch(path, "/user/:id", params) {
      let id = params["id"]
      var binds: list = []
      binds.push(id)
      let name = dbQueryBind(db, "SELECT name FROM users WHERE id = ?", binds)
      httpReply(req, 200, "text/plain", name)
    }
  }
  dbClose(db)
}
```

Same program, same line count. The win is not brevity — it is that every construct is one a
backend developer already recognises, and that `ASK … WITH`, `MY NAME IS … SET TO`, and
`GET … FROM` stop being things you have to learn before writing a route handler.

---

## 11. What the front-end must guarantee

1. **Lower to v1 text**, not to C. Codegen is untouched (plan §3).
2. **Emit `// @luke-file "app.lk" N` markers** so diagnostics, `#line` maps, gdb, and LSP all
   report `.lk` positions. This is a Phase 2 gate, not a follow-up.
3. **Preserve evaluation order and stdout exactly.** The acceptance test is byte-identical
   stdout against the v1 twin, for every file in [`examples/v2/`](../examples/v2/).
4. **Reject, never guess.** An unmappable construct is a compile error citing this spec, not a
   silent passthrough.

### Implementation status

Shipped in `vm/src/luke2_lex.cpp`, `luke2_parse.cpp`, `luke2_lower.cpp`, `luke2_migrate.cpp`;
`.lk` paths and `luke MIGRATE` are dispatched from `main.cpp`. `build_c.cpp` is unmodified.

| Gate | State |
| --- | --- |
| Spec ↔ live-phrase coverage | `scripts/syntax_v2_spec_check.py` |
| Golden corpus pairing | `scripts/syntax_v2_corpus_check.py` |
| Lower: BUILD(v2) ≡ BUILD(v1) | `scripts/syntax_v2_equiv.sh` — **9 / 9 normative** |
| Migrate: BUILD(MIGRATE(v1)) ≡ BUILD(v1) | `scripts/syntax_v2_migrate_equiv.sh` — **10 / 10 goldens** |
| Full corpus BUILD-after-migrate | ~**93 / 118** (Phase 3a in progress; 9 intentional negatives) |
| Errors report `.lk` positions | **pass** |
| Debug info points at `.lk` | **pass** |
| Layout/UI surface (§6) | **not lowered** — provisional |

Phase 3a (structuring the remaining Raw tail) is required before `MIGRATE_CORPUS=all` can be
the CI gate; today that probe is informational (~38/118 pass on `examples/build`).

```bash
cd vm && make && make test-syntax-v2
```

Type information comes from v2 annotations, literals, in-file `fn` signatures, and — for stdlib
calls — the `GIVES BACK` clauses read straight out of the v1 `vm/stdlib/*.luke` sources. No
hand-maintained return-type table, so it cannot drift from the stdlib.
