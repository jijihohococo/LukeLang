/* Syntax v2 migrator — conversational v1 Program AST → technical v2 source.
 *
 * Strategy (docs/SYNTAX_V2_PLAN.md Phase 3): print v2 from the existing parseLuke
 * AST. Structured StmtKinds map directly; high-value Raw phrases are rewritten;
 * anything else becomes `// TODO(migrate): …` (Phase 3b). Mutation analysis
 * chooses let vs var (spec §2.1). Does not touch build_c.cpp.
 */

#include "luke2.hpp"
#include "luke_ast.hpp"
#include "luke_parse.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace luke2 {
namespace {

using luke::Ast;
using luke::AstKind;
using luke::Stmt;
using luke::StmtKind;
using luke::TokKind;

/* ---------- string helpers ---------- */

std::string trim(std::string s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
  size_t b = s.size();
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

std::string upper(std::string s) {
  for (char &c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

bool startsCI(const std::string &s, const char *p) {
  size_t n = 0;
  while (p[n]) ++n;
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    char a = (char)std::toupper((unsigned char)s[i]);
    char b = (char)std::toupper((unsigned char)p[i]);
    if (a != b) return false;
  }
  return true;
}

bool eqCI(const std::string &a, const char *b) {
  size_t n = 0;
  while (b[n]) ++n;
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if ((char)std::toupper((unsigned char)a[i]) !=
        (char)std::toupper((unsigned char)b[i]))
      return false;
  }
  return true;
}

/* Top-level needle search that respects "…" strings and ( ) nesting. */
size_t findTopCI(const std::string &s, const char *needle, size_t from = 0) {
  const size_t nlen = std::char_traits<char>::length(needle);
  int depth = 0;
  bool inStr = false;
  for (size_t i = from; i < s.size(); ++i) {
    char c = s[i];
    if (inStr) {
      if (c == '\\' && i + 1 < s.size()) {
        ++i;
        continue;
      }
      if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
      continue;
    }
    if (c == '(') {
      ++depth;
      continue;
    }
    if (c == ')') {
      if (depth > 0) --depth;
      continue;
    }
    if (depth != 0) continue;
    if (i + nlen > s.size()) return std::string::npos;
    bool ok = true;
    for (size_t k = 0; k < nlen; ++k) {
      char a = (char)std::toupper((unsigned char)s[i + k]);
      char b = (char)std::toupper((unsigned char)needle[k]);
      if (a != b) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    /* word-ish boundaries for alphabetic needles (not if needle ends with space) */
    auto isId = [](char ch) {
      return std::isalnum((unsigned char)ch) || ch == '_';
    };
    if (nlen > 0 && std::isalpha((unsigned char)needle[0])) {
      if (i > 0 && isId(s[i - 1])) continue;
      char last = needle[nlen - 1];
      if (std::isalnum((unsigned char)last) || last == '_') {
        if (i + nlen < s.size() && isId(s[i + nlen])) continue;
      }
    }
    return i;
  }
  return std::string::npos;
}

std::vector<std::string> splitTopComma(const std::string &s) {
  std::vector<std::string> out;
  int depth = 0;
  bool inStr = false;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (inStr) {
      if (c == '\\' && i + 1 < s.size()) {
        ++i;
        continue;
      }
      if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
      continue;
    }
    if (c == '(') {
      ++depth;
      continue;
    }
    if (c == ')') {
      if (depth > 0) --depth;
      continue;
    }
    if (depth == 0 && c == ',') {
      out.push_back(trim(s.substr(start, i - start)));
      start = i + 1;
    }
  }
  out.push_back(trim(s.substr(start)));
  return out;
}

/* ---------- types ---------- */

bool isV1TypeWord(const std::string &t) {
  auto u = upper(trim(t));
  return u == "NUMBER" || u == "INTEGER" || u == "TEXT" || u == "FLAG" ||
         u == "JSON" || u == "LIST" || u == "MAP" || u == "SERVER" ||
         u == "REQUEST" || u == "DATABASE" || u == "BLUEPRINT";
}

std::string v2Type(const std::string &t) {
  auto u = upper(trim(t));
  if (u == "NUMBER") return "float";
  if (u == "INTEGER") return "int";
  if (u == "TEXT") return "str";
  if (u == "FLAG") return "bool";
  if (u == "JSON") return "json";
  if (u == "LIST") return "list";
  if (u == "MAP") return "map";
  if (u == "SERVER") return "Server";
  if (u == "REQUEST") return "Request";
  if (u == "DATABASE") return "Db";
  return trim(t); /* struct / custom */
}

bool looksLikeValue(const std::string &t) {
  auto s = trim(t);
  if (s.empty()) return false;
  if (isV1TypeWord(s)) return false;
  if (eqCI(s, "TRUE") || eqCI(s, "FALSE")) return true;
  if (s[0] == '"' || s[0] == '\'') return true;
  if (std::isdigit((unsigned char)s[0]) ||
      (s[0] == '-' && s.size() > 1 && std::isdigit((unsigned char)s[1])))
    return true;
  /* bare identifier used as REMEMBER … AS name — treat as value expression */
  return true;
}

/* ---------- expression rewrite ---------- */

std::string rewriteExpr(std::string s);

std::string rewriteIdentSpelling(std::string s) {
  s = trim(s);
  if (eqCI(s, "TRUE")) return "true";
  if (eqCI(s, "FALSE")) return "false";
  if (eqCI(s, "THE CURRENT USER")) return "current_user";
  if (eqCI(s, "THE CLOCK") || eqCI(s, "THE TIME IN MILLISECONDS") ||
      eqCI(s, "THE CLOCK IN MILLISECONDS"))
    return "clock";
  if (eqCI(s, "THE GRANULAR PAINT COUNT") || eqCI(s, "GRANULAR PAINTS") ||
      eqCI(s, "THE GRANULAR PAINTS"))
    return "granular_paint_count";
  if (eqCI(s, "THE REGION PAINT COUNT")) return "region_paint_count";
  if (eqCI(s, "THE REACTIVE ERROR COUNT")) return "reactive_error_count";
  if (eqCI(s, "THE ERROR ISOLATION COUNT")) return "error_isolation_count";
  if (eqCI(s, "THE LAST ERROR NODE")) return "last_error_node";
  if (eqCI(s, "THE ASYNC FAILURE COUNT")) return "async_failure_count";
  if (eqCI(s, "THE FLUSH COUNT")) return "flush_count";
  if (eqCI(s, "THE DIRTY COUNT")) return "dirty_count";
  if (eqCI(s, "THE DERIVED COUNT")) return "derived_count";
  if (eqCI(s, "THE ALIVE COUNT")) return "alive_count";
  if (eqCI(s, "THE STALE COUNT")) return "stale_count";
  if (eqCI(s, "THE SUBTREE COUNT")) return "subtree_count";
  if (eqCI(s, "THE WEAK COUNT")) return "weak_count";
  if (eqCI(s, "THE LEAK COUNT")) return "leak_count";
  if (eqCI(s, "THE SCOPE COUNT")) return "scope_count";
  if (eqCI(s, "THE SCHEDULER COUNT") || eqCI(s, "THE SCHEDULER TICK")) return "scheduler_count";
  if (eqCI(s, "THE LAST ERROR NODE")) return "last_error_node";
  if (eqCI(s, "THE BENCH MEDIAN")) return "bench_median";
  if (eqCI(s, "THE BENCH MIN")) return "bench_min";
  if (eqCI(s, "THE BENCH MAX")) return "bench_max";
  if (eqCI(s, "THE BENCH SAMPLE COUNT")) return "bench_sample_count";
  /* SELF.foo → self.foo (also mid-token SELF.) */
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if ((i == 0 || !std::isalnum((unsigned char)s[i - 1])) &&
        i + 5 <= s.size() && upper(s.substr(i, 5)) == "SELF." ) {
      out += "self.";
      i += 5;
      continue;
    }
    out.push_back(s[i++]);
  }
  return out;
}

std::string rewriteArgsList(const std::string &args) {
  auto parts = splitTopComma(args);
  std::ostringstream o;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) o << ", ";
    o << rewriteExpr(parts[i]);
  }
  return o.str();
}

