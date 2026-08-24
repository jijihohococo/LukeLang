/* Syntax v2 parser — recursive-descent statements + Pratt expressions. */

#include "luke2.hpp"

namespace luke2 {
namespace {

struct P {
  const std::vector<Tok> &t;
  size_t i = 0;
  std::string err;
  size_t errLine = 0;

  explicit P(const std::vector<Tok> &toks) : t(toks) {}

  const Tok &cur() const { return t[i < t.size() ? i : t.size() - 1]; }
  const Tok &peek(size_t n = 1) const {
    size_t j = i + n;
    return t[j < t.size() ? j : t.size() - 1];
  }
  bool done() const { return cur().kind == Tk::End || !err.empty(); }

  void fail(const std::string &m) {
    if (err.empty()) {
      err = m;
      errLine = cur().line;
    }
  }

  void skipNewlines() {
    while (cur().kind == Tk::Newline) ++i;
  }
  /* Statement separators: newline or ';'. */
  void skipSeps() {
    while (cur().kind == Tk::Newline || (cur().kind == Tk::Punct && cur().text == ";")) ++i;
  }

  bool isPunct(const char *p) const { return cur().kind == Tk::Punct && cur().text == p; }
  bool isKw(const char *k) const { return cur().kind == Tk::Ident && cur().text == k; }

  bool eatPunct(const char *p) {
    if (isPunct(p)) {
      ++i;
      return true;
    }
    return false;
  }
  bool eatKw(const char *k) {
    if (isKw(k)) {
      ++i;
      return true;
    }
    return false;
  }
  void expectPunct(const char *p) {
    if (!eatPunct(p)) fail(std::string("expected '") + p + "' but found '" + cur().text + "'");
  }
  std::string expectIdent() {
    if (cur().kind != Tk::Ident) {
      fail("expected an identifier but found '" + cur().text + "'");
      return {};
    }
    return t[i++].text;
  }

  /* ---------- types ---------- */

  /* `: T` — returns empty when absent. */
  std::string optType() {
    if (!eatPunct(":")) return {};
    return expectIdent();
  }

  /* ---------- expressions (Pratt) ---------- */

  static int prec(const std::string &op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/" || op == "%") return 5;
    return 0;
  }

  Ex parseExpr(int minPrec = 0) {
    Ex lhs = parseUnary();
    for (;;) {
      if (cur().kind != Tk::Punct) break;
      int p = prec(cur().text);
      if (p == 0 || p < minPrec) break;
      std::string op = cur().text;
      size_t line = cur().line;
      ++i;
      skipNewlines(); /* allow a line break after a binary operator */
      Ex rhs = parseExpr(p + 1);
      Ex n;
      n.k = Ek::Binary;
      n.s = op;
      n.line = line;
      n.kids.push_back(lhs);
      n.kids.push_back(rhs);
      lhs = n;
      if (!err.empty()) break;
    }
    return lhs;
  }

  Ex parseUnary() {
    if (isPunct("!") || isPunct("-")) {
      Ex n;
      n.k = Ek::Unary;
      n.s = cur().text;
      n.line = cur().line;
      ++i;
      n.kids.push_back(parseUnary());
      return n;
    }
    return parsePostfix();
  }

  Ex parsePostfix() {
    Ex e = parsePrimary();
    for (;;) {
      if (isPunct(".")) {
        size_t line = cur().line;
        ++i;
        std::string name = expectIdent();
        if (!err.empty()) return e;
        if (isPunct("(")) {
          ++i;
          Ex call;
          call.k = Ek::Method;
          call.s = name;
          call.line = line;
          call.kids.push_back(e);
          parseArgs(call.kids);
          e = call;
        } else {
          Ex f;
          f.k = Ek::Field;
          f.s = name;
          f.line = line;
          f.kids.push_back(e);
          e = f;
        }
        continue;
      }
      if (isPunct("[")) {
        size_t line = cur().line;
        ++i;
        Ex idx;
        idx.k = Ek::Index;
        idx.line = line;
        idx.kids.push_back(e);
        idx.kids.push_back(parseExpr());
        expectPunct("]");
        e = idx;
        continue;
      }
      if (isPunct("(") && (e.k == Ek::Ident)) {
        size_t line = cur().line;
        ++i;
        Ex call;
        call.k = Ek::Call;
        call.s = e.s;
        call.line = line;
        parseArgs(call.kids);
        e = call;
        continue;
      }
      break;
    }
    return e;
  }

