#pragma once
/* Production expression frontend: tokenize → Pratt AST → lower to C snippets.
 * Statement parsing remains in build_c.cpp (line-based) until stmt AST lands.
 */

#include "luke_ast.hpp"

#include <functional>
#include <string>
#include <vector>

namespace luke {

struct ExprLower {
  /* Resolve a primary identifier / keyword phrase (already stripped).
   * Returns {c_code, type_tag} where type_tag is one of:
   *   "num" | "int" | "text" | "flag" | "err"
   */
  using ResolveFn = std::function<std::pair<std::string, std::string>(const std::string &atom,
                                                                     size_t line)>;
  using FailFn = std::function<void(size_t line, const std::string &msg)>;

  ResolveFn resolve;
  FailFn fail;
  std::string arenaVar = "arena"; /* for text concat */
};

/* Tokenize a Luke expression (word-ops, parens, literals). */
std::vector<Token> tokenizeExpr(const std::string &src);

/* Pratt-parse tokens into Ast. On error, calls fail and returns empty Ident.
 * If outConsumed is set, it receives the token index after the expression
 * (should point at End for a complete parse). */
Ast parseExprAst(const std::vector<Token> &toks, size_t line, const ExprLower &ctx,
                 size_t *outConsumed = nullptr);

/* Lower Ast to C code + type tag (num/int/text/flag). */
std::pair<std::string, std::string> lowerExprAst(const Ast &ast, size_t line, const ExprLower &ctx);

/* One-shot: source → {code, type_tag}. Falls back to empty on total failure. */
std::pair<std::string, std::string> compileExpr(const std::string &src, size_t line,
                                               const ExprLower &ctx);

/* Pretty-print expression source with normalized spacing (formatter beachhead). */
std::string formatExpr(const std::string &src);

} // namespace luke