std::string bin(const std::string &a, const char *op, const std::string &b) {
  return rewriteExpr(a) + " " + op + " " + rewriteExpr(b);
}

std::string rewriteExpr(std::string s) {
  s = trim(s);
  if (s.empty()) return s;

  /* strip one layer of grouping parens when balanced */
  if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
    int d = 0;
    bool bal = true;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
      if (s[i] == '"' ) {
        ++i;
        while (i + 1 < s.size() && s[i] != '"') {
          if (s[i] == '\\') ++i;
          ++i;
        }
        continue;
      }
      if (s[i] == '(') ++d;
      else if (s[i] == ')') {
        --d;
        if (d == 0 && i + 1 < s.size() - 1) {
          bal = false;
          break;
        }
      }
    }
    if (bal && d == 1) return rewriteExpr(s.substr(1, s.size() - 2));
  }

  if (startsCI(s, "ASK ")) {
    auto rest = trim(s.substr(4));
    /* ASK f  (no WITH) → f() */
    if (findTopCI(rest, " WITH ") == std::string::npos &&
        findTopCI(rest, " TO ") == std::string::npos)
      return rewriteExpr(rest) + "()";
    return rewriteExpr(rest);
  }

  if (eqCI(s, "THE CURRENT USER")) return "current_user";
  if (eqCI(s, "TRUE")) return "true";
  if (eqCI(s, "FALSE")) return "false";

  /* NEW Type WITH args / NEW Type */
  if (startsCI(s, "NEW ")) {
    auto rest = trim(s.substr(4));
    auto w = findTopCI(rest, " WITH ");
    if (w != std::string::npos) {
      auto ty = trim(rest.substr(0, w));
      auto args = trim(rest.substr(w + 6));
      return ty + "(" + rewriteArgsList(args) + ")";
    }
    return rewriteIdentSpelling(rest) + "()";
  }

  /* ADD a AND b */
  if (startsCI(s, "ADD ")) {
    auto rest = trim(s.substr(4));
    auto a = findTopCI(rest, " AND ");
    if (a != std::string::npos)
      return bin(rest.substr(0, a), "+", rest.substr(a + 5));
  }

  /* infix: a ADD b (derived / arithmetic without AND) */
  {
    auto p = findTopCI(s, " ADD ");
    if (p != std::string::npos)
      return bin(s.substr(0, p), "+", s.substr(p + 5));
  }

  /* SUBTRACT b FROM a */
  if (startsCI(s, "SUBTRACT ")) {
    auto rest = trim(s.substr(9));
    auto f = findTopCI(rest, " FROM ");
    if (f != std::string::npos)
      return bin(rest.substr(f + 6), "-", rest.substr(0, f));
  }

  /* HOW MANY / ITEM / GET often appear on the left of EQUALS in MAKE SURE.
   * Resolve comparison / arithmetic phrases before those prefix forms. */
  struct BinOp {
    const char *needle;
    const char *op;
  };
  static const BinOp kBins[] = {
      {" IS GREATER THAN OR EQUAL TO ", ">="},
      {" IS LESS THAN OR EQUAL TO ", "<="},
      {" IS GREATER THAN ", ">"},
      {" IS LESS THAN ", "<"},
      {" IS NOT ", "!="},
      {" EQUALS ", "=="},
      {" MULTIPLIED BY ", "*"},
      {" DIVIDED BY ", "/"},
      {" SUBTRACT ", "-"},
      {" MULTIPLY ", "*"},
      {" DIVIDE ", "/"},
      {" MOD ", "%"},
      {" OR ", "||"},
  };
  for (const auto &b : kBins) {
    auto p = findTopCI(s, b.needle);
    if (p != std::string::npos)
      return bin(s.substr(0, p), b.op,
                 s.substr(p + std::char_traits<char>::length(b.needle)));
  }

  /* HOW MANY IN x */
  {
    auto p = findTopCI(s, "HOW MANY IN ");
    if (p == 0) return rewriteExpr(s.substr(12)) + ".len()";
  }
  /* ITEM i OF x */
  if (startsCI(s, "ITEM ")) {
    auto rest = trim(s.substr(5));
    auto o = findTopCI(rest, " OF ");
    if (o != std::string::npos)
      return rewriteExpr(rest.substr(o + 4)) + "[" + rewriteExpr(rest.substr(0, o)) +
             "]";
  }
  /* LAST OF x */
  if (startsCI(s, "LAST OF ")) return rewriteExpr(s.substr(8)) + ".last()";
  /* GET k FROM m */
  if (startsCI(s, "GET ")) {
    auto rest = trim(s.substr(4));
    auto f = findTopCI(rest, " FROM ");
    if (f != std::string::npos)
      return rewriteExpr(rest.substr(f + 6)) + "[" + rewriteExpr(rest.substr(0, f)) +
             "]";
  }
  /* HAS KEY k IN m */
  if (startsCI(s, "HAS KEY ")) {
    auto rest = trim(s.substr(8));
    auto i = findTopCI(rest, " IN ");
    if (i != std::string::npos)
      return rewriteExpr(rest.substr(i + 4)) + ".has(" +
             rewriteExpr(rest.substr(0, i)) + ")";
  }

  /* NOT x */
  if (startsCI(s, "NOT ")) return "!" + rewriteExpr(s.substr(4));

  /* obj TO meth WITH args / obj TO meth */
  {
    auto t = findTopCI(s, " TO ");
    if (t != std::string::npos) {
      auto recv = trim(s.substr(0, t));
      auto rest = trim(s.substr(t + 4));
      auto w = findTopCI(rest, " WITH ");
      if (w != std::string::npos) {
        auto meth = trim(rest.substr(0, w));
        auto args = trim(rest.substr(w + 6));
        return rewriteExpr(recv) + "." + meth + "(" + rewriteArgsList(args) + ")";
      }
      /* bare method call */
      if (!rest.empty() && findTopCI(rest, " ") == std::string::npos)
        return rewriteExpr(recv) + "." + rest + "()";
    }
  }

  /* f WITH args — call */
  {
    auto w = findTopCI(s, " WITH ");
    if (w != std::string::npos) {
      auto f = trim(s.substr(0, w));
      auto args = trim(s.substr(w + 6));
      if (!f.empty() && findTopCI(f, " ") == std::string::npos)
        return rewriteExpr(f) + "(" + rewriteArgsList(args) + ")";
    }
  }

  /* a AND b … — text concat (Build overloads AND; migrator always uses +) */
  {
    auto p = findTopCI(s, " AND ");
    if (p != std::string::npos)
      return bin(s.substr(0, p), "+", s.substr(p + 5));
  }

  /* string / number literals pass through; rewrite SELF. in idents */
  if (!s.empty() && s[0] == '"') return s;
  if (!s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '-')) return s;
  return rewriteIdentSpelling(s);
}