  /* Consumes args up to and including ')'. */
  void parseArgs(std::vector<Ex> &into) {
    skipNewlines();
    if (eatPunct(")")) return;
    for (;;) {
      into.push_back(parseExpr());
      if (!err.empty()) return;
      skipNewlines();
      if (eatPunct(",")) {
        skipNewlines();
        continue;
      }
      expectPunct(")");
      return;
    }
  }

  Ex parsePrimary() {
    Ex n;
    n.line = cur().line;
    const Tok &tk = cur();

    if (tk.kind == Tk::Int) {
      n.k = Ek::Int;
      n.s = tk.text;
      ++i;
      return n;
    }
    if (tk.kind == Tk::Float) {
      n.k = Ek::Float;
      n.s = tk.text;
      ++i;
      return n;
    }
    if (tk.kind == Tk::Str) {
      n.k = Ek::Str;
      n.s = tk.raw;
      ++i;
      return n;
    }
    if (tk.kind == Tk::Ident) {
      if (tk.text == "true" || tk.text == "false") {
        n.k = Ek::Bool;
        n.s = tk.text;
        ++i;
        return n;
      }
      n.k = Ek::Ident;
      n.s = tk.text;
      ++i;
      return n;
    }
    if (isPunct("(")) {
      ++i;
      Ex inner = parseExpr();
      expectPunct(")");
      return inner;
    }
    if (isPunct("[")) {
      ++i;
      n.k = Ek::ListLit;
      skipNewlines();
      if (!eatPunct("]")) {
        for (;;) {
          n.kids.push_back(parseExpr());
          if (!err.empty()) return n;
          skipNewlines();
          if (eatPunct(",")) {
            skipNewlines();
            continue;
          }
          expectPunct("]");
          break;
        }
      }
      return n;
    }
    if (isPunct("{")) {
      ++i;
      n.k = Ek::MapLit;
      skipNewlines();
      if (!eatPunct("}")) {
        for (;;) {
          n.kids.push_back(parseExpr()); /* key */
          expectPunct(":");
          n.kids.push_back(parseExpr()); /* value */
          if (!err.empty()) return n;
          skipNewlines();
          if (eatPunct(",")) {
            skipNewlines();
            continue;
          }
          expectPunct("}");
          break;
        }
      }
      return n;
    }

    fail("unexpected '" + tk.text + "' in expression");
    return n;
  }

  /* ---------- blocks ---------- */

  std::vector<St> parseBlock() {
    std::vector<St> out;
    expectPunct("{");
    for (;;) {
      skipSeps();
      if (err.size()) return out;
      if (eatPunct("}")) return out;
      if (cur().kind == Tk::End) {
        fail("unexpected end of file — missing '}'");
        return out;
      }
      St s = parseStmt();
      if (!err.empty()) return out;
      out.push_back(s);
    }
  }

  /* ---------- statements ---------- */

