# LukeLang scorecard (evidence bar)

> External “A+ language” grades are **evidence**, not a new product vision.
> Updated after true Execution A+ (no recompute fallbacks for supported shapes; multi-row JOIN cards).

| Dimension | Grade | Evidence |
| --- | --- | --- |
| **Honesty** | **A+** | Claims match tests; Live Graph differentials are real `WHEN NEW` / join-bag / bag — not recompute theater |
| **Architecture** | **A** | `parseLuke` → `Program`/`Stmt`/`Ast` → `flattenProgram` → emit on every BUILD; Pratt exprs; IF/WHILE/FUNCTION/WHEN are nested Stmt nodes |
| **Execution** | **A+** | Point + multi-row join IVM, inequality bag filters, N-table chains, wire fail-closed, C10K beachhead, wall smoke |
| **Ambition** | **A+** | Vision held — reactive full-stack; mobile/game parked |
| **Tooling** | **A** | LSP (hover types/signatures + outline/refs/rename/signatureHelp/FMT/semantic tokens/code actions) + FMT + `#line` maps; `luke DEBUG`/`DAP` |
| **Adoption** | **A-** | Wall deploy proof + Caddy; `site/` stub; registry `sha256`; public DNS/traffic still to earn |

## What “Execution A+” required (and shipped)

1. Partner / non-point joins: rowid-keyed **join bag** (`luke_ivm_jbag_*`) — multi-row `group_concat`, not scalar-only cards.
2. 3+ table equi-JOIN point chains with equality-closure `WHEN` + NEW-pinned probes (`live_graph_join3`).
3. Differential aggregates for **any** NEW./OLD.-qualifiable filter (equality, inequality, LIKE, …) via `luke_ivm_bag_*` (`live_graph_agg`, `live_graph_agg_range`).

## Still not A+ everywhere (honest remainder)

| Gap | Why it is not a vision change |
| --- | --- |
| Some surface still `StmtKind::Raw` (ROUTES/FLOW/…) | Specialize nodes without inventing a second IR |
| Debugger | `#line` + `luke DEBUG` break/step/inspect + `luke DAP` beachhead; richer IDE UX still open |
| Live lukelang.org + signed registry keys | Adoption after the wall sentence |
| LEFT JOIN / non-equi ON / expression-only SELECT lists | Broader SQL shapes outside the equi-JOIN + simple-column bag |

**Do not dilute:** un-parking mobile/game to chase a checklist is how Ambition drops.
