# Syntax v2 — conversational → technical

> **Status:** Phases 0–5 flip complete (`.luke` is v2; `--syntax=1` deprecation window).
> Phrase-prefix deletion in `build_c.cpp` is the post-window cleanup. No codegen AST rewrite yet.
> **Scope:** replace the conversational surface syntax with a conventional technical syntax
> (braces, operators, `fn`, `let`) while keeping the reactive engine and Build guarantees intact.

This document is the plan of record for the syntax change. It is written against measurements
of the current tree, not assumptions. Every number below is reproducible with the commands in
[§2](#2-what-the-code-actually-looks-like-today).

---

## 1. The premise, stated honestly

The trigger for this work is user feedback that the syntax is bad. That feedback is worth
acting on — conversational syntax is the single most common objection to LukeLang, and it is
the first thing a backend developer sees.

One caveat to record before spending compiler effort: the feedback captured so far is a small
number of public comments, not a measured signal. A syntax migration is the most expensive
change a language can make, and it is close to irreversible once the corpus is converted.
**Phase 0 exists to make the premise cheap to verify** — it costs no compiler work and can run
while Phase 1 design proceeds.

This is not an argument against the change. It is an argument for spending one cheap step on
evidence, so the decision record says "we measured" rather than "we reacted".

### What the project's own strategy says

`docs/STRATEGY.md` is explicit on two points that matter here:

- *"the **reactive engine is the moat**, the **conversational syntax is the face**"* (line 12)
- *"**Conversational syntax.** It is core identity and a felt differentiator. Keep it."* (line 56)

The first line is the one that makes this change defensible on the project's own terms: **the
moat is the reactive engine, not the syntax.** Changing the face does not breach the moat. The
second line directly contradicts this proposal and must be formally amended, not quietly
ignored — see [Phase 0](#phase-0--validate-and-record-the-decision).

---

## 2. What the code actually looks like today

Measured on this tree:

| Fact | Value |
| --- | --- |
| `.luke` files | 155 (5,429 lines) |
| — `examples/build/` | 118 |
| — `vm/stdlib/` | 16 |
| Compiler total | 14,985 lines |
| — `vm/src/build_c.cpp` (codegen) | **7,944 lines (53%)** |
| — `vm/src/luke_parse.cpp` (statement AST) | 858 lines |
| Distinct conversational phrases hard-coded in codegen | **216** — **170 live**, 44 dead (20.6%) |
| Distinct phrases in the statement parser | 41 |
| App-level statements that reach a structured AST node | **80%** |
| App-level statements that stay `StmtKind::Raw` (text passthrough) | **20%** (453 nodes) |
| Distinct leading forms in that `Raw` tail | **203** |
| `luke` code fences in docs | 46 |
| Test scripts with conversational source embedded | 4 + `vm/Makefile` |

Reproduce every number above:

```bash
cd vm && make
python3 ../scripts/syntax_surface_census.py              # the table
python3 ../scripts/syntax_surface_census.py --raw-queue   # Phase 3a work queue
```

### 2.1 The architectural finding that decides the strategy

The pipeline is **not** `source → AST → C`. It is:

```
source
  → expandImports
  → parseLuke        (luke_parse.cpp)  → Program AST      ← used by IR / FMT / LSP
  → flattenProgram   (luke_parse.cpp)  → conversational TEXT
  → parse(bc, text)  (build_c.cpp)     → BC lowering
  → emit             (build_c.cpp)     → C
```

From `vm/src/build_c.cpp:7854`:

```cpp
Program prog = parseLuke(expanded);
r.astSummary = dumpProgram(prog);
std::string viaAst = flattenProgram(prog);
return compileExpanded(viaAst.empty() ? expanded : viaAst, options, std::move(r));
```

So there are effectively **two parsers**. The `Program` AST is real and shared by tooling, but
**codegen does not consume it** — it re-parses flattened *conversational text* using ~216
hard-coded English phrase prefixes (`startsWithCI(e, "THE TEXT WIDTH OF ")` and friends),
spread through 7,944 lines.

Two consequences, one bad and one very good:

- **Bad:** the conversational syntax is not a thin front-end. It is welded into the largest file
  in the compiler. A "just swap the parser" change does not exist.
- **Good:** because codegen's input is *text*, a technical front-end can **lower to
  conversational text** and reuse the entire existing backend unchanged. That is the cheap,
  low-risk path, and it is what this plan recommends.

### 2.2 The `Raw` tail is the real cost centre

20% of app statements never become structured nodes; they pass through as text. That tail is
**203 distinct leading forms for 453 statements** — the most common is 5.9%. There is no 80/20
win available. It spreads across reactive/live-graph (37%), core (22%), frontend/UI (21%), and
declarative backend (20%).

This matters because **a fully automatic v1 → v2 migrator can only translate what is
structured.** The `Raw` tail is exactly the set of statements a migrator must either
(a) get structured first, or (b) hand to a human. See [Phase 3](#phase-3--mechanical-migrator).

One mitigating detail the census surfaces: a slice of the tail is not Luke at all. Entries like
`font-weight: 700; …`, `background: #f4a259; …` and bare `}` are **embedded CSS inside
`WEAR STYLE """…"""` blocks**. Embedded foreign content never needs structuring — it only needs
to survive the migration verbatim. Triage the queue on that basis before estimating 3a; the real
form count is meaningfully below 203.

---

## 3. Strategy: dual front-end, one backend

**Add a v2 front-end that lowers to v1 conversational text.** Do not rewrite codegen.

```
app.lk  → lex2 → parse2 → v2 AST → lower2 (emit v1 text + line markers) ─┐
                                                                          ├→ existing compileLukeToC → C
app.luke ─────────────────────────────────────────────────────────────────┘
```

Why this shape:

- **Zero changes to the 7,944-line codegen.** No-GC guarantees, typechecks, Live Graph, arena
  scopes, C10K server, auth — all keep working because their input is unchanged.
- **Risk is contained to one new module.** If v2 lowering is wrong, v1 still builds.
- **Diagnostics and source maps survive.** This is the one hard problem with a desugaring
  front-end: errors would point at generated v1 text instead of the user's file. The tree
  **already solves this** — `luke_ast.hpp` defines `// @luke-file "path" N` markers, consumed at
  `build_c.cpp:5952`, used today to remap lines across `IMPORT` expansion. The v2 lowerer emits
  the same markers, so `#line` maps, gdb, `luke DEBUG`, and LSP diagnostics point at `.lk`
  source for free.
- **Both syntaxes coexist in one tree**, which the equivalence harness in Phase 3 *requires*.

### Rejected alternative: rewrite codegen to consume the AST directly

This is the correct long-term architecture, and it should happen eventually. It must **not** be
coupled to the syntax change: it means rewriting 7,944 lines of working, CI-covered codegen and
structuring the 203-form `Raw` tail at the same time as changing the user-facing language. Two
high-risk projects at once, with no intermediate state that ships. Keep it as a separate track.

### File extension during transition

`.lk` = v2, `.luke` = v1. Not a pragma. The equivalence harness needs v1/v2 twins of the same
program side by side in one tree, which a single extension cannot express. At flip time
(Phase 5), `.luke` becomes v2 and `.lk` remains an alias.

---

## 4. Proposed technical syntax

Opinionated recommendation, TypeScript/Rust-family, chosen because the audience is backend
developers. Open questions are isolated in [§8](#8-open-decisions-needing-sign-off).

### Core

| Concept | v1 (conversational) | v2 (technical) |
| --- | --- | --- |
| Output | `SPEAK "hi"` | `print("hi")` |
| Immutable binding | `MY NAME IS n SET TO 1` | `let n = 1` |
| Typed binding | `MY NAME IS n AS NUMBER SET TO 1` | `let n: float = 1` |
| Mutable binding | *(implicit)* | `var n = 1` |
| Assignment | `SET n TO 4` | `n = 4` |
| Increment | `INCREASE n BY 1` | `n += 1` |
| Function | `THIS IS FUNCTION f WITH a AS NUMBER GIVES BACK NUMBER DO … END FUNCTION` | `fn f(a: float) -> float { … }` |
| Return | `GIVE BACK x` | `return x` |
| Call | `ASK f WITH a, b` | `f(a, b)` |
| Method call | `ASK obj TO m` | `obj.m()` |
| Conditional | `IF c DO … END IF` | `if c { … }` |
| Loop | `WHILE c … END WHILE` | `while c { … }` |
| Range loop | `FOR i FROM 0 TO 10` | `for i in 0..10 { … }` |
| Import | `IMPORT std/server` | `import std/server` |
| Arena scope | `IN ARENA DO … END ARENA` | `arena { … }` |
| Test | `TEST "m" DO MAKE SURE a EQUALS b END TEST` | `test "m" { assert a == b }` |

### Operators

| v1 | v2 |
| --- | --- |
| `ADD a AND b` | `a + b` |
| `SUBTRACT b FROM a` | `a - b` |
| `a MULTIPLIED BY b` | `a * b` |
| `a DIVIDED BY b` | `a / b` |
| `a EQUALS b` | `a == b` |
| `a IS LESS THAN b` | `a < b` |
| `NOT a` | `!a` |
| `"x=" AND x` *(text concat)* | `"x=" + x` |

**This fixes a real ambiguity.** In v1, `AND` is both text concatenation and logical
conjunction. v2 separates them: `+` concatenates, `&&` conjoins. That is a language
improvement, not just a cosmetic change.

### Types

| v1 | v2 |
| --- | --- |
| `NUMBER` (double) | `float` |
| `INTEGER` (int64) | `int` |
| `TEXT` | `str` |
| `FLAG` | `bool` |
| `JSON` / `LIST` / `MAP` | `json` / `list` / `map` |
| `SERVER` / `REQUEST` / `DATABASE` | `Server` / `Request` / `Db` |

### Collections and errors

| v1 | v2 |
| --- | --- |
| `ADD x TO nums` | `nums.push(x)` |
| `ITEM 0 OF nums` | `nums[0]` |
| `HOW MANY IN nums` | `nums.len()` |
| `PUT k TO v IN bag` | `bag[k] = v` |
| `GET k FROM bag` | `bag[k]` |
| `ATTEMPT DO … OTHERWISE WITH e DO … END ATTEMPT` | `try { … } catch (e) { … }` |
| `GIVE UP WITH "m"` | `throw "m"` |

### Blueprints

```
struct Ticket {
  price: int

  init(p: int) { self.price = p }

  fn book(n: int) -> bool { … }
}

let t = Ticket(10)
t.book(2)
```

### Reactive — deliberately still declarative

**This is the moat. It does not become generic function calls.** Keeping these as first-class
declarative keywords is what preserves the differentiator; the v2 spellings match Solid/Svelte
vocabulary, so they read as *more* familiar to modern developers, not less distinctive.

| v1 | v2 |
| --- | --- |
| `REMEMBER price AS 100` | `signal price = 100` |
| `SECRET REMEMBER t AS "…"` | `secret signal t = "…"` |
| `THE total IS price MULTIPLIED BY quantity` | `derived total = price * quantity` |
| `CHANGE quantity TO 4` | `quantity = 4` |
| `WHEN REACTIVE … END WHEN REACTIVE` | `effect { … }` |
| `BEGIN REACTIVE BATCH … END REACTIVE BATCH` | `batch { … }` |
| `BIND "label" TO count` | `bind("label", count)` |
| `WATCH user FROM db WHERE "id = 1"` | `watch user from db where "id = 1"` |
| `PUSH WATCH user ON req` | `push watch user on req` |
| `WHEN "go" IS CLICKED DO … END WHEN` | `on click("go") { … }` |

### Illustrative before/after

```luke
import std/server
fn handle(req: Request) {
  let path = httpPath(req)
  if path == "/ok" {
    httpReply(req, 200, "text/plain", "ok")
  }
}
```

```rust
// v2
import std/server

fn handle(req: Request) {
  let path = httpPath(req)
  if path == "/ok" {
    httpReply(req, 200, "text/plain", "ok")
  }
}
```

---

## 5. What does not change

- The reactive engine, Live Graph, IVM, scheduler semantics.
- Build guarantees: no GC, arena memory, fixed layouts, AOT native/WASM.
- `std/*` behaviour and function names (`httpReply`, `dbQueryBind`, `pgExecBind`, …).
- `luke LSP` / `DAP` / `FMT` / `IR` command surface.
- Codegen (`build_c.cpp`) — untouched for the whole migration.

---

## 6. Phases

Each phase has an exit gate. Do not start the next phase until the gate is green.

### Phase 0 — Validate and record the decision

No compiler work. Runs in parallel with Phase 1.

1. Publish the [§4](#4-proposed-technical-syntax) before/after side by side and collect
   preference data from more than a handful of respondents. The goal is a number, not a vibe.
2. Amend `docs/STRATEGY.md` — line 12 identity sentence and line 56 ("Keep it"). Record *why*
   the face is changing and that the moat is not.
3. Choose scope: **full technical** vs **technical imperative core, declarative reactive
   keywords retained** (this plan recommends the latter — see [§4](#4-proposed-technical-syntax)).
4. Freeze: no new conversational-only surface lands while the migration is in flight.

**Gate:** written decision record merged; scope chosen.

### Phase 1 — Specify the surface

Docs only, no code risk.

1. `docs/SYNTAX_V2_SPEC.md` — normative mapping for the **170 live** codegen phrases.
   Explicitly **drop the 44 dead ones** rather than porting them; that is a free 20.6%
   reduction in surface area (`--raw-queue` and the census list them).
2. Resolve [§8](#8-open-decisions-needing-sign-off).
3. Hand-write a **golden corpus** of ~10 representative programs in v2: `hello`, `functions`,
   `oop`, `collections`, `backend_api`, `reactive_core`, `live_graph_server`,
   `frontend_widgets`, `auth_api`, `collections_test`. These are the spec's executable examples
   and Phase 2's acceptance tests.

**Gate:** spec covers every live phrase or explicitly drops it; golden corpus written.

### Phase 2 — v2 front-end

New files, additive. Touches existing code only at CLI dispatch.

- `vm/src/luke2_lex.cpp` — real tokenizer (v1's statement layer is prefix-matched strings; v2
  needs actual tokens).
- `vm/src/luke2_parse.cpp` — recursive-descent statements + brace blocks; reuse the existing
  Pratt expression parser from `luke_expr.cpp` with a retargeted operator table.
- `vm/src/luke2_lower.cpp` — v2 AST → v1 conversational text **plus `// @luke-file` markers**.
- `vm/src/main.cpp` — dispatch `.lk` through the v2 front-end.

**Gate:** every golden-corpus file builds, and its stdout is byte-identical to its v1 twin.
Diagnostics on a deliberately broken `.lk` report the **`.lk` file and line**, not generated text.

### Phase 3 — Mechanical migrator

`luke MIGRATE in.luke -o out.lk` — v1 AST → v2 printer.

The 20% `Raw` tail is the constraint. Two sub-tracks:

- **3a:** extend `luke_parse.cpp` to structure the `Raw` forms, reducing 20% → near zero. This
  is a 203-form long-tail grind with no shortcut. Prioritise by measured frequency; the census
  command is in [§2](#2-what-the-code-actually-looks-like-today).
- **3b:** for anything still `Raw`, the migrator emits a `TODO(migrate)` marker and a
  `file:line` report for manual conversion. Ships value before 3a is complete.

**Equivalence harness** — the rigorous gate, and the reason this plan is safe. Model it directly
on `scripts/fmt_roundtrip_all.sh`, which already proves `BUILD(FMT(x)) ≡ BUILD(x)` for every
example:

```
for every f in examples/build/*.luke:
    BUILD(f)            → stdout_v1
    MIGRATE(f) → f.lk
    BUILD(f.lk)         → stdout_v2
    assert stdout_v1 == stdout_v2
```

**Gate:** harness green across all 118 examples, wired into `make test`.

### Phase 4 — Corpus and tooling cutover

- Migrate 118 examples. Migrate `vm/stdlib/` **last and separately** — it is the public API
  surface, and changing it at the same time as the syntax conflates two breaking changes.
- Rewrite `tools/vscode/lukelang/syntaxes/lukelang.tmLanguage.json` (operators/braces, not
  phrase prefixes), `language-configuration.json` (brace pairs, indent rules), snippets, and the
  `KEYWORDS` list in `src/extension.js`.
- Update the LSP keyword/completion tables in `vm/src/lsp.cpp`.
- Update 46 doc code fences and the 4 test scripts + `vm/Makefile` with embedded conversational
  source.

**Gate:** `make test` green; `lsp_providers.sh`, `dap_handshake.sh`, `fmt_roundtrip_all.sh`
green; VS Code extension packages and highlights v2 correctly.

### Phase 5 — Flip the default and deprecate

1. `.luke` means v2. v1 available behind `--syntax=1` for a deprecation window.
2. After the window: delete the v1 statement parser and **strip the 216 conversational phrase
   prefixes from `build_c.cpp`**, replacing them with the operator/token surface. This is where
   the codebase actually gets smaller, and it is the natural moment to start the separate
   "codegen consumes the AST directly" track from [§3](#rejected-alternative-rewrite-codegen-to-consume-the-ast-directly).

**Gate:** one syntax in the tree; docs, examples, tooling consistent.

---

## 7. Risks

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Diagnostics point at generated v1 text, not user `.lk` | High | `// @luke-file` markers already exist and already do this for `IMPORT`. Make it a Phase 2 gate. |
| `Raw` 20% blocks automatic migration | High | Phase 3b ships with `TODO(migrate)` markers; 3a grinds the tail by measured frequency. |
| Two syntaxes double doc/test/tooling burden | Medium | Time-box the window; Phase 5 has a hard delete step. |
| stdlib migration = simultaneous API break | Medium | Sequence stdlib last, separately (Phase 4). |
| Reactive keywords flattened into plain calls, moat diluted | Medium | [§4](#4-proposed-technical-syntax) keeps them declarative by design. Non-negotiable. |
| Strategy doc contradiction unresolved | Low | Phase 0 amendment. |
| Existing users' code breaks | Low today, rising | Corpus is 155 files and mostly first-party. Migrating now is far cheaper than later. |

### Effort shape (not calendar)

- **Phase 0–1:** docs only. No compiler risk.
- **Phase 2:** one new subsystem (~3 files), additive; existing code touched only at CLI
  dispatch. Contained and revertable.
- **Phase 3a:** the genuine unknown — 203 distinct forms across `luke_parse.cpp`. This is the
  phase that determines whether migration is fully automatic or partly manual.
- **Phase 4:** broad but mechanical once the harness is green; touches examples, docs, LSP
  tables, VS Code assets, test scripts.
- **Phase 5:** largest deletion; removes 216 phrase prefixes from the biggest file in the tree.

---

## 8. Open decisions needing sign-off

**All nine are resolved** — signed off as recommended. Normative record:
[`SYNTAX_V2_SPEC.md`](./SYNTAX_V2_SPEC.md) §1.

| # | Decision | Resolution |
| --- | --- | --- |
| 1 | Scope | Technical imperative core; **reactive keywords stay declarative** |
| 2 | Blocks | Braces `{ }` |
| 3 | Mutability | `let` / `var` |
| 4 | Type syntax | `x: int` |
| 5 | Numeric names | `int` (int64) / `float` (double) |
| 6 | String concat | `+` now; `"${x}"` deferred |
| 7 | Instantiation | `Ticket(10)` — no `new` |
| 8 | Type keyword | `struct` |
| 9 | Extension at flip | `.luke` = v2; `.lk` transition-only, deleted at flip |

One consequence surfaced while writing the golden corpus and is now spec'd: **decision 3 makes
the `MY NAME IS` mapping non-mechanical.** v1 has no immutability distinction, so the migrator
needs per-binding mutation analysis to choose `let` or `var` — see spec §2.1.

---

## 9. Status

**Phases 0–5 flip complete.** `.luke` is syntax v2 by default; conversational v1 is
`--syntax=1` (sources archived under `examples/v1_archive/` + `vm/stdlib_v1_archive/`).
Codegen still consumes lowered v1 text — phrase-prefix deletion is the post-window step.

### Phase 5 — flip (shipped)

- `.luke` means v2 (`isV2Path`); `--syntax=1` / `--syntax=2` on the CLI
- Live `examples/build/` + `vm/stdlib/` rewritten to v2; transition `.lk` removed there
- IMPORT lowers v2 modules (stdlib/siblings); `__*` intrinsics keep C-call spelling
- Twin / FMT / migrate gates read `examples/v1_archive/`
- LSP lowers v2 buffers before analyze
- Play VM accepts typed `WITH a AS NUMBER` params; string `+` lowers to flat `AND` chains;
  `*`/`/` lower to prefix `MULTIPLY`/`DIVIDE` (Play-compatible)
- Play-only gaps still on `--syntax=1` for the deprecation window: native closures,
  privacy, and `advanced_oop` (nested-fn upvalues break under typed lowered forms)

### Phase 4 — done

- Doc fences converted; VS Code 0.3.0; examples + stdlib cutover; LSP keywords/providers
- Deploy wall + Makefile inline probes (c10k grace, LSP fixture) on v2
- `make test` green after the flip

### Phase 3a — done (corpus migrate BUILD)

Goal was drive `MIGRATE` → BUILD for all positive `examples/build` programs, then tighten
equivalence. Achieved:

- `raw "…"` / `raw """…"""` passthrough; bare `try`; typed `signal … AS NUMBER SET TO`
- Migrator keeps opaque v1 for forms the v2 surface cannot express yet (IF/THEN derived,
  WEAK VALUE, dotted entity derived/INCREASE, NEW blueprints without in-file BLUEPRINT,
  FLOW COLLECT CHANGE cells, BIND OPACITY, WHEN with nested END IF, …)
- Harness writes migrated `.lk` next to the source so relative `IMPORT "sibling.luke"` works
- Golden migrate gate: **10 / 10**
- Full positive corpus BUILD-after-migrate: **109 / 109** (9 intentional negatives skipped)
- `MIGRATE_CORPUS=all` equivalence: positives pass (C-identical accepted when stdout is
  nondeterministic — auth salts, bench timings)

Remaining Raw `TODO(migrate)` markers are expected grind work; they do not block Phase 4.

### Phase 2 — shipped

The v2 front-end exists and the corpus is verified against it:

| Gate | Result |
| --- | --- |
| Golden corpus equivalence | **9 / 9 normative** — 6 byte-identical stdout, 3 servers byte-identical generated C |
| Errors cite `.lk` positions | **pass** |
| Debug info cites `.lk` | **pass** — `readelf --debug-dump=line` shows `hello.lk` |
| `build_c.cpp` modified | **no** — as designed |

New: `vm/include/luke2.hpp`, `vm/src/luke2_lex.cpp`, `luke2_parse.cpp`, `luke2_lower.cpp`,
`luke2_migrate.cpp`. Touched: `main.cpp` (`.lk` + `MIGRATE`), `Makefile`. Codegen untouched.

```bash
cd vm && make && make test-syntax-v2
```

Three findings from building it, all now in the spec:

1. **`+` genuinely needs types.** v1 spells numeric addition `ADD a AND b` and concatenation
   `a AND b`, and `ADD` rejects `TEXT` — there is no type-agnostic form. The lowerer carries a
   small type environment and reads `GIVES BACK` clauses straight out of `vm/stdlib/*.luke`, so
   stdlib return types cannot drift from a hand-maintained table.
2. **v1 concatenation is right-associative, arithmetic is left-associative.** The lowerer always
   parenthesises, which makes `-` and `/` immune; the cost is a differently-shaped (semantically
   identical) tree for string `+`, which the harness treats as expected.
3. **`effect` needs its cell.** v1 requires `WHEN REACTIVE c CHANGES DO`, so the v2 form is
   `effect on c { … }`, not a bare `effect { … }`. The spec previously had this wrong.

### Phases 0 and 1

| Item | State |
| --- | --- |
| Nine decisions signed off | done — [§8](#8-open-decisions-needing-sign-off) |
| `STRATEGY.md` amended (identity, "what we keep", decision table) | done |
| `SYNTAX_V2_SPEC.md` covering every live phrase | done — gate: `scripts/syntax_v2_spec_check.py` |
| 10-file golden corpus | done — [`examples/v2/`](../examples/v2/), gate: `scripts/syntax_v2_corpus_check.py` |

Both gates run in CI. Because this is a solo project with no second reviewer, the automated gates
*are* the review — that is deliberate, and it is why the spec is machine-checked against codegen
rather than maintained by hand.

```bash
python3 scripts/syntax_surface_census.py     # the measurements
python3 scripts/syntax_v2_spec_check.py      # every live phrase is mapped or dropped
python3 scripts/syntax_v2_corpus_check.py    # corpus paired with v1 twins, no v1 leakage
```

### Next: post-window cleanup

1. After the deprecation window: delete the v1 statement parser and strip conversational
   phrase prefixes from `build_c.cpp` (plan Phase 5 step 2).
2. Optional later: codegen consumes the AST directly (rejected-as-blocker for the flip).
