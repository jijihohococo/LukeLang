# LukeLang scorecard (evidence bar)

> External “A+ language” grades are **evidence**, not a new product vision.
> Updated after the Program AST ship (`docs/AST.md`).

| Dimension | Grade | Evidence |
| --- | --- | --- |
| **Honesty** | **A+** | Claims match tests; Live Graph join differential is real `WHEN NEW`; AST dump in `luke IR` |
| **Architecture** | **A** | `parseLuke` → `Program`/`Stmt`/`Ast` → `flattenProgram` → emit on every BUILD; Pratt exprs; IF/WHILE/FUNCTION/WHEN are nested Stmt nodes |
| **Execution** | **A** | Keyed join IVM, wire fail-closed, C10K beachhead, wall smoke |
| **Ambition** | **A+** | Vision held — reactive full-stack; mobile/game parked |
| **Tooling** | **A** | `luke LSP` hover/completion/definition on Stmt AST; `luke FMT` / `FMT -e` via `formatProgram`/`formatExpr`; IR shows AST |
| **Adoption** | **A-** | Wall deploy proof + Caddy; `site/` stub; registry `sha256`; public DNS/traffic still to earn |

## What “A” required (and shipped)

1. **Lexer + expression Pratt** filling `luke_ast.hpp`, lowered in codegen.
2. **Statement AST** — not “exprs only”: `luke_parse.cpp` builds a real `Program`.
3. **Tooling rides that IR** — no second JS parser.

## Still not A+ everywhere (honest remainder)

| Gap | Why it is not a vision change |
| --- | --- |
| Some surface still `StmtKind::Raw` (ROUTES/FLOW/…) | Specialize nodes without inventing a second IR |
| Debugger | Tooling depth after LSP/FMT |
| Live lukelang.org + signed registry keys | Adoption after the wall sentence |

**Do not dilute:** un-parking mobile/game to chase a checklist is how Ambition drops.
