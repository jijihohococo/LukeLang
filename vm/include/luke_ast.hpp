#pragma once
/* Luke expression AST beachhead — tokenizer + nodes for shared IR.
 *
 * build_c.cpp still owns statement parsing (line-based). Expressions now
 * respect parentheses via stripOuterParens + findOutsideQuotes(depth).
 * This header is the stable shape LSP / formatter / reactive analysis will
 * eventually share — do not invent a second JS-side parser.
 *
 * Roadmap: Pratt/recursive-descent fills these nodes; codegen lowers Ast → C.
 */

#include <string>
#include <vector>

namespace luke {

enum class TokKind {
  End,
  Ident,
  Number,
  String,
  LParen,
  RParen,
  OpAdd,      /* ADD */
  OpSub,      /* SUBTRACT */
  OpMul,      /* MULTIPLY / MULTIPLIED BY */
  OpDiv,      /* DIVIDE / DIVIDED BY */
  OpAnd,      /* AND (text concat) */
  OpEq,       /* EQUALS */
  OpLt,       /* IS LESS THAN */
  OpGt,       /* IS GREATER THAN */
  OpLe,
  OpGe,
  OpNot,      /* NOT */
  Unknown
};

struct Token {
  TokKind kind = TokKind::End;
  std::string text; /* original lexeme */
  size_t pos = 0;
};

enum class AstKind {
  Ident,
  Number,
  String,
  Unary,
  Binary,
  Group,
  Call /* ASK f WITH … — later */
};

struct Ast {
  AstKind kind = AstKind::Ident;
  std::string text;
  TokKind op = TokKind::Unknown;
  std::vector<Ast> kids;
  size_t line = 0;
};

} // namespace luke