std::string emitAst(const Ast &a) {
  switch (a.kind) {
    case AstKind::Empty:
      return {};
    case AstKind::String: {
      std::ostringstream o;
      o << '"';
      for (char c : a.text) {
        if (c == '\\' || c == '"') o << '\\';
        o << c;
      }
      o << '"';
      return o.str();
    }
    case AstKind::Number:
      return a.text;
    case AstKind::Ident:
      /* May be a conversational blob (ADD a AND b, ASK f WITH …). */
      return rewriteExpr(a.text);
    case AstKind::Unary: {
      std::string op = "!";
      if (a.op == TokKind::OpNot) op = "!";
      else if (a.op == TokKind::OpSub) op = "-";
      std::string inner = a.kids.empty() ? std::string() : emitAst(a.kids[0]);
      return op + inner;
    }
    case AstKind::Binary: {
      std::string L = a.kids.size() > 0 ? emitAst(a.kids[0]) : "";
      std::string R = a.kids.size() > 1 ? emitAst(a.kids[1]) : "";
      const char *op = "+";
      switch (a.op) {
        case TokKind::OpAnd:
          op = "+";
          break; /* Build AST OpAnd is concat */
        case TokKind::OpAdd:
          op = "+";
          break;
        case TokKind::OpSub:
          op = "-";
          break;
        case TokKind::OpMul:
          op = "*";
          break;
        case TokKind::OpDiv:
          op = "/";
          break;
        case TokKind::OpEq:
          op = "==";
          break;
        case TokKind::OpLt:
          op = "<";
          break;
        case TokKind::OpGt:
          op = ">";
          break;
        case TokKind::OpLe:
          op = "<=";
          break;
        case TokKind::OpGe:
          op = ">=";
          break;
        default:
          op = "+";
          break;
      }
      return L + " " + op + " " + R;
    }
    case AstKind::Group:
      return a.kids.empty() ? std::string("()") : ("(" + emitAst(a.kids[0]) + ")");
    case AstKind::Call: {
      /* Rarely structured; fall back to text. */
      if (!a.text.empty()) return rewriteExpr(a.text);
      std::ostringstream o;
      o << (a.kids.empty() ? "?" : emitAst(a.kids[0])) << "(";
      for (size_t i = 1; i < a.kids.size(); ++i) {
        if (i > 1) o << ", ";
        o << emitAst(a.kids[i]);
      }
      o << ")";
      return o.str();
    }
  }
  return rewriteExpr(a.text);
}

std::string emitExpr(const Ast &a) {
  if (a.kind == AstKind::Empty) return {};
  /* Prefer structured tree when it is richer than a bare Ident blob. */
  if (a.kind == AstKind::Ident && !a.text.empty()) return rewriteExpr(a.text);
  if (a.kind != AstKind::Empty && a.kind != AstKind::Ident) return emitAst(a);
  return emitAst(a);
}

/* ---------- params / headers ---------- */

std::vector<std::pair<std::string, std::string>> parseParams(const std::string &aux) {
  std::vector<std::pair<std::string, std::string>> out;
  for (auto &part : splitTopComma(aux)) {
    auto p = part;
    auto u = upper(p);
    auto as = u.find(" AS ");
    if (as != std::string::npos) {
      out.push_back({trim(p.substr(0, as)), v2Type(trim(p.substr(as + 4)))});
    } else if (!trim(p).empty()) {
      out.push_back({trim(p), ""});
    }
  }
  return out;
}

std::string emitParams(const std::vector<std::pair<std::string, std::string>> &ps) {
  std::ostringstream o;
  o << "(";
  for (size_t i = 0; i < ps.size(); ++i) {
    if (i) o << ", ";
    o << ps[i].first;
    if (!ps[i].second.empty()) o << ": " << ps[i].second;
  }
  o << ")";
  return o.str();
}

