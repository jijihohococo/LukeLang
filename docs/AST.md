# Luke AST — shared IR

> **Status:** production pipeline — every `luke BUILD` / `IR` / `FMT` / `LSP` path goes through `parseLuke`  
> **Files:** `vm/include/luke_ast.hpp`, `vm/src/luke_expr.cpp`, `vm/src/luke_parse.cpp`

## Pipeline

```
source  →  IMPORT expand  →  parseLuke → Program (Stmt + Expr AST)
                                      ↓
                               flattenProgram
                                      ↓
                               BC lower / emit → C
```

Expressions use Pratt (`tokenizeExpr` → `parseExprAst` → `lowerExprAst`).
Statements are first-class `Stmt` nodes (`Speak`, `Let`, `If`, `While`, `Function`,
`WhenReactive`, …). Forms not yet specialized remain `StmtKind::Raw` but are still
nodes in the Program — there is no parallel line-only IR for tooling.

## Tooling on the same IR

| Tool | Use of AST |
| --- | --- |
| `luke IR` | dumps `luke-ast-program` + stmt kinds |
| `luke FMT` / `FMT -e` | `formatProgram` / `formatExpr` — preserves casing; CI proves `BUILD(FMT(x))≡BUILD(x)` for every `examples/build` file (`scripts/fmt_roundtrip_all.sh`) |
| `luke LSP` | symbols from Stmt walk; rich hover; outline/refs/rename/signatureHelp/FMT/semantic tokens/code actions; context completion; didChange re-diagnose |

## Evidence

```bash
luke IR examples/build/hello.luke          # --- ast --- Speak/Let …
luke BUILD examples/build/ast_roundtrip.luke
luke FMT examples/build/expr_pratt.luke
```

See [`STRATEGY.md`](./STRATEGY.md) scorecard — Architecture rides this foundation.
