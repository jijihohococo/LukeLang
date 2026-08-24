#pragma once
/* Syntax v2 front-end: technical source (.lk) → conversational v1 source.
 *
 * Strategy (docs/SYNTAX_V2_PLAN.md §3): codegen consumes v1 *text*, so v2 is a
 * front-end that lowers to v1 and reuses the entire existing backend. Nothing in
 * here touches build_c.cpp.
 *
 * Line fidelity: the lowerer emits `// @luke-file "<path>" N` markers so build
 * errors, #line maps, gdb and LSP all report .lk positions (spec §11.2).
 */

#include <map>
#include <set>
#include <string>
#include <vector>

namespace luke2 {

/* ---------- tokens ---------- */

enum class Tk { End, Newline, Ident, Int, Float, Str, Punct };

struct Tok {
  Tk kind = Tk::End;
  std::string text;  /* identifier / punctuation / literal spelling */
  std::string raw;   /* string literals: original bytes incl. escapes */
  size_t line = 1;
};

/* Tokenize v2 source. Never throws; lexical errors surface as Tk::End with err set. */
std::vector<Tok> lex(const std::string &src, std::string *err, size_t *errLine);

/* ---------- expressions ---------- */

enum class Ek { Int, Float, Str, Bool, Ident, Unary, Binary, Call, Method, Index, Field, ListLit, MapLit };

struct Ex {
  Ek k = Ek::Ident;
  std::string s;              /* name / operator / literal text */
  std::vector<Ex> kids;
  size_t line = 0;
};

/* ---------- statements ---------- */

enum class Sk {
  Let, Assign, OpAssign, ExprStmt, If, While, For, Fn, Return, Struct,
  FieldDecl, Init, Method, Try, Throw, Test, Assert, Import, Arena,
  Signal, Derived, Effect, Batch, Watch, PushWatch, Break, Routes, Route
};

struct St {
  Sk k = Sk::ExprStmt;
  std::string name;    /* binding / function / struct / method name */
  std::string type;    /* v2 type annotation */
  std::string aux;     /* parent struct, catch var, import spec, watch source … */
  std::string aux2;    /* watch predicate, push-watch tail … */
  bool isVar = false;  /* var vs let */
  bool secret = false;
  bool isPrivate = false;
  Ex e, e2;
  std::vector<St> body, body2;                              /* body2: else / catch */
  std::vector<std::pair<std::string, std::string>> params;  /* name, type */
  size_t line = 0;
};

struct Program {
  std::vector<St> stmts;
  std::string error;
  size_t errorLine = 0;
  bool ok() const { return error.empty(); }
};

/* Parse v2 tokens into a Program. On error, Program::error is set. */
Program parse(const std::vector<Tok> &toks);

/* ---------- lowering ---------- */

struct LowerOptions {
  std::string sourcePath;  /* emitted in @luke-file markers */
  std::string stdlibDir;   /* to read v1 stdlib signatures for return types */
  bool emitMarkers = true;
};

struct Result {
  bool ok = false;
  std::string v1;
  std::string error;
  size_t line = 0;
};

/* Lower a parsed v2 program to v1 conversational source. */
Result lower(const Program &p, const LowerOptions &opt);

/* One-shot: v2 source → v1 source. */
Result lowerSource(const std::string &src, const LowerOptions &opt);

/* True when the path should be treated as Syntax v2. */
bool isV2Path(const std::string &path);

/* ---------- migration (v1 → v2) ---------- */

struct MigrateResult {
  bool ok = false;
  std::string v2;
  std::string error;
  size_t line = 0;
  int todos = 0; /* Raw lines that became TODO(migrate) comments */
};

/* Migrate conversational v1 source to technical v2.
 * Structured stmts + high-value Raw patterns become v2.
 * Unrecognised Raw becomes `// TODO(migrate): …` and increments todos. */
MigrateResult migrateSource(const std::string &v1Source);

}  // namespace luke2