std::string baseAssignName(const std::string &n) {
  /* SELF.x is not a let-binding name; "answered" is. */
  auto s = trim(n);
  if (startsCI(s, "SELF.")) return {};
  auto dot = s.find('.');
  if (dot != std::string::npos) return s.substr(0, dot);
  return s;
}

/* ---------- migrator ---------- */

struct Mig {
  std::ostringstream out;
  int todos = 0;
  std::set<std::string> mutated;
  std::set<std::string> declared;
  int indent = 0;

  std::string ind() const { return std::string((size_t)indent * 2, ' '); }

  void line(const std::string &s) { out << ind() << s << "\n"; }

  /* Emit opaque v1 via `raw "…"` / `raw """…"""` so BUILD still sees the original
   * statement. Counts as a TODO for the 3a grind. */
  void emitV1Raw(const std::string &raw) {
    ++todos;
    auto body = raw;
    if (body.find("\"\"\"") != std::string::npos) {
      line("/* TODO(migrate):");
      out << body << "\n";
      line("*/");
      return;
    }
    /* Single-line → escaped regular string (avoids """ colliding with a trailing "). */
    if (body.find('\n') == std::string::npos) {
      std::string esc;
      for (char c : body) {
        if (c == '\\' || c == '"') esc.push_back('\\');
        esc.push_back(c);
      }
      line(std::string("raw \"") + esc + "\"");
      return;
    }
    /* Multi-line triple-quote: if body ends with ", add a newline so the closer
     * is not absorbed into a 4-quote run (`" """` → dangling quote). */
    if (!body.empty() && body.back() == '"') body.push_back('\n');
    line(std::string("raw \"\"\"") + body + "\"\"\"");
  }

  void todo(const std::string &raw) { emitV1Raw(raw); }

  void collectMut(const Stmt &s) {
    switch (s.kind) {
      case StmtKind::Set: {
        auto b = baseAssignName(s.name);
        if (!b.empty()) {
          /* First SET of a bare name is a declaration (SET buddy TO NEW …);
           * later SETs of the same name mark it mutable. */
          if (declared.count(b)) mutated.insert(b);
          else declared.insert(b);
        }
        break;
      }
      case StmtKind::Change:
        mutated.insert(s.name);
        break;
      case StmtKind::AddTo:
      case StmtKind::PutIn:
        mutated.insert(s.name);
        break;
      case StmtKind::Let:
        if (declared.count(s.name)) mutated.insert(s.name);
        else declared.insert(s.name);
        break;
      case StmtKind::Raw: {
        auto t = trim(s.text);
        if (startsCI(t, "INCREASE ")) {
          auto rest = trim(t.substr(9));
          auto by = findTopCI(rest, " BY ");
          auto n = by == std::string::npos ? rest : trim(rest.substr(0, by));
          mutated.insert(n);
        } else if (startsCI(t, "DECREASE ")) {
          auto rest = trim(t.substr(9));
          auto by = findTopCI(rest, " BY ");
          auto n = by == std::string::npos ? rest : trim(rest.substr(0, by));
          mutated.insert(n);
        }
        break;
      }
      default:
        break;
    }
    for (auto &c : s.body) collectMut(c);
    for (auto &c : s.elseBody) collectMut(c);
  }

  void collectAll(const std::vector<Stmt> &stmts) {
    declared.clear();
    mutated.clear();
    for (auto &s : stmts) collectMut(s);
    /* second pass only for nested scopes: reset declared but keep mutated from
     * first full walk — collectMut already walked nested bodies. Re-run declared
     * tracking per scope during emit; mutated set is global-enough for goldens. */
  }

  bool isVar(const std::string &name) const { return mutated.count(name) > 0; }

  void emitBlock(const std::vector<Stmt> &body) {
    ++indent;
    for (auto &c : body) emitStmt(c);
    --indent;
  }

  /* Blueprint body: Raw METHOD/WHEN BORN open braces; END closes. */
  void emitBlueprintBody(const std::vector<Stmt> &body) {
    ++indent;
    for (size_t i = 0; i < body.size(); ++i) {
      const Stmt &s = body[i];
      if (s.kind == StmtKind::Raw) {
        auto t = trim(s.text);
        if (startsCI(t, "HAS ")) {
          emitHas(t);
          continue;
        }
        if (startsCI(t, "WHEN BORN")) {
          emitWhenBorn(t);
          continue;
        }
        if (eqCI(t, "END BORN") || startsCI(t, "END BORN")) {
          --indent;
          line("}");
          continue;
        }
        if (startsCI(t, "METHOD ") || startsCI(t, "PRIVATE METHOD ") ||
            startsCI(t, "SECRET METHOD ")) {
          emitMethodHeader(t);
          continue;
        }
        if (eqCI(t, "END METHOD") || startsCI(t, "END METHOD")) {
          --indent;
          line("}");
          continue;
        }
        if (startsCI(t, "CALL PARENT ")) {
          emitCallParent(t);
          continue;
        }
        /* fall through to generic raw */
      }
      emitStmt(s);
    }
    --indent;
  }

  void emitHas(const std::string &t) {
    /* HAS name AS T [SET TO e] | HAS name SET TO e */
    auto rest = trim(t.substr(4));
    auto u = upper(rest);
    auto asPos = u.find(" AS ");
    auto setPos = u.find(" SET TO ");
    bool priv = false;
    if (startsCI(rest, "PRIVATE ")) {
      priv = true;
      rest = trim(rest.substr(8));
      u = upper(rest);
      asPos = u.find(" AS ");
      setPos = u.find(" SET TO ");
    }
    std::ostringstream o;
    if (priv) o << "private ";
    if (asPos != std::string::npos && (setPos == std::string::npos || asPos < setPos)) {
      auto name = trim(rest.substr(0, asPos));
      auto after = trim(rest.substr(asPos + 4));
      auto aU = upper(after);
      auto st = aU.find(" SET TO ");
      if (st != std::string::npos) {
        o << name << ": " << v2Type(trim(after.substr(0, st))) << " = "
          << rewriteExpr(after.substr(st + 8));
      } else if (isV1TypeWord(after)) {
        o << name << ": " << v2Type(after);
      } else {
        o << name << " = " << rewriteExpr(after);
      }
    } else if (setPos != std::string::npos) {
      o << trim(rest.substr(0, setPos)) << " = "
        << rewriteExpr(rest.substr(setPos + 8));
    } else {
      todo(t);
      return;
    }
    line(o.str());
  }

