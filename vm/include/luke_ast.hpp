#pragma once
/* Luke shared IR — expressions + statements + program.
 *
 * One AST for codegen, LSP, formatter, and IR dump.
 * Pipeline: source → parseLuke → Program → lower/emit → C
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
  OpAdd,  /* ADD */
  OpSub,  /* SUBTRACT */
  OpMul,  /* MULTIPLY / MULTIPLIED BY */
  OpDiv,  /* DIVIDE / DIVIDED BY */
  OpAnd,  /* AND (text concat) */
  OpEq,   /* EQUALS */
  OpLt,   /* IS LESS THAN */
  OpGt,   /* IS GREATER THAN */
  OpLe,
  OpGe,
  OpNot,  /* NOT */
  Newline,
  Comment,
  Keyword, /* reserved word kept as Ident-class for stmts */
  Unknown
};

struct Token {
  TokKind kind = TokKind::End;
  std::string text;
  size_t pos = 0;
  size_t line = 1; /* 1-based source line */
};

enum class AstKind {
  Ident,
  Number,
  String,
  Unary,
  Binary,
  Group,
  Call, /* ASK f WITH … */
  Empty
};

struct Ast {
  AstKind kind = AstKind::Empty;
  std::string text;
  TokKind op = TokKind::Unknown;
  std::vector<Ast> kids;
  size_t line = 0;
};

enum class StmtKind {
  Empty,
  Import,
  Speak,
  Let,          /* MY NAME IS … [AS ty] [SET TO expr] */
  Remember,     /* REMEMBER / SECRET REMEMBER */
  Change,       /* CHANGE cell TO expr */
  Set,          /* SET name TO expr */
  Ask,          /* ASK … (statement form) */
  GiveBack,
  If,
  While,
  MakeSure,
  AddTo,        /* ADD x TO list */
  PutIn,        /* PUT k TO v IN map */
  Function,     /* THIS IS FUNCTION … body … END FUNCTION */
  Blueprint,
  WhenReactive, /* WHEN REACTIVE cell CHANGES DO … END WHEN REACTIVE */
  When,         /* WHEN … DO … END WHEN */
  Block,        /* generic DO…END body holder */
  Watch,
  PushWatch,
  Bind,
  WearStyle,
  NamePage,
  BeginColumn,  /* layout openers kept structured when recognized */
  EndColumn,
  LayOut,
  Paint,
  Raw, /* unclassified — preserved text; still part of the Program AST */
};

struct Stmt {
  StmtKind kind = StmtKind::Empty;
  std::string text;       /* original / reconstructed line or header */
  std::string name;       /* primary identifier (cell, fn, blueprint, …) */
  std::string typeName;   /* AS NUMBER / TEXT / … */
  std::string aux;        /* secondary string (import spec, event, …) */
  bool flag = false;      /* SECRET, BACKGROUND, WEAK, … */
  Ast expr;               /* primary expression */
  Ast expr2;              /* secondary expression when needed */
  std::vector<Stmt> body;
  std::vector<Stmt> elseBody;
  std::vector<Ast> args;  /* function params as Ident ASTs, etc. */
  size_t line = 0;
  size_t endLine = 0;
};

struct Program {
  std::vector<Stmt> stmts;
  std::string error;
  size_t errorLine = 0;
  bool ok() const { return error.empty(); }
};

const char *stmtKindName(StmtKind k);
const char *astKindName(AstKind k);

/* Pretty dump for IR / tests — one line per node. */
std::string dumpProgram(const Program &p);

} // namespace luke
