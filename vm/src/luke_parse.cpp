#include "luke_parse.hpp"
#include "luke_expr.hpp"

#include <cctype>
#include <sstream>

namespace luke {
namespace {

std::string trimCopy(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
  size_t e = s.size();
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string toUpperCopy(std::string s) {
  for (char &c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

bool startsCI(const std::string &s, const char *p) {
  size_t n = std::char_traits<char>::length(p);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i)
    if ((char)std::toupper((unsigned char)s[i]) != (char)std::toupper((unsigned char)p[i]))
      return false;
  return true;
}

void stripDo(std::string &s) {
  auto t = trimCopy(s);
  auto U = toUpperCopy(t);
  if (U.size() >= 3 && U.compare(U.size() - 3, 3, " DO") == 0)
    s = trimCopy(t.substr(0, t.size() - 3));
  else
    s = t;
}

struct Line {
  std::string text;
  size_t lineNo = 1;
};

/* Split source into logical lines; keep """ … """ on one logical unit via raw lines
 * but WEAR STYLE multiline is handled by consuming subsequent lines in parse. */
std::vector<Line> splitLogicalLines(const std::string &source) {
  std::vector<Line> out;
  std::string cur;
  size_t lineNo = 1;
  size_t startLine = 1;
  bool inTriple = false;
  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    if (!inTriple && i + 2 < source.size() && c == '"' && source[i + 1] == '"' &&
        source[i + 2] == '"') {
      inTriple = true;
      cur += "\"\"\"";
      i += 2;
      continue;
    }
    if (inTriple && i + 2 < source.size() && c == '"' && source[i + 1] == '"' &&
        source[i + 2] == '"') {
      inTriple = false;
      cur += "\"\"\"";
      i += 2;
      continue;
    }
    if (c == '\n' && !inTriple) {
      out.push_back({cur, startLine});
      cur.clear();
      ++lineNo;
      startLine = lineNo;
      continue;
    }
    if (c == '\n') {
      cur.push_back(c);
      ++lineNo;
      continue;
    }
    if (c != '\r') cur.push_back(c);
  }
  out.push_back({cur, startLine});
  return out;
}

Ast parseExprOrEmpty(const std::string &src, size_t line) {
  auto t = trimCopy(src);
  if (t.empty()) {
    Ast z;
    z.kind = AstKind::Empty;
    z.line = line;
    return z;
  }
  ExprLower quiet;
  quiet.fail = nullptr;
  quiet.resolve = [](const std::string &atom, size_t) {
    return std::make_pair(atom, std::string("num"));
  };
  auto toks = tokenizeExpr(t);
  size_t consumed = 0;
  Ast a = parseExprAst(toks, line, quiet, &consumed);
  a.line = line;
  /* Incomplete Pratt (legacy phrase) — keep full text as Ident for tooling. */
  if (consumed >= toks.size() || toks[consumed].kind != TokKind::End) {
    Ast id;
    id.kind = AstKind::Ident;
    id.text = t;
    id.line = line;
    return id;
  }
  return a;
}

struct Parser {
  std::vector<Line> lines;
  size_t i = 0;
  Program *prog = nullptr;

  bool done() const { return i >= lines.size(); }
  const Line &cur() const { return lines[i < lines.size() ? i : lines.size() - 1]; }
  void advance() {
    if (i < lines.size()) ++i;
  }

  std::string peekTrimmed() const {
    if (done()) return {};
    return trimCopy(lines[i].text);
  }

  bool isEnder(const std::string &t, const char *kw) const {
    auto U = toUpperCopy(t);
    return U == kw || startsCI(t, (std::string(kw) + " ").c_str());
  }

  std::vector<Stmt> parseBlockUntil(const char *endA, const char *endB = nullptr,
                                    const char *endC = nullptr) {
    std::vector<Stmt> body;
    while (!done()) {
      auto t = peekTrimmed();
      if (t.empty() || startsCI(t, "//")) {
        advance();
        continue;
      }
      if (endA && isEnder(t, endA)) break;
      if (endB && isEnder(t, endB)) break;
      if (endC && isEnder(t, endC)) break;
      body.push_back(parseStmt());
    }
    return body;
  }

  Stmt parseStmt() {
    Stmt s;
    if (done()) return s;
    auto raw = lines[i].text;
    auto t = trimCopy(raw);
    s.line = lines[i].lineNo;
    s.text = t;
    if (t.empty() || startsCI(t, "//")) {
      s.kind = StmtKind::Empty;
      advance();
      return s;
    }

    /* IMPORT */
    if (startsCI(t, "IMPORT ")) {
      s.kind = StmtKind::Import;
      s.aux = trimCopy(t.substr(7));
      advance();
      return s;
    }

    /* NAME THE PAGE */
    if (startsCI(t, "NAME THE PAGE ")) {
      s.kind = StmtKind::NamePage;
      s.expr = parseExprOrEmpty(t.substr(14), s.line);
      advance();
      return s;
    }

    /* WEAR STYLE """ … """ — may span logical line already joined */
    if (startsCI(t, "WEAR STYLE")) {
      s.kind = StmtKind::WearStyle;
      advance();
      /* If opener without close, consume lines until """ */
      if (t.find("\"\"\"") != std::string::npos) {
        auto first = t.find("\"\"\"");
        auto second = t.find("\"\"\"", first + 3);
        if (second == std::string::npos) {
          std::ostringstream body;
          body << t.substr(first + 3);
          while (!done()) {
            auto L = lines[i].text;
            advance();
            auto p = L.find("\"\"\"");
            if (p != std::string::npos) {
              body << "\n" << L.substr(0, p);
              break;
            }
            body << "\n" << L;
          }
          s.aux = body.str();
        } else {
          s.aux = t.substr(first + 3, second - (first + 3));
        }
      }
      s.endLine = done() ? s.line : lines[i ? i - 1 : 0].lineNo;
      return s;
    }

    /* SPEAK / SAY */
    if (startsCI(t, "SPEAK ") || startsCI(t, "SAY ") || startsCI(t, "YELL ") ||
        startsCI(t, "SHOUT ")) {
      s.kind = StmtKind::Speak;
      size_t n = startsCI(t, "SPEAK ") ? 6 : startsCI(t, "SAY ") ? 4 : 5;
      s.expr = parseExprOrEmpty(t.substr(n), s.line);
      advance();
      return s;
    }

    /* MAKE SURE */
    if (startsCI(t, "MAKE SURE ")) {
      s.kind = StmtKind::MakeSure;
      s.expr = parseExprOrEmpty(t.substr(10), s.line);
      advance();
      return s;
    }

    /* MY NAME IS */
    if (startsCI(t, "MY NAME IS ")) {
      s.kind = StmtKind::Let;
      auto rest = trimCopy(t.substr(11));
      auto U = toUpperCopy(rest);
      auto asPos = U.find(" AS ");
      auto setPos = U.find(" SET TO ");
      if (asPos != std::string::npos && (setPos == std::string::npos || asPos < setPos)) {
        s.name = trimCopy(rest.substr(0, asPos));
        auto afterAs = trimCopy(rest.substr(asPos + 4));
        auto aU = toUpperCopy(afterAs);
        auto st = aU.find(" SET TO ");
        if (st != std::string::npos) {
          s.typeName = trimCopy(afterAs.substr(0, st));
          s.expr = parseExprOrEmpty(afterAs.substr(st + 8), s.line);
        } else {
          s.typeName = afterAs;
        }
      } else if (setPos != std::string::npos) {
        s.name = trimCopy(rest.substr(0, setPos));
        s.expr = parseExprOrEmpty(rest.substr(setPos + 8), s.line);
      } else {
        s.name = rest;
      }
      advance();
      return s;
    }

    /* SECRET REMEMBER / REMEMBER */
    if (startsCI(t, "SECRET REMEMBER ") || startsCI(t, "REMEMBER ")) {
      s.kind = StmtKind::Remember;
      s.flag = startsCI(t, "SECRET REMEMBER ");
      auto rest = trimCopy(t.substr(s.flag ? 16 : 9));
      auto U = toUpperCopy(rest);
      auto asPos = U.find(" AS ");
      auto setPos = U.find(" SET TO ");
      if (asPos != std::string::npos && (setPos == std::string::npos || asPos < setPos)) {
        s.name = trimCopy(rest.substr(0, asPos));
        auto afterAs = trimCopy(rest.substr(asPos + 4));
        auto aU = toUpperCopy(afterAs);
        auto st = aU.find(" SET TO ");
        if (st != std::string::npos) {
          s.typeName = trimCopy(afterAs.substr(0, st));
          s.expr = parseExprOrEmpty(afterAs.substr(st + 8), s.line);
        } else {
          s.typeName = afterAs;
        }
      } else if (setPos != std::string::npos) {
        s.name = trimCopy(rest.substr(0, setPos));
        s.expr = parseExprOrEmpty(rest.substr(setPos + 8), s.line);
      } else {
        s.name = rest;
      }
      advance();
      return s;
    }

    /* CHANGE … TO */
    if (startsCI(t, "CHANGE ")) {
      s.kind = StmtKind::Change;
      auto rest = trimCopy(t.substr(7));
      auto U = toUpperCopy(rest);
      auto toPos = U.find(" TO ");
      if (toPos != std::string::npos) {
        s.name = trimCopy(rest.substr(0, toPos));
        s.expr = parseExprOrEmpty(rest.substr(toPos + 4), s.line);
      } else {
        s.name = rest;
      }
      advance();
      return s;
    }

    /* SET … TO */
    if (startsCI(t, "SET ") && !startsCI(t, "SET TO ")) {
      s.kind = StmtKind::Set;
      auto rest = trimCopy(t.substr(4));
      auto U = toUpperCopy(rest);
      auto toPos = U.find(" TO ");
      if (toPos != std::string::npos) {
        s.name = trimCopy(rest.substr(0, toPos));
        s.expr = parseExprOrEmpty(rest.substr(toPos + 4), s.line);
      } else {
        s.name = rest;
      }
      advance();
      return s;
    }

    /* ADD … TO … */
    if (startsCI(t, "ADD ") && toUpperCopy(t).find(" TO ") != std::string::npos) {
      s.kind = StmtKind::AddTo;
      auto rest = trimCopy(t.substr(4));
      auto U = toUpperCopy(rest);
      auto toPos = U.find(" TO ");
      s.expr = parseExprOrEmpty(rest.substr(0, toPos), s.line);
      s.name = trimCopy(rest.substr(toPos + 4));
      advance();
      return s;
    }

    /* GIVE BACK */
    if (startsCI(t, "GIVE BACK ") || startsCI(t, "SEND BACK ") || startsCI(t, "HAND BACK ")) {
      s.kind = StmtKind::GiveBack;
      size_t n = startsCI(t, "GIVE BACK ") ? 10 : startsCI(t, "SEND BACK ") ? 10 : 10;
      s.expr = parseExprOrEmpty(t.substr(n), s.line);
      advance();
      return s;
    }

    /* ASK … (statement) */
    if (startsCI(t, "ASK ")) {
      s.kind = StmtKind::Ask;
      s.expr = parseExprOrEmpty(t.substr(4), s.line);
      /* Keep full text for legacy lower of ASK forms */
      advance();
      return s;
    }

    /* BIND */
    if (startsCI(t, "BIND ")) {
      s.kind = StmtKind::Bind;
      advance();
      return s;
    }

    /* WATCH / PUSH WATCH */
    if (startsCI(t, "PUSH WATCH ")) {
      s.kind = StmtKind::PushWatch;
      advance();
      return s;
    }
    if (startsCI(t, "WATCH ")) {
      s.kind = StmtKind::Watch;
      advance();
      return s;
    }

    /* LAY OUT / PAINT */
    if (toUpperCopy(t) == "LAY OUT THE SCREEN" || startsCI(t, "LAY OUT ")) {
      s.kind = StmtKind::LayOut;
      advance();
      return s;
    }
    if (toUpperCopy(t) == "PAINT THE SCREEN" || startsCI(t, "PAINT ")) {
      s.kind = StmtKind::Paint;
      advance();
      return s;
    }

    /* BEGIN COLUMN / END COLUMN */
    if (startsCI(t, "BEGIN COLUMN ") || startsCI(t, "BEGIN ROW ") ||
        startsCI(t, "BEGIN STACK ")) {
      s.kind = StmtKind::BeginColumn;
      advance();
      return s;
    }
    if (toUpperCopy(t) == "END COLUMN" || toUpperCopy(t) == "END ROW" ||
        toUpperCopy(t) == "END STACK") {
      s.kind = StmtKind::EndColumn;
      advance();
      return s;
    }

    /* IF … DO … END IF  [with OTHERWISE] */
    if (startsCI(t, "IF ")) {
      s.kind = StmtKind::If;
      auto rest = trimCopy(t.substr(3));
      stripDo(rest);
      s.expr = parseExprOrEmpty(rest, s.line);
      advance();
      s.body = parseBlockUntil("END IF", "OTHERWISE", "ELSE");
      if (!done() && (startsCI(peekTrimmed(), "OTHERWISE") || startsCI(peekTrimmed(), "ELSE"))) {
        advance();
        s.elseBody = parseBlockUntil("END IF");
      }
      if (!done() && isEnder(peekTrimmed(), "END IF")) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* WHILE … DO … END WHILE */
    if (startsCI(t, "WHILE ")) {
      s.kind = StmtKind::While;
      auto rest = trimCopy(t.substr(6));
      stripDo(rest);
      s.expr = parseExprOrEmpty(rest, s.line);
      advance();
      s.body = parseBlockUntil("END WHILE");
      if (!done() && isEnder(peekTrimmed(), "END WHILE")) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* WHEN REACTIVE … END WHEN REACTIVE */
    if (startsCI(t, "WHEN BACKGROUND REACTIVE ") || startsCI(t, "WHEN REACTIVE WEAK ") ||
        startsCI(t, "WHEN WEAK REACTIVE ") || startsCI(t, "WHEN REACTIVE ")) {
      s.kind = StmtKind::WhenReactive;
      s.flag = startsCI(t, "WHEN BACKGROUND REACTIVE ") || startsCI(t, "WHEN REACTIVE WEAK ") ||
               startsCI(t, "WHEN WEAK REACTIVE ");
      auto rest = t;
      if (startsCI(rest, "WHEN BACKGROUND REACTIVE ")) rest = trimCopy(rest.substr(25));
      else if (startsCI(rest, "WHEN REACTIVE WEAK ")) rest = trimCopy(rest.substr(19));
      else if (startsCI(rest, "WHEN WEAK REACTIVE ")) rest = trimCopy(rest.substr(18));
      else rest = trimCopy(rest.substr(14));
      stripDo(rest);
      auto U = toUpperCopy(rest);
      auto ch = U.find(" CHANGES");
      if (ch != std::string::npos) s.name = trimCopy(rest.substr(0, ch));
      else s.name = rest;
      advance();
      s.body = parseBlockUntil("END WHEN REACTIVE", "END WHEN");
      if (!done() && (isEnder(peekTrimmed(), "END WHEN REACTIVE") ||
                      isEnder(peekTrimmed(), "END WHEN"))) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* WHEN … DO … END WHEN — not WHEN BORN (blueprint lifecycle; ends END BORN). */
    if (startsCI(t, "WHEN ")) {
      if (startsCI(t, "WHEN BORN")) {
        s.kind = StmtKind::Raw;
        advance();
        return s;
      }
      s.kind = StmtKind::When;
      auto rest = trimCopy(t.substr(5));
      stripDo(rest);
      s.aux = rest;
      advance();
      s.body = parseBlockUntil("END WHEN", "END WHEN REACTIVE");
      if (!done() && (isEnder(peekTrimmed(), "END WHEN") ||
                      isEnder(peekTrimmed(), "END WHEN REACTIVE"))) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* THIS IS FUNCTION / MAKE FUNCTION / FUNCTION */
    if (startsCI(t, "THIS IS FUNCTION ") || startsCI(t, "MAKE FUNCTION ") ||
        startsCI(t, "FUNCTION ")) {
      s.kind = StmtKind::Function;
      std::string rest;
      if (startsCI(t, "THIS IS FUNCTION ")) rest = trimCopy(t.substr(17));
      else if (startsCI(t, "MAKE FUNCTION ")) rest = trimCopy(t.substr(14));
      else rest = trimCopy(t.substr(9));
      stripDo(rest);
      auto U = toUpperCopy(rest);
      auto gb = U.find(" GIVES BACK ");
      if (gb == std::string::npos) gb = U.find(" RETURNS ");
      if (gb != std::string::npos) {
        size_t kw = (U.compare(gb, 12, " GIVES BACK ") == 0) ? 12 : 9;
        s.typeName = trimCopy(rest.substr(gb + kw));
        rest = trimCopy(rest.substr(0, gb));
        U = toUpperCopy(rest);
      }
      auto w = U.find(" WITH ");
      if (w == std::string::npos) s.name = rest;
      else {
        s.name = trimCopy(rest.substr(0, w));
        s.aux = trimCopy(rest.substr(w + 6)); /* params raw */
      }
      advance();
      s.body = parseBlockUntil("END FUNCTION");
      if (!done() && isEnder(peekTrimmed(), "END FUNCTION")) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* BLUEPRINT / CLASS */
    if (startsCI(t, "BLUEPRINT ") || startsCI(t, "CLASS ")) {
      s.kind = StmtKind::Blueprint;
      s.name = trimCopy(startsCI(t, "BLUEPRINT ") ? t.substr(10) : t.substr(6));
      stripDo(s.name);
      advance();
      s.body = parseBlockUntil("END BLUEPRINT", "END CLASS");
      if (!done() && (isEnder(peekTrimmed(), "END BLUEPRINT") ||
                      isEnder(peekTrimmed(), "END CLASS"))) {
        s.endLine = cur().lineNo;
        advance();
      }
      return s;
    }

    /* Fallback — still a first-class AST node */
    s.kind = StmtKind::Raw;
    advance();
    return s;
  }
};

void dumpStmt(std::ostringstream &o, const Stmt &s, int indent) {
  for (int i = 0; i < indent; ++i) o << "  ";
  o << stmtKindName(s.kind);
  if (!s.name.empty()) o << " name=" << s.name;
  if (!s.typeName.empty()) o << " ty=" << s.typeName;
  if (s.expr.kind != AstKind::Empty) o << " expr=" << astKindName(s.expr.kind);
  o << " line=" << s.line << "\n";
  for (auto &c : s.body) dumpStmt(o, c, indent + 1);
  if (!s.elseBody.empty()) {
    for (int i = 0; i < indent; ++i) o << "  ";
    o << "else\n";
    for (auto &c : s.elseBody) dumpStmt(o, c, indent + 1);
  }
}

void formatStmt(std::ostringstream &o, const Stmt &s, int indent) {
  auto ind = [&] {
    for (int i = 0; i < indent; ++i) o << "  ";
  };
  if (s.kind == StmtKind::Empty) return;
  ind();
  switch (s.kind) {
  case StmtKind::Speak: {
    auto U = toUpperCopy(s.text);
    size_t n = 0;
    if (startsCI(s.text, "SPEAK ")) n = 6;
    else if (startsCI(s.text, "SAY ")) n = 4;
    else n = 5;
    o << s.text.substr(0, n) << formatExpr(s.text.substr(n)) << "\n";
    break;
  }
  case StmtKind::Let:
    o << "MY NAME IS " << s.name;
    if (!s.typeName.empty()) o << " AS " << s.typeName;
    if (s.expr.kind != AstKind::Empty) {
      auto U = toUpperCopy(s.text);
      auto st = U.find(" SET TO ");
      if (st != std::string::npos) o << " SET TO " << formatExpr(s.text.substr(st + 8));
    }
    o << "\n";
    break;
  case StmtKind::Remember:
    o << (s.flag ? "SECRET REMEMBER " : "REMEMBER ") << s.name;
    if (!s.typeName.empty()) o << " AS " << s.typeName;
    o << "\n";
    break;
  case StmtKind::Change: {
    o << "CHANGE " << s.name << " TO ";
    auto U = toUpperCopy(s.text);
    auto to = U.find(" TO ");
    if (to != std::string::npos) o << formatExpr(s.text.substr(to + 4));
    o << "\n";
    break;
  }
  case StmtKind::If: {
    auto r = trimCopy(s.text.size() > 3 ? s.text.substr(3) : "");
    stripDo(r);
    o << "IF " << formatExpr(r) << " DO\n";
    for (auto &c : s.body) formatStmt(o, c, indent + 1);
    if (!s.elseBody.empty()) {
      ind();
      o << "OTHERWISE\n";
      for (auto &c : s.elseBody) formatStmt(o, c, indent + 1);
    }
    ind();
    o << "END IF\n";
    break;
  }
  case StmtKind::While: {
    auto r = trimCopy(s.text.size() > 6 ? s.text.substr(6) : "");
    stripDo(r);
    o << "WHILE " << formatExpr(r) << " DO\n";
    for (auto &c : s.body) formatStmt(o, c, indent + 1);
    ind();
    o << "END WHILE\n";
    break;
  }
  case StmtKind::Function:
    o << "THIS IS FUNCTION " << s.name;
    if (!s.aux.empty()) o << " WITH " << s.aux;
    if (!s.typeName.empty()) o << " GIVES BACK " << s.typeName;
    o << " DO\n";
    for (auto &c : s.body) formatStmt(o, c, indent + 1);
    ind();
    o << "END FUNCTION\n";
    break;
  case StmtKind::WhenReactive:
    o << "WHEN REACTIVE " << s.name << " CHANGES DO\n";
    for (auto &c : s.body) formatStmt(o, c, indent + 1);
    ind();
    o << "END WHEN REACTIVE\n";
    break;
  case StmtKind::Import:
    o << "IMPORT " << s.aux << "\n";
    break;
  default:
    o << s.text << "\n";
    break;
  }
}

} // namespace

const char *stmtKindName(StmtKind k) {
  switch (k) {
  case StmtKind::Empty: return "Empty";
  case StmtKind::Import: return "Import";
  case StmtKind::Speak: return "Speak";
  case StmtKind::Let: return "Let";
  case StmtKind::Remember: return "Remember";
  case StmtKind::Change: return "Change";
  case StmtKind::Set: return "Set";
  case StmtKind::Ask: return "Ask";
  case StmtKind::GiveBack: return "GiveBack";
  case StmtKind::If: return "If";
  case StmtKind::While: return "While";
  case StmtKind::MakeSure: return "MakeSure";
  case StmtKind::AddTo: return "AddTo";
  case StmtKind::PutIn: return "PutIn";
  case StmtKind::Function: return "Function";
  case StmtKind::Blueprint: return "Blueprint";
  case StmtKind::WhenReactive: return "WhenReactive";
  case StmtKind::When: return "When";
  case StmtKind::Block: return "Block";
  case StmtKind::Watch: return "Watch";
  case StmtKind::PushWatch: return "PushWatch";
  case StmtKind::Bind: return "Bind";
  case StmtKind::WearStyle: return "WearStyle";
  case StmtKind::NamePage: return "NamePage";
  case StmtKind::BeginColumn: return "BeginColumn";
  case StmtKind::EndColumn: return "EndColumn";
  case StmtKind::LayOut: return "LayOut";
  case StmtKind::Paint: return "Paint";
  case StmtKind::Raw: return "Raw";
  }
  return "Unknown";
}

const char *astKindName(AstKind k) {
  switch (k) {
  case AstKind::Ident: return "Ident";
  case AstKind::Number: return "Number";
  case AstKind::String: return "String";
  case AstKind::Unary: return "Unary";
  case AstKind::Binary: return "Binary";
  case AstKind::Group: return "Group";
  case AstKind::Call: return "Call";
  case AstKind::Empty: return "Empty";
  }
  return "Unknown";
}

std::string dumpProgram(const Program &p) {
  std::ostringstream o;
  o << "luke-ast-program stmts=" << p.stmts.size() << "\n";
  for (auto &s : p.stmts) dumpStmt(o, s, 0);
  if (!p.error.empty()) o << "error line=" << p.errorLine << " " << p.error << "\n";
  return o.str();
}

Program parseLuke(const std::string &source) {
  Program prog;
  Parser p;
  p.lines = splitLogicalLines(source);
  p.prog = &prog;
  while (!p.done()) {
    auto t = p.peekTrimmed();
    if (t.empty() || startsCI(t, "//")) {
      p.advance();
      continue;
    }
    prog.stmts.push_back(p.parseStmt());
  }
  return prog;
}

std::string formatProgram(const Program &p) {
  std::ostringstream o;
  for (auto &s : p.stmts) formatStmt(o, s, 0);
  return o.str();
}

namespace {

void flattenStmt(std::ostringstream &o, const Stmt &s) {
  if (s.kind == StmtKind::Empty) return;
  switch (s.kind) {
  case StmtKind::If: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    if (!s.elseBody.empty()) {
      o << "OTHERWISE\n";
      for (auto &c : s.elseBody) flattenStmt(o, c);
    }
    o << "END IF\n";
    break;
  }
  case StmtKind::While: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    o << "END WHILE\n";
    break;
  }
  case StmtKind::Function: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    o << "END FUNCTION\n";
    break;
  }
  case StmtKind::Blueprint: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    o << "END BLUEPRINT\n";
    break;
  }
  case StmtKind::WhenReactive: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    o << "END WHEN REACTIVE\n";
    break;
  }
  case StmtKind::When: {
    o << s.text << "\n";
    for (auto &c : s.body) flattenStmt(o, c);
    o << "END WHEN\n";
    break;
  }
  case StmtKind::WearStyle: {
    if (!s.aux.empty() && s.text.find("\"\"\"") != std::string::npos &&
        s.text.find("\"\"\"", s.text.find("\"\"\"") + 3) == std::string::npos) {
      o << "WEAR STYLE \"\"\"" << s.aux << "\"\"\"\n";
    } else {
      o << s.text << "\n";
    }
    break;
  }
  default:
    o << s.text << "\n";
    break;
  }
}

} // namespace

std::string flattenProgram(const Program &p) {
  std::ostringstream o;
  for (auto &s : p.stmts) flattenStmt(o, s);
  return o.str();
}

std::string formatLukeSource(const std::string &source) {
  return formatProgram(parseLuke(source));
}

} // namespace luke