  void emitWhenBorn(const std::string &t) {
    /* WHEN BORN [WITH params] DO */
    auto rest = trim(t.substr(9)); /* after WHEN BORN */
    if (startsCI(rest, "WITH ")) rest = trim(rest.substr(5));
    if (startsCI(rest, "DO")) rest = trim(rest.substr(2));
    /* strip trailing DO */
    auto u = upper(rest);
    if (u.size() >= 3 && u.substr(u.size() - 3) == " DO")
      rest = trim(rest.substr(0, rest.size() - 3));
    auto ps = parseParams(rest);
    line("init" + emitParams(ps) + " {");
    ++indent;
  }

  void emitMethodHeader(const std::string &t) {
    bool priv = startsCI(t, "PRIVATE METHOD ") || startsCI(t, "SECRET METHOD ");
    size_t off = 0;
    if (startsCI(t, "PRIVATE METHOD ")) off = 15;
    else if (startsCI(t, "SECRET METHOD ")) off = 14;
    else off = 7; /* METHOD */
    auto rest = trim(t.substr(off));
    auto u = upper(rest);
    /* strip trailing DO */
    if (u.size() >= 3 && u.substr(u.size() - 3) == " DO") {
      rest = trim(rest.substr(0, rest.size() - 3));
      u = upper(rest);
    }
    std::string name;
    std::string params;
    std::string ret;
    auto with = u.find(" WITH ");
    auto gives = u.find(" GIVES BACK ");
    if (with != std::string::npos) {
      name = trim(rest.substr(0, with));
      auto after = trim(rest.substr(with + 6));
      auto aU = upper(after);
      auto g = aU.find(" GIVES BACK ");
      if (g != std::string::npos) {
        params = trim(after.substr(0, g));
        ret = trim(after.substr(g + 12));
      } else
        params = after;
    } else if (gives != std::string::npos) {
      name = trim(rest.substr(0, gives));
      ret = trim(rest.substr(gives + 12));
    } else {
      name = trim(rest);
    }
    std::ostringstream o;
    if (priv) o << "private ";
    o << "fn " << name << emitParams(parseParams(params));
    if (!ret.empty()) o << " -> " << v2Type(ret);
    o << " {";
    line(o.str());
    ++indent;
  }

  void emitCallParent(const std::string &t) {
    /* CALL PARENT m [OF Anc] [WITH args] */
    auto rest = trim(t.substr(12));
    auto u = upper(rest);
    auto of = u.find(" OF ");
    auto with = u.find(" WITH ");
    std::string meth;
    std::string args;
    if (of != std::string::npos) {
      meth = trim(rest.substr(0, of));
      /* Anc ignored for simple super.m — goldens use bare CALL PARENT */
      auto after = trim(rest.substr(of + 4));
      auto aU = upper(after);
      auto w = aU.find(" WITH ");
      if (w != std::string::npos) {
        args = trim(after.substr(w + 6));
      }
    } else if (with != std::string::npos) {
      meth = trim(rest.substr(0, with));
      args = trim(rest.substr(with + 6));
    } else {
      meth = trim(rest);
    }
    if (args.empty())
      line("super." + meth + "()");
    else
      line("super." + meth + "(" + rewriteArgsList(args) + ")");
  }

  void emitWatchLine(const std::string &t) {
    /* WATCH name FROM src WHERE "…"
       WATCH name FROM src AS "SELECT…" [FOR CURRENT USER] */
    auto rest = trim(t.substr(6));
    auto u = upper(rest);
    auto from = u.find(" FROM ");
    if (from == std::string::npos) {
      emitV1Raw(t);
      return;
    }
    auto name = trim(rest.substr(0, from));
    auto after = trim(rest.substr(from + 6));
    auto aU = upper(after);
    bool forUser = false;
    auto forPos = aU.rfind(" FOR CURRENT USER");
    if (forPos != std::string::npos) {
      forUser = true;
      after = trim(after.substr(0, forPos));
      aU = upper(after);
    }
    auto asPos = aU.find(" AS ");
    auto wherePos = aU.find(" WHERE ");
    std::ostringstream o;
    o << "watch " << name << " from ";
    if (asPos != std::string::npos) {
      o << trim(after.substr(0, asPos)) << " as " << trim(after.substr(asPos + 4));
    } else if (wherePos != std::string::npos) {
      o << trim(after.substr(0, wherePos)) << " where "
        << trim(after.substr(wherePos + 7));
    } else {
      o << after;
    }
    if (forUser) o << " for current_user";
    line(o.str());
  }

  void emitPushWatchLine(const std::string &t) {
    /* PUSH WATCH name ON req [FOR n BEATS EVERY m MILLISECONDS] */
    auto rest = trim(t.substr(11));
    auto u = upper(rest);
    auto on = u.find(" ON ");
    if (on == std::string::npos) {
      todo(t);
      return;
    }
    auto name = trim(rest.substr(0, on));
    auto after = trim(rest.substr(on + 4));
    auto aU = upper(after);
    auto forPos = aU.find(" FOR ");
    if (forPos == std::string::npos) {
      line("push watch " + name + " on " + rewriteExpr(after));
      return;
    }
    auto req = trim(after.substr(0, forPos));
    auto tail = trim(after.substr(forPos + 5));
    auto tU = upper(tail);
    /* n BEATS EVERY m MILLISECONDS */
    auto beats = tU.find(" BEATS EVERY ");
    auto ms = tU.find(" MILLISECOND");
    if (beats != std::string::npos && ms != std::string::npos) {
      auto n = trim(tail.substr(0, beats));
      auto mid = trim(tail.substr(beats + 13));
      auto mU = upper(mid);
      auto mEnd = mU.find(" MILLISECOND");
      auto m = trim(mid.substr(0, mEnd));
      line("push watch " + name + " on " + rewriteExpr(req) + " for " + n +
           " beats every " + m + " ms");
    } else {
      line("push watch " + name + " on " + rewriteExpr(req) + " for " +
           rewriteExpr(tail));
    }
  }