  St parseStmt() {
    St s;
    s.line = cur().line;

    if (isKw("let") || isKw("var")) {
      s.k = Sk::Let;
      s.isVar = cur().text == "var";
      ++i;
      s.name = expectIdent();
      s.type = optType();
      if (eatPunct("=")) s.e = parseExpr();
      return s;
    }
    if (isKw("secret") && (peek().kind == Tk::Ident && peek().text == "signal")) {
      ++i;
      St r = parseStmt();
      r.secret = true;
      return r;
    }
    if (isKw("signal")) {
      s.k = Sk::Signal;
      ++i;
      s.name = expectIdent();
      s.type = optType();
      if (eatPunct("=")) s.e = parseExpr();
      return s;
    }
    if (isKw("derived")) {
      s.k = Sk::Derived;
      ++i;
      s.name = expectIdent();
      expectPunct("=");
      s.e = parseExpr();
      return s;
    }
    if (isKw("effect")) {
      s.k = Sk::Effect;
      ++i;
      /* `effect on <cell> { … }`; optional mode: background / weak */
      if (isKw("background") || isKw("weak")) {
        s.aux2 = cur().text;
        ++i;
      }
      if (eatKw("on")) s.name = expectIdent();
      s.body = parseBlock();
      return s;
    }
    if (isKw("batch")) {
      s.k = Sk::Batch;
      ++i;
      s.body = parseBlock();
      return s;
    }
    if (isKw("watch")) {
      s.k = Sk::Watch;
      ++i;
      s.name = expectIdent();
      if (eatKw("from")) {
        if (cur().kind == Tk::Str) {
          s.aux = cur().raw;
          ++i;
        } else {
          s.aux = expectIdent();
        }
      }
      if (eatKw("as")) {
        if (cur().kind != Tk::Str) {
          fail("`as` needs a string SQL");
          return s;
        }
        s.aux2 = std::string("AS ") + cur().raw;
        ++i;
      } else if (eatKw("where")) {
        if (cur().kind == Tk::Str) {
          s.aux2 = std::string("WHERE ") + cur().raw;
          ++i;
        } else {
          fail("`where` needs a string predicate");
        }
      }
      if (isKw("for") && peek().kind == Tk::Ident && peek().text == "current_user") {
        i += 2;
        if (s.aux2.empty()) s.aux2 = "FOR CURRENT USER";
        else s.aux2 += " FOR CURRENT USER";
      }
      return s;
    }
    if (isKw("push") && peek().kind == Tk::Ident && peek().text == "watch") {
      s.k = Sk::PushWatch;
      i += 2;
      s.name = expectIdent();
      if (eatKw("on")) s.aux = expectIdent();
      /* optional tail: for N beats every M ms */
      std::string tail;
      while (cur().kind == Tk::Ident || cur().kind == Tk::Int) {
        if (cur().kind == Tk::Ident && cur().text == "ms") {
          tail += " MILLISECONDS";
          ++i;
          continue;
        }
        std::string w = cur().text;
        for (auto &c : w) c = (char)toupper((unsigned char)c);
        tail += " " + w;
        ++i;
      }
      s.aux2 = tail;
      return s;
    }
    if (isKw("fn")) {
      s.k = Sk::Fn;
      ++i;
      s.name = expectIdent();
      expectPunct("(");
      parseParams(s.params);
      if (eatPunct("->")) s.type = expectIdent();
      s.body = parseBlock();
      return s;
    }
    if (isKw("return")) {
      s.k = Sk::Return;
      ++i;
      if (cur().kind != Tk::Newline && !isPunct("}") && !isPunct(";")) s.e = parseExpr();
      return s;
    }
    if (isKw("if")) return parseIf();
    if (isKw("while")) {
      s.k = Sk::While;
      ++i;
      s.e = parseExpr();
      s.body = parseBlock();
      return s;
    }
    if (isKw("for")) {
      s.k = Sk::For;
      ++i;
      s.name = expectIdent();
      if (!eatKw("in")) fail("expected `in` in for loop");
      s.e = parseExpr();
      if (eatPunct("..")) s.e2 = parseExpr();
      s.body = parseBlock();
      return s;
    }
    if (isKw("struct")) {
      s.k = Sk::Struct;
      ++i;
      s.name = expectIdent();
      if (eatPunct(":")) s.aux = expectIdent();
      s.body = parseStructBody();
      return s;
    }
    if (isKw("try")) {
      s.k = Sk::Try;
      ++i;
      s.body = parseBlock();
      skipNewlines();
      if (!eatKw("catch")) {
        fail("expected `catch` after try block");
        return s;
      }
      expectPunct("(");
      s.aux = expectIdent();
      expectPunct(")");
      s.body2 = parseBlock();
      return s;
    }
    if (isKw("throw")) {
      s.k = Sk::Throw;
      ++i;
      s.e = parseExpr();
      return s;
    }
    if (isKw("test")) {
      s.k = Sk::Test;
      ++i;
      if (cur().kind != Tk::Str) {
        fail("test needs a name string");
        return s;
      }
      s.aux = cur().raw;
      ++i;
      s.body = parseBlock();
      return s;
    }
    if (isKw("assert")) {
      s.k = Sk::Assert;
      ++i;
      s.e = parseExpr();
      return s;
    }
    if (isKw("arena")) {
      s.k = Sk::Arena;
      ++i;
      s.body = parseBlock();
      return s;
    }
    if (isKw("break")) {
      s.k = Sk::Break;
      ++i;
      return s;
    }
    if (isKw("import")) {
      s.k = Sk::Import;
      ++i;
      /* `import std/server`, `import "./x.lk"`, or `import c:m` (FFI). */
      if (cur().kind == Tk::Str) {
        s.aux = cur().raw;
        ++i;
      } else {
        std::string spec = expectIdent();
        while (isPunct("/") || isPunct(":")) {
          spec += cur().text;
          ++i;
          spec += expectIdent();
        }
        s.aux = spec;
      }
      return s;
    }
    if (isKw("require") && peek().kind == Tk::Ident && peek().text == "login") {
      s.k = Sk::ExprStmt;
      i += 2;
      std::string tail = "REQUIRE LOGIN";
      if (eatKw("on")) tail += " ON " + expectIdent();
      if (eatKw("with")) tail += " WITH " + expectIdent();
      s.aux = tail;
      s.name = "__raw__";
      return s;
    }
    /* Phase 3a: opaque v1 statement passthrough for forms not yet in the v2 surface. */
    if (isKw("raw")) {
      s.k = Sk::ExprStmt;
      s.name = "__raw__";
      ++i;
      if (cur().kind != Tk::Str) {
        fail("`raw` needs a string (prefer \"\"\"…\"\"\" for multi-line)");
        return s;
      }
      std::string r = cur().raw;
      ++i;
      if (r.size() >= 6 && r.rfind("\"\"\"", 0) == 0 &&
          r.compare(r.size() - 3, 3, "\"\"\"") == 0) {
        s.aux = r.substr(3, r.size() - 6);
      } else if (r.size() >= 2 && r.front() == '"' && r.back() == '"') {
        std::string body = r.substr(1, r.size() - 2);
        std::string decoded;
        for (size_t k = 0; k < body.size(); ++k) {
          if (body[k] == '\\' && k + 1 < body.size()) {
            decoded.push_back(body[k + 1]);
            ++k;
            continue;
          }
          decoded.push_back(body[k]);
        }
        s.aux = decoded;
      } else {
        s.aux = r;
      }
      return s;
    }

    /* assignment or bare expression */
    Ex lhs = parseExpr();
    if (!err.empty()) return s;
    if (isPunct("=")) {
      ++i;
      s.k = Sk::Assign;
      s.e = lhs;
      s.e2 = parseExpr();
      return s;
    }
    if (isPunct("+=") || isPunct("-=") || isPunct("*=") || isPunct("/=")) {
      s.k = Sk::OpAssign;
      s.aux = cur().text;
      ++i;
      s.e = lhs;
      s.e2 = parseExpr();
      return s;
    }
    s.k = Sk::ExprStmt;
    s.e = lhs;
    return s;
  }

