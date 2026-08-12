# LukeLang scorecard (evidence bar)

> External “A+ language” grades are **evidence**, not a new product vision.
> Updated after Execution A+ (partner EXISTS, N-table join chains, bag aggregates).

| Dimension | Grade | Evidence |
| --- | --- | --- |
| **Honesty** | **A+** | Claims match tests; Live Graph differentials are real `WHEN NEW` / EXISTS / bag — not recompute theater |
| **Architecture** | **A** | `parseLuke` → `Program`/`Stmt`/`Ast` → `flattenProgram` → emit on every BUILD; Pratt exprs; IF/WHILE/FUNCTION/WHEN are nested Stmt nodes |
| **Execution** | **A+** | Keyed 2-table + N-table join IVM, filter+EXISTS partner, bag `group_concat`, wire fail-closed, C10K beachhead, wall smoke |
| **Ambition** | **A+** | Vision held — reactive full-stack; mobile/game parked |
| **Tooling** | **A** | `luke LSP` hover/completion/definition on Stmt AST; `luke FMT` / `FMT -e` via `formatProgram`/`formatExpr`; IR shows AST |
| **Adoption** | **A-** | Wall deploy proof + Caddy; `site/` stub; registry `sha256`; public DNS/traffic still to earn |

## What “Execution A+” required (and shipped)

1. Partner-table writes bounded by join-key `EXISTS` (not full partner recompute).
2. 3+ table equi-JOIN chains with equality-closure `WHEN` + NEW-pinned probes (`live_graph_join3`).
3. Differential aggregates: bag table maintained under insert/update/delete (`live_graph_agg`).

## Still not A+ everywhere (honest remainder)

| Gap | Why it is not a vision change |
| --- | --- |
| Some surface still `StmtKind::Raw` (ROUTES/FLOW/…) | Specialize nodes without inventing a second IR |
| Debugger | Tooling depth after LSP/FMT |
| Live lukelang.org + signed registry keys | Adoption after the wall sentence |
| LEFT JOIN / non-equi ON | Broader SQL shapes — not the Execution A+ bar |

**Do not dilute:** un-parking mobile/game to chase a checklist is how Ambition drops.