  bool emitRaw(const std::string &raw) {
    auto t = trim(raw);
    if (t.empty()) return true;

    if (startsCI(t, "THE ") && findTopCI(t, " IS ") != std::string::npos) {
      auto rest = trim(t.substr(4));
      auto is = findTopCI(rest, " IS ");
      auto name = trim(rest.substr(0, is));
      auto expr = trim(rest.substr(is + 4));
      /* Infix ADD/SUBTRACT without typed operands — keep v1 (cycle/bench forms). */
      auto eU = upper(expr);
      if (eU.find(" ADD ") != std::string::npos || eU.find(" SUBTRACT ") != std::string::npos ||
          startsCI(expr, "ADD ") || startsCI(expr, "SUBTRACT ")) {
        emitV1Raw(t);
        return true;
      }
      line("derived " + name + " = " + rewriteExpr(expr));
      return true;
    }
    if (eqCI(t, "BEGIN REACTIVE BATCH") || startsCI(t, "BEGIN REACTIVE BATCH")) {
      line("batch {");
      ++indent;
      return true;
    }
    if (eqCI(t, "END REACTIVE BATCH") || startsCI(t, "END REACTIVE BATCH")) {
      --indent;
      line("}");
      return true;
    }
    if (startsCI(t, "INCREASE ")) {
      auto rest = trim(t.substr(9));
      auto by = findTopCI(rest, " BY ");
      if (by != std::string::npos) {
        line(trim(rest.substr(0, by)) + " += " + rewriteExpr(rest.substr(by + 4)));
        return true;
      }
    }
    if (startsCI(t, "DECREASE ")) {
      auto rest = trim(t.substr(9));
      auto by = findTopCI(rest, " BY ");
      if (by != std::string::npos) {
        line(trim(rest.substr(0, by)) + " -= " + rewriteExpr(rest.substr(by + 4)));
        return true;
      }
    }
    if (startsCI(t, "PUT ")) {
      auto rest = trim(t.substr(4));
      auto u = upper(rest);
      auto to = u.find(" TO ");
      auto in = u.find(" IN ");
      if (to != std::string::npos && in != std::string::npos && in > to) {
        auto k = trim(rest.substr(0, to));
        auto v = trim(rest.substr(to + 4, in - (to + 4)));
        auto m = trim(rest.substr(in + 4));
        line(rewriteExpr(m) + "[" + rewriteExpr(k) + "] = " + rewriteExpr(v));
        return true;
      }
    }
    if (startsCI(t, "ATTEMPT")) {
      line("try {");
      ++indent;
      return true;
    }
    if (startsCI(t, "OTHERWISE WITH ")) {
      auto rest = trim(t.substr(15));
      auto u = upper(rest);
      if (u.size() >= 3 && u.substr(u.size() - 3) == " DO")
        rest = trim(rest.substr(0, rest.size() - 3));
      --indent;
      line("} catch (" + trim(rest) + ") {");
      ++indent;
      return true;
    }
    if (startsCI(t, "OTHERWISE")) {
      --indent;
      line("} catch {");
      ++indent;
      return true;
    }
    if (eqCI(t, "END ATTEMPT") || startsCI(t, "END ATTEMPT")) {
      --indent;
      line("}");
      return true;
    }
    if (startsCI(t, "GIVE UP WITH ")) {
      line("throw " + rewriteExpr(t.substr(13)));
      return true;
    }
    if (startsCI(t, "GIVE UP")) {
      line("throw");
      return true;
    }
    if (startsCI(t, "TEST ")) {
      /* TEST "name" DO */
      auto rest = trim(t.substr(5));
      auto u = upper(rest);
      if (u.size() >= 3 && u.substr(u.size() - 3) == " DO")
        rest = trim(rest.substr(0, rest.size() - 3));
      line("test " + rest + " {");
      ++indent;
      return true;
    }
    if (eqCI(t, "END TEST") || startsCI(t, "END TEST")) {
      --indent;
      line("}");
      return true;
    }
    if (startsCI(t, "REQUIRE LOGIN ON ")) {
      /* REQUIRE LOGIN ON req WITH db */
      auto rest = trim(t.substr(17));
      auto w = findTopCI(rest, " WITH ");
      if (w != std::string::npos) {
        line("require login on " + rewriteExpr(rest.substr(0, w)) + " with " +
             rewriteExpr(rest.substr(w + 6)));
        return true;
      }
      line("require login on " + rewriteExpr(rest));
      return true;
    }
    if (startsCI(t, "CALL PARENT ")) {
      emitCallParent(t);
      return true;
    }
    if (startsCI(t, "FILL ")) {
      /* FILL "id" WITH body */
      auto rest = trim(t.substr(5));
      auto u = upper(rest);
      auto with = u.find(" WITH ");
      if (with != std::string::npos) {
        auto id = trim(rest.substr(0, with));
        auto body = trim(rest.substr(with + 6));
        line("fill(" + id + ", " + body + ")");
        return true;
      }
    }
    if (startsCI(t, "REFRESH QUERY ")) {
      line(rewriteExpr(trim(t.substr(14))) + ".refresh()");
      return true;
    }
    if (startsCI(t, "FOR EACH ")) {
      auto rest = trim(t.substr(9));
      auto u = upper(rest);
      auto in = u.find(" IN ");
      if (in != std::string::npos) {
        auto var = trim(rest.substr(0, in));
        auto xs = trim(rest.substr(in + 4));
        if (upper(xs).size() >= 3 && upper(xs).substr(upper(xs).size() - 3) == " DO")
          xs = trim(xs.substr(0, xs.size() - 3));
        line("for " + var + " in " + rewriteExpr(xs) + " {");
        ++indent;
        return true;
      }
    }
    if (eqCI(t, "END FOR") || startsCI(t, "END FOR EACH") || eqCI(t, "END FOR EACH")) {
      --indent;
      line("}");
      return true;
    }
    if (eqCI(t, "PAINT THE SCREEN") || startsCI(t, "PAINT THE SCREEN")) {
      line("paint()");
      return true;
    }
    if (eqCI(t, "LAY OUT THE SCREEN") || startsCI(t, "LAY OUT THE SCREEN")) {
      line("layout()");
      return true;
    }
    if (startsCI(t, "FOREIGN FUNCTION ") || startsCI(t, "FOREIGN ")) {
      emitV1Raw(t);
      return true;
    }
    if (startsCI(t, "BRING FONT ")) {
      auto rest = trim(t.substr(11));
      auto u = upper(rest);
      auto from = u.find(" FROM ");
      if (from != std::string::npos) {
        line("page.font(" + trim(rest.substr(0, from)) + ", " + trim(rest.substr(from + 6)) +
             ")");
        return true;
      }
    }
    /* Blueprint leftovers that appear at top level somehow */
    if (startsCI(t, "HAS ") || startsCI(t, "METHOD ") || startsCI(t, "WHEN BORN") ||
        startsCI(t, "END METHOD") || startsCI(t, "END BORN") ||
        startsCI(t, "END CLASS") || startsCI(t, "END BLUEPRINT") ||
        startsCI(t, "END FUNCTION") || eqCI(t, "LET'S START") || eqCI(t, "DONE") ||
        startsCI(t, "LET'S START") || eqCI(t, "DONE")) {
      if (startsCI(t, "END ") || eqCI(t, "DONE") || startsCI(t, "LET'S START"))
        return true; /* drop bookends / closers already handled */
    }
    return false;
  }