  St parseIf() {
    St s;
    s.k = Sk::If;
    s.line = cur().line;
    ++i; /* if */
    s.e = parseExpr();
    s.body = parseBlock();
    skipNewlines();
    if (isKw("else")) {
      ++i;
      if (isKw("if")) {
        s.body2.push_back(parseIf());
      } else {
        s.body2 = parseBlock();
      }
    }
    return s;
  }

  void parseParams(std::vector<std::pair<std::string, std::string>> &into) {
    skipNewlines();
    if (eatPunct(")")) return;
    for (;;) {
      std::string nm = expectIdent();
      std::string ty = optType();
      into.emplace_back(nm, ty);
      if (!err.empty()) return;
      skipNewlines();
      if (eatPunct(",")) {
        skipNewlines();
        continue;
      }
      expectPunct(")");
      return;
    }
  }

  std::vector<St> parseStructBody() {
    std::vector<St> out;
    expectPunct("{");
    for (;;) {
      skipSeps();
      if (!err.empty()) return out;
      if (eatPunct("}")) return out;
      if (cur().kind == Tk::End) {
        fail("unexpected end of file — missing '}' in struct");
        return out;
      }

      St m;
      m.line = cur().line;
      bool priv = false;
      if (isKw("private")) {
        priv = true;
        ++i;
      }

      if (isKw("init")) {
        m.k = Sk::Init;
        ++i;
        expectPunct("(");
        parseParams(m.params);
        m.body = parseBlock();
      } else if (isKw("fn")) {
        m.k = Sk::Method;
        ++i;
        m.name = expectIdent();
        expectPunct("(");
        parseParams(m.params);
        if (eatPunct("->")) m.type = expectIdent();
        m.body = parseBlock();
      } else {
        /* field: `name: T` or `name = expr` */
        m.k = Sk::FieldDecl;
        m.name = expectIdent();
        if (eatPunct(":")) {
          m.type = expectIdent();
          if (eatPunct("=")) m.e = parseExpr();
        } else if (eatPunct("=")) {
          m.e = parseExpr();
        } else {
          fail("struct field needs a type or an initialiser");
        }
      }
      m.isPrivate = priv;
      if (!err.empty()) return out;
      out.push_back(m);
    }
  }
};

}  // namespace

Program parse(const std::vector<Tok> &toks) {
  Program prog;
  P p(toks);
  for (;;) {
    p.skipSeps();
    if (!p.err.empty()) break;
    if (p.cur().kind == Tk::End) break;
    St s = p.parseStmt();
    if (!p.err.empty()) break;
    prog.stmts.push_back(s);
  }
  if (!p.err.empty()) {
    prog.error = p.err;
    prog.errorLine = p.errLine;
  }
  return prog;
}

}  // namespace luke2