  void emitStmt(const Stmt &s) {
    switch (s.kind) {
      case StmtKind::Empty:
        return;

      case StmtKind::Import:
        line("import " + s.aux);
        return;

      case StmtKind::Speak: {
        std::string e = emitExpr(s.expr);
        /* Unmapped THE … phrases (and any multi-word remnant) are invalid v2. */
        if (e.find("THE ") != std::string::npos || e.find(" THE") != std::string::npos) {
          emitV1Raw(s.text.empty() ? ("SPEAK " + e) : s.text);
          return;
        }
        line("print(" + e + ")");
        return;
      }

      case StmtKind::Let: {
        /* Redeclaration → assignment (collections_test catch rebind). */
        bool already = declared.count(s.name) > 0;
        if (!already) declared.insert(s.name);

        if (already && s.expr.kind != AstKind::Empty) {
          std::string rhs = emitExpr(s.expr);
          if (rhs.find("THE ") != std::string::npos) {
            emitV1Raw(s.text);
            return;
          }
          line(s.name + " = " + rhs);
          return;
        }

        bool noInit = (s.expr.kind == AstKind::Empty);
        std::string ty = s.typeName.empty() ? "" : v2Type(s.typeName);
        bool collection = (upper(s.typeName) == "LIST" || upper(s.typeName) == "MAP");
        bool useVar = isVar(s.name) || noInit || collection;

        std::ostringstream o;
        o << (useVar ? "var " : "let ") << s.name;
        if (!ty.empty()) o << ": " << ty;
        if (noInit) {
          if (upper(s.typeName) == "LIST") o << " = []";
          else if (upper(s.typeName) == "MAP") o << " = {}";
        } else {
          std::string rhs = emitExpr(s.expr);
          if (rhs.find("THE ") != std::string::npos) {
            emitV1Raw(s.text);
            return;
          }
          o << " = " << rhs;
        }
        line(o.str());
        return;
      }

      case StmtKind::Remember: {
        std::ostringstream o;
        if (s.flag) o << "secret ";
        o << "signal " << s.name;
        if (!s.typeName.empty() && startsCI(s.typeName, "QUERY ON ")) {
          /* Keep query cells as opaque v1 — lowerer has no query-on form yet. */
          emitV1Raw(s.text);
          return;
        }
        if (!s.typeName.empty() && isV1TypeWord(s.typeName)) {
          o << ": " << v2Type(s.typeName);
          if (s.expr.kind != AstKind::Empty) o << " = " << emitExpr(s.expr);
          else if (upper(s.typeName) == "LIST") o << " = []";
          else if (upper(s.typeName) == "MAP") o << " = {}";
          /* TEXT/NUMBER/FLAG typed-empty: annotation only */
        } else if (!s.typeName.empty() && looksLikeValue(s.typeName) &&
                   s.expr.kind == AstKind::Empty) {
          o << " = " << rewriteExpr(s.typeName);
        } else if (s.expr.kind != AstKind::Empty) {
          o << " = " << emitExpr(s.expr);
        } else if (!s.typeName.empty()) {
          o << " = " << rewriteExpr(s.typeName);
        }
        line(o.str());
        return;
      }

      case StmtKind::Change: {
        std::string rhs = emitExpr(s.expr);
        if (rhs.find("THE ") != std::string::npos) {
          emitV1Raw(s.text);
          return;
        }
        line(s.name + " = " + rhs);
        return;
      }

      case StmtKind::Set: {
        /* SET ITEM n OF xs TO v */
        if (startsCI(s.name, "ITEM ") || startsCI(s.text, "SET ITEM ")) {
          auto t = s.text;
          if (startsCI(t, "SET ")) t = trim(t.substr(4));
          auto u = upper(t);
          auto of = u.find(" OF ");
          auto to = u.find(" TO ");
          if (startsCI(t, "ITEM ") && of != std::string::npos && to != std::string::npos &&
              to > of) {
            auto idx = trim(t.substr(5, of - 5));
            auto xs = trim(t.substr(of + 4, to - (of + 4)));
            line(rewriteExpr(xs) + "[" + rewriteExpr(idx) + "] = " + emitExpr(s.expr));
            return;
          }
        }
        std::string lhs = rewriteIdentSpelling(s.name);
        auto b = baseAssignName(s.name);
        /* Top-level SET of a fresh name is a declaration in v1 (e.g. SET buddy TO NEW …). */
        if (!b.empty() && s.name.find('.') == std::string::npos && !declared.count(b)) {
          declared.insert(b);
          bool useVar = isVar(b);
          line(std::string(useVar ? "var " : "let ") + lhs + " = " + emitExpr(s.expr));
        } else {
          line(lhs + " = " + emitExpr(s.expr));
        }
        return;
      }

      case StmtKind::Ask: {
        /* Prefer full original text (includes ASK). */
        std::string e;
        if (!s.text.empty() && startsCI(s.text, "ASK "))
          e = rewriteExpr(s.text);
        else
          e = emitExpr(s.expr);
        line(e);
        return;
      }

      case StmtKind::GiveBack:
        if (s.expr.kind == AstKind::Empty) line("return");
        else line("return " + emitExpr(s.expr));
        return;

      case StmtKind::If: {
        std::string cond;
        if (s.expr.kind != AstKind::Empty) cond = emitExpr(s.expr);
        else {
          auto r = trim(s.text.size() > 3 ? s.text.substr(3) : "");
          auto u = upper(r);
          if (u.size() >= 3 && u.substr(u.size() - 3) == " DO")
            r = trim(r.substr(0, r.size() - 3));
          cond = rewriteExpr(r);
        }
        line("if " + cond + " {");
        emitBlock(s.body);
        if (!s.elseBody.empty()) {
          line("} else {");
          emitBlock(s.elseBody);
        }
        line("}");
        return;
      }

      case StmtKind::While: {
        std::string cond;
        if (s.expr.kind != AstKind::Empty) cond = emitExpr(s.expr);
        else {
          auto r = trim(s.text.size() > 6 ? s.text.substr(6) : "");
          auto u = upper(r);
          if (u.size() >= 3 && u.substr(u.size() - 3) == " DO")
            r = trim(r.substr(0, r.size() - 3));
          cond = rewriteExpr(r);
        }
        line("while " + cond + " {");
        emitBlock(s.body);
        line("}");
        return;
      }

      case StmtKind::MakeSure:
        line("assert " + emitExpr(s.expr));
        return;

      case StmtKind::AddTo:
        line(s.name + ".push(" + emitExpr(s.expr) + ")");
        return;

      case StmtKind::PutIn: {
        /* structured rare; text fallback */
        if (!emitRaw(s.text)) todo(s.text);
        return;
      }

      case StmtKind::Function: {
        auto ps = parseParams(s.aux);
        std::ostringstream o;
        o << "fn " << s.name << emitParams(ps);
        if (!s.typeName.empty()) o << " -> " << v2Type(s.typeName);
        o << " {";
        line(o.str());
        /* nested declarations */
        auto saved = declared;
        emitBlock(s.body);
        declared = saved;
        line("}");
        return;
      }

      case StmtKind::Blueprint: {
        /* name may be "Dog FOLLOWS Animal" */
        std::string nm = s.name;
        std::string parent;
        auto u = upper(nm);
        auto fol = u.find(" FOLLOWS ");
        auto ext = u.find(" EXTENDS ");
        if (fol != std::string::npos) {
          parent = trim(nm.substr(fol + 9));
          nm = trim(nm.substr(0, fol));
        } else if (ext != std::string::npos) {
          parent = trim(nm.substr(ext + 9));
          nm = trim(nm.substr(0, ext));
        }
        if (parent.empty())
          line("struct " + nm + " {");
        else
          line("struct " + nm + " : " + parent + " {");
        auto saved = declared;
        emitBlueprintBody(s.body);
        declared = saved;
        line("}");
        return;
      }

      case StmtKind::WhenReactive: {
        std::string kw = "effect";
        auto U = upper(s.text);
        if (U.find("BACKGROUND") != std::string::npos) kw = "effect background";
        else if (U.find("WEAK") != std::string::npos) kw = "effect weak";
        line(kw + " on " + s.name + " {");
        emitBlock(s.body);
        line("}");
        return;
      }

      case StmtKind::When: {
        /* Flatten WHEN…END WHEN to opaque v1 (click/viewport/route not in v2 surface yet). */
        std::ostringstream v1;
        v1 << trim(s.text) << "\n";
        std::vector<const Stmt *> stack;
        for (auto &c : s.body) {
          if (!c.text.empty())
            v1 << "  " << c.text << "\n";
          for (auto &n : c.body)
            if (!n.text.empty()) v1 << "    " << n.text << "\n";
        }
        v1 << "END WHEN";
        emitV1Raw(v1.str());
        return;
      }

      case StmtKind::Watch:
        emitWatchLine(s.text);
        return;

      case StmtKind::PushWatch:
        emitPushWatchLine(s.text);
        return;

      case StmtKind::Bind: {
        auto t = trim(s.text);
        if (startsCI(t, "BIND LIST ")) {
          auto rest = trim(t.substr(10));
          auto u = upper(rest);
          auto as = u.find(" AS ");
          if (as != std::string::npos) {
            line("bind.list(" + rewriteExpr(trim(rest.substr(0, as))) + ", " +
                 trim(rest.substr(as + 4)) + ")");
            return;
          }
        }
        if (startsCI(t, "BIND ")) {
          auto rest = trim(t.substr(5));
          auto u = upper(rest);
          auto to = u.find(" TO ");
          if (to != std::string::npos) {
            line("bind(" + trim(rest.substr(0, to)) + ", " +
                 rewriteExpr(trim(rest.substr(to + 4))) + ")");
            return;
          }
        }
        emitV1Raw(t);
        return;
      }

      case StmtKind::WearStyle: {
        std::string css = s.aux;
        if (css.empty() && startsCI(s.text, "WEAR STYLE ")) {
          auto rest = trim(s.text.substr(11));
          if (rest.size() >= 6 && rest.rfind("\"\"\"", 0) == 0)
            css = rest.substr(3, rest.size() - 6);
          else
            css = rest;
        }
        line(std::string("page.style(\"\"\"") + css + "\"\"\")");
        return;
      }

      case StmtKind::NamePage: {
        if (s.expr.kind != AstKind::Empty)
          line("page.title(" + emitExpr(s.expr) + ")");
        else
          emitV1Raw(s.text);
        return;
      }

      case StmtKind::BeginColumn:
      case StmtKind::EndColumn:
      case StmtKind::LayOut:
      case StmtKind::Paint:
        emitV1Raw(s.text.empty() ? luke::stmtKindName(s.kind) : s.text);
        return;

      case StmtKind::Block:
        emitBlock(s.body);
        return;

      case StmtKind::Raw:
        if (!emitRaw(s.text)) emitV1Raw(s.text);
        return;
    }
  }

  void run(const luke::Program &p) {
    collectAll(p.stmts);
    declared.clear(); /* emit re-tracks */
    for (auto &s : p.stmts) emitStmt(s);
  }
};

}  // namespace

MigrateResult migrateSource(const std::string &v1Source) {
  MigrateResult r;
  luke::Program prog = luke::parseLuke(v1Source);
  if (!prog.ok()) {
    r.error = prog.error;
    r.line = prog.errorLine;
    return r;
  }
  Mig m;
  m.run(prog);
  r.ok = true;
  r.v2 = m.out.str();
  r.todos = m.todos;
  return r;
}

}  // namespace luke2
