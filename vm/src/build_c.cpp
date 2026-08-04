#include "luke/build.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace luke {
namespace {

std::string trim(const std::string &s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}
std::string toUpper(std::string s) {
  for (char &c : s) c = (char)toupper((unsigned char)c);
  return s;
}
bool startsWithCI(const std::string &s, const std::string &p) {
  if (s.size() < p.size()) return false;
  for (size_t i = 0; i < p.size(); ++i)
    if (toupper((unsigned char)s[i]) != toupper((unsigned char)p[i])) return false;
  return true;
}
bool stripDo(std::string &s) {
  auto U = toUpper(s);
  auto p = U.rfind(" DO");
  if (p != std::string::npos && p + 3 == U.size()) {
    s = trim(s.substr(0, p));
    return true;
  }
  return false;
}
std::string cIdent(const std::string &n) {
  std::string o;
  for (char c : n) o.push_back(isalnum((unsigned char)c) ? c : '_');
  if (o.empty() || isdigit((unsigned char)o[0])) o = "_" + o;
  return o;
}
std::string esc(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '"') o += "\\\"";
    else if (c == '\n') o += "\\n";
    else o.push_back(c);
  }
  return o;
}
std::vector<std::string> splitArgs(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  bool q = false;
  char qc = 0;
  for (char c : s) {
    if (q) {
      cur.push_back(c);
      if (c == qc) q = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      q = true;
      qc = c;
      cur.push_back(c);
      continue;
    }
    if (c == ',') {
      auto t = trim(cur);
      if (!t.empty()) out.push_back(t);
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  auto t = trim(cur);
  if (!t.empty()) out.push_back(t);
  return out;
}

enum class K { Num, Flag, Text, Json, Void, Ptr };
struct Ty {
  K k = K::Void;
  std::string klass;
  static Ty num() { return {K::Num, ""}; }
  static Ty flag() { return {K::Flag, ""}; }
  static Ty text() { return {K::Text, ""}; }
  static Ty json() { return {K::Json, ""}; }
  static Ty vod() { return {K::Void, ""}; }
  static Ty ptr(const std::string &c) { return {K::Ptr, c}; }
};
std::string cTy(const Ty &t) {
  switch (t.k) {
    case K::Num: return "double";
    case K::Flag: return "int";
    case K::Text: return "LukeText";
    case K::Json: return "LukeJson *";
    case K::Ptr: return cIdent(t.klass) + " *";
    default: return "void";
  }
}
std::string tyName(const Ty &t) {
  switch (t.k) {
    case K::Num: return "NUMBER";
    case K::Flag: return "FLAG";
    case K::Text: return "TEXT";
    case K::Json: return "JSON";
    case K::Ptr: return t.klass.empty() ? "blueprint" : t.klass;
    default: return "nothing";
  }
}
bool typesEqual(const Ty &a, const Ty &b) {
  if (a.k != b.k) return false;
  if (a.k == K::Ptr) return a.klass == b.klass;
  return true;
}

struct Param {
  std::string name;
  Ty ty;
};
struct Field {
  std::string name;
  Ty ty;
  std::string defRaw;
  bool priv = false;
  std::string owner;
};
struct Method {
  std::string name;
  std::vector<Param> params;
  std::vector<std::string> body;
  std::vector<size_t> lines;
  bool ctor = false;
};
struct BP {
  std::string name, parent;
  std::vector<Field> fields;
  std::vector<Method> methods;
};
struct Fn {
  std::string name;
  std::vector<Param> params;
  Ty ret = Ty::num();
  bool retDeclared = false;
  std::vector<std::string> body;
  std::vector<size_t> lines;
};

struct Expr {
  std::string code;
  Ty ty;
};

struct BC {
  std::string err;
  bool bad = false;
  std::map<std::string, BP> bps;
  std::vector<std::string> bpOrder;
  std::map<std::string, Fn> fns;
  std::vector<std::string> fnOrder;
  std::vector<std::pair<size_t, std::string>> top;
  std::map<std::string, Ty> locals;
  std::string curClass;
  Ty curRet = Ty::vod();
  bool hasCurRet = false;

  void fail(size_t line, const std::string &m) {
    if (bad) return;
    bad = true;
    err = "Build error on line " + std::to_string(line) + ": " + m;
  }

  void expectTy(size_t line, const Ty &got, const Ty &want, const std::string &what) {
    if (want.k == K::Void || got.k == K::Void) return;
    if (!typesEqual(got, want)) {
      fail(line, what + " wants " + tyName(want) + " but got " + tyName(got));
    }
  }

  std::vector<Expr> checkCallArgs(size_t line, const std::string &callee,
                                  const std::vector<Param> &params,
                                  const std::vector<std::string> &args) {
    std::vector<Expr> out;
    if (args.size() != params.size()) {
      fail(line, "'" + callee + "' expects " + std::to_string(params.size()) + " argument" +
                     (params.size() == 1 ? "" : "s") + " but got " + std::to_string(args.size()));
      return out;
    }
    for (size_t i = 0; i < params.size(); ++i) {
      auto e = expr(args[i], line);
      if (bad) return out;
      if (params[i].ty.k == K::Void) {
        fail(line, "'" + callee + "' parameter '" + params[i].name +
                       "' has an unknown type — use AS NUMBER/TEXT/FLAG/JSON or a blueprint name");
        return out;
      }
      expectTy(line, e.ty, params[i].ty,
               "'" + callee + "' argument '" + params[i].name + "'");
      out.push_back(e);
    }
    return out;
  }

  void expectArgs(size_t line, const std::string &callee, const std::vector<Param> &params,
                  const std::vector<std::string> &args) {
    (void)checkCallArgs(line, callee, params, args);
  }

  Ty parseTy(const std::string &t) {
    auto U = toUpper(t);
    if (U == "NUMBER" || U == "NUM") return Ty::num();
    if (U == "FLAG" || U == "BOOL") return Ty::flag();
    if (U == "TEXT" || U == "STRING") return Ty::text();
    if (U == "JSON") return Ty::json();
    if (bps.count(t)) return Ty::ptr(t);
    return Ty::vod();
  }

  Param parseParam(const std::string &raw) {
    Param p;
    auto s = trim(raw);
    auto U = toUpper(s);
    auto as = U.find(" AS ");
    if (as != std::string::npos) {
      p.name = trim(s.substr(0, as));
      auto tyRaw = trim(s.substr(as + 4));
      p.ty = parseTy(tyRaw);
      if (p.ty.k == K::Void) {
        // Unknown annotation — keep Void so callers can fail with context.
        p.ty = Ty::vod();
      }
    } else {
      p.name = s;
      p.ty = Ty::num();
    }
    return p;
  }

  std::vector<Field> flatFields(const std::string &klass) {
    std::vector<std::string> chain;
    for (std::string c = klass; !c.empty(); c = bps[c].parent) chain.push_back(c);
    std::map<std::string, Field> latest;
    for (int i = (int)chain.size() - 1; i >= 0; --i)
      for (auto &f : bps[chain[(size_t)i]].fields) latest[f.name] = f;
    // preserve order: base to derived unique
    std::vector<Field> out;
    std::set<std::string> seen;
    for (int i = (int)chain.size() - 1; i >= 0; --i) {
      for (auto &f : bps[chain[(size_t)i]].fields) {
        if (seen.count(f.name)) continue;
        seen.insert(f.name);
        out.push_back(latest[f.name]);
      }
    }
    return out;
  }

  std::string fname(const Field &f) {
    return f.priv ? ("_priv_" + cIdent(f.name)) : cIdent(f.name);
  }

  Expr coerceText(const Expr &e) {
    if (e.ty.k == K::Text) return e;
    if (e.ty.k == K::Num) return {"luke_number_to_text(arena, (" + e.code + "))", Ty::text()};
    if (e.ty.k == K::Flag)
      return {"luke_text((" + e.code + ") ? \"true\" : \"false\")", Ty::text()};
    return {"luke_text(\"\")", Ty::text()};
  }

  Expr primary(std::string e, size_t line);
  Expr expr(std::string e, size_t line);
};

Expr BC::primary(std::string e, size_t line) {
  e = trim(e);

  // Native stdlib / runtime calls: __luke_read_file(path), etc.
  if (e.size() > 2 && e[0] == '_' && e[1] == '_') {
    auto lp = e.find('(');
    auto rp = e.rfind(')');
    if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
      auto callee = trim(e.substr(0, lp));
      auto args = splitArgs(e.substr(lp + 1, rp - lp - 1));
      auto mapCall = [&](const std::string &cName, Ty ret, bool arenaFirst) -> Expr {
        std::ostringstream call;
        call << cName << "(";
        if (arenaFirst) call << "arena";
        for (size_t i = 0; i < args.size(); ++i) {
          if (arenaFirst || i) call << ", ";
          call << expr(args[i], line).code;
        }
        call << ")";
        return {call.str(), ret};
      };
      if (callee == "__luke_read_file") return mapCall("luke_read_file", Ty::text(), true);
      if (callee == "__luke_write_file") return mapCall("luke_write_file", Ty::flag(), false);
      if (callee == "__luke_file_exists") return mapCall("luke_file_exists", Ty::flag(), false);
      if (callee == "__luke_json_string") return mapCall("luke_json_string", Ty::text(), true);
      if (callee == "__luke_json_parse") return mapCall("luke_json_parse", Ty::json(), true);
      if (callee == "__luke_json_get") return mapCall("luke_json_get", Ty::json(), false);
      if (callee == "__luke_json_index") return mapCall("luke_json_index", Ty::json(), false);
      if (callee == "__luke_json_len") return mapCall("luke_json_len", Ty::num(), false);
      if (callee == "__luke_json_has") return mapCall("luke_json_has", Ty::flag(), false);
      if (callee == "__luke_json_as_text") return mapCall("luke_json_as_text", Ty::text(), true);
      if (callee == "__luke_json_as_number") return mapCall("luke_json_as_number", Ty::num(), false);
      if (callee == "__luke_json_as_flag") return mapCall("luke_json_as_flag", Ty::flag(), false);
      if (callee == "__luke_json_stringify") return mapCall("luke_json_stringify", Ty::text(), true);
      if (callee == "__luke_json_is_null") return mapCall("luke_json_is_null", Ty::flag(), false);
      if (callee == "__luke_http_get") return mapCall("luke_http_get", Ty::text(), true);
      fail(line, "Unknown native helper '" + callee +
                     "' — did you IMPORT std/files, std/json, or std/http?");
      return {"0", Ty::num()};
    }
  }

  if (e.size() >= 2 && ((e.front() == '"' && e.back() == '"') || (e.front() == '\'' && e.back() == '\''))) {
    auto raw = e.substr(1, e.size() - 2);
    std::string unesc;
    for (size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '\\' && i + 1 < raw.size()) {
        char n = raw[++i];
        if (n == 'n') unesc.push_back('\n');
        else if (n == 't') unesc.push_back('\t');
        else if (n == 'r') unesc.push_back('\r');
        else unesc.push_back(n);
      } else
        unesc.push_back(raw[i]);
    }
    return {"luke_text(\"" + esc(unesc) + "\")", Ty::text()};
  }
  auto U = toUpper(e);
  if (U == "TRUE" || U == "YES") return {"1", Ty::flag()};
  if (U == "FALSE" || U == "NO") return {"0", Ty::flag()};
  if (U == "SELF") {
    if (curClass.empty()) {
      fail(line, "SELF only works inside a METHOD or WHEN BORN — you're not in a blueprint method here");
      return {"0", Ty::num()};
    }
    return {"self", Ty::ptr(curClass)};
  }
  if (startsWithCI(e, "SELF.")) {
    auto field = trim(e.substr(5));
    for (auto &f : flatFields(curClass)) {
      if (f.name == field) {
        if (f.priv && f.owner != curClass) {
          fail(line, "Field '" + field + "' is PRIVATE/SECRET on " + f.owner +
                          " — only that blueprint's methods may touch it");
          return {"0", Ty::num()};
        }
        return {"self->" + fname(f), f.ty};
      }
    }
    fail(line, "No field '" + field + "' on blueprint " + curClass + " — declare it with HAS");
    return {"0", Ty::num()};
  }
  char *end = nullptr;
  std::strtod(e.c_str(), &end);
  if (end && end != e.c_str() && *end == '\0') return {e, Ty::num()};

  bool words = !e.empty();
  for (char c : e)
    if (!(isalpha((unsigned char)c) || isspace((unsigned char)c))) words = false;
  if (words && e.find(' ') != std::string::npos)
    return {"luke_text(\"" + esc(e) + "\")", Ty::text()};

  if (locals.count(e)) return {cIdent(e), locals[e]};

  auto dot = e.find('.');
  if (dot != std::string::npos) {
    auto obj = e.substr(0, dot), field = e.substr(dot + 1);
    if (locals.count(obj) && locals[obj].k == K::Ptr) {
      for (auto &f : flatFields(locals[obj].klass))
        if (f.name == field) return {cIdent(obj) + "->" + fname(f), f.ty};
      fail(line, "No field '" + field + "' on " + locals[obj].klass);
      return {"0", Ty::num()};
    }
  }

  fail(line, "I don't know '" + e + "' yet — declare it with MY NAME IS … SET TO … "
             "(or AS NUMBER/TEXT/FLAG)");
  return {"0", Ty::num()};
}

Expr BC::expr(std::string e, size_t line) {
  e = trim(e);
  if (e.empty()) return {"0", Ty::num()};

  if (startsWithCI(e, "ASK ")) {
    auto rest = trim(e.substr(4));
    auto U = toUpper(rest);
    auto toPos = U.find(" TO ");
    if (toPos != std::string::npos) {
      auto obj = trim(rest.substr(0, toPos));
      auto after = trim(rest.substr(toPos + 4));
      auto aU = toUpper(after);
      auto w = aU.find(" WITH ");
      std::string method;
      std::vector<std::string> args;
      if (w == std::string::npos) method = after;
      else {
        method = trim(after.substr(0, w));
        args = splitArgs(trim(after.substr(w + 6)));
      }
      auto recv = expr(obj, line);
      if (recv.ty.k != K::Ptr) {
        fail(line, "ASK TO needs a blueprint instance — got " + tyName(recv.ty));
        return {"0", Ty::num()};
      }
      // Resolve method on klass or ancestors.
      std::string owner = recv.ty.klass;
      Method *meth = nullptr;
      for (std::string c = owner; !c.empty(); c = bps[c].parent) {
        for (auto &m : bps[c].methods) {
          if (!m.ctor && m.name == method) {
            owner = c;
            meth = &m;
            break;
          }
        }
        if (meth) break;
      }
      if (!meth) {
        fail(line, "Unknown method '" + method + "' on " + recv.ty.klass +
                       " — add METHOD " + method + " or CALL PARENT");
        return {"0", Ty::num()};
      }
      auto checked = checkCallArgs(line, method, meth->params, args);
      if (bad) return {"0", Ty::num()};
      std::ostringstream call;
      if (owner == recv.ty.klass) {
        call << cIdent(owner) << "_" << cIdent(method) << "(arena, " << recv.code;
      } else {
        call << cIdent(owner) << "_" << cIdent(method) << "(arena, (" << cIdent(owner) << "*)"
             << recv.code;
      }
      for (auto &a : checked) call << ", " << a.code;
      call << ")";
      return {call.str(), Ty::vod()};
    }
    auto w = U.find(" WITH ");
    std::string name;
    std::vector<std::string> args;
    if (w == std::string::npos) name = rest;
    else {
      name = trim(rest.substr(0, w));
      args = splitArgs(trim(rest.substr(w + 6)));
    }
    if (!fns.count(name)) {
      fail(line, "Unknown function '" + name + "' — define it with THIS IS FUNCTION, or IMPORT it");
      return {"0", Ty::num()};
    }
    auto checked = checkCallArgs(line, name, fns[name].params, args);
    if (bad) return {"0", Ty::num()};
    std::ostringstream call;
    call << cIdent(name) << "(arena";
    for (auto &a : checked) call << ", " << a.code;
    call << ")";
    return {call.str(), fns[name].ret};
  }

  if (startsWithCI(e, "NEW ")) {
    auto rest = trim(e.substr(4));
    auto U = toUpper(rest);
    auto w = U.find(" WITH ");
    std::string cls;
    std::vector<std::string> args;
    if (w == std::string::npos) cls = rest;
    else {
      cls = trim(rest.substr(0, w));
      args = splitArgs(trim(rest.substr(w + 6)));
    }
    if (!bps.count(cls)) {
      fail(line, "Unknown blueprint '" + cls + "' — declare BLUEPRINT " + cls + " or IMPORT it");
      return {"0", Ty::num()};
    }
    std::vector<Param> ctorP;
    for (auto &m : bps[cls].methods)
      if (m.ctor || m.name == "born") ctorP = m.params;
    if (ctorP.empty()) {
      for (std::string c = bps[cls].parent; !c.empty(); c = bps[c].parent) {
        for (auto &m : bps[c].methods) {
          if (m.ctor || m.name == "born") {
            ctorP = m.params;
            break;
          }
        }
        if (!ctorP.empty()) break;
      }
    }
    auto checked = checkCallArgs(line, "NEW " + cls, ctorP, args);
    if (bad) return {"0", Ty::num()};
    std::ostringstream call;
    call << cIdent(cls) << "_new(arena";
    for (auto &a : checked) call << ", " << a.code;
    call << ")";
    return {call.str(), Ty::ptr(cls)};
  }

  auto U = toUpper(e);
  auto cmp = [&](const std::string &needle, const char *op) -> Expr * {
    static Expr r;
    auto pos = U.find(needle);
    if (pos == std::string::npos) return nullptr;
    auto L = expr(trim(e.substr(0, pos)), line);
    auto R = expr(trim(e.substr(pos + needle.size())), line);
    if (!typesEqual(L.ty, R.ty) && !(L.ty.k == K::Num && R.ty.k == K::Num)) {
      // Allow num comparisons; otherwise require matching types.
      if (L.ty.k != R.ty.k) {
        fail(line, "Cannot compare " + tyName(L.ty) + " with " + tyName(R.ty));
        r = {"0", Ty::flag()};
        return &r;
      }
    }
    if (L.ty.k == K::Text) {
      // Text equality via length+memcmp
      if (std::string(op) == "==") {
        r = {"(luke_text_eq((" + L.code + "),(" + R.code + ")))", Ty::flag()};
        return &r;
      }
      fail(line, "TEXT only supports EQUALS comparisons in Build for now");
      r = {"0", Ty::flag()};
      return &r;
    }
    if (L.ty.k != K::Num && L.ty.k != K::Flag) {
      fail(line, "Can only compare NUMBER or FLAG values here (got " + tyName(L.ty) + ")");
      r = {"0", Ty::flag()};
      return &r;
    }
    r = {L.code + op + R.code, Ty::flag()};
    return &r;
  };
  if (auto *r = cmp(" EQUALS ", "==")) return *r;
  if (auto *r = cmp(" IS EQUAL TO ", "==")) return *r;
  if (auto *r = cmp(" IS LESS THAN ", "<")) return *r;
  if (auto *r = cmp(" IS GREATER THAN ", ">")) return *r;

  auto arith = [&](const std::string &mid, const std::string &pref, char op) -> Expr * {
    static Expr r;
    auto finish = [&](Expr L, Expr R) {
      expectTy(line, L.ty, Ty::num(), "Arithmetic");
      expectTy(line, R.ty, Ty::num(), "Arithmetic");
      r = {"(" + L.code + op + R.code + ")", Ty::num()};
      return &r;
    };
    auto pos = U.find(mid);
    if (pos != std::string::npos) {
      return finish(expr(trim(e.substr(0, pos)), line), expr(trim(e.substr(pos + mid.size())), line));
    }
    if (startsWithCI(e, pref)) {
      auto rest = trim(e.substr(pref.size()));
      auto ap = toUpper(rest).find(" AND ");
      if (ap != std::string::npos) {
        return finish(expr(trim(rest.substr(0, ap)), line), expr(trim(rest.substr(ap + 5)), line));
      }
    }
    return nullptr;
  };
  {
    auto pos = U.find(" MULTIPLY BY ");
    if (pos != std::string::npos) {
      auto L = expr(trim(e.substr(0, pos)), line);
      auto R = expr(trim(e.substr(pos + 13)), line);
      expectTy(line, L.ty, Ty::num(), "Arithmetic");
      expectTy(line, R.ty, Ty::num(), "Arithmetic");
      return {"(" + L.code + "*" + R.code + ")", Ty::num()};
    }
  }
  if (auto *r = arith(" ADD ", "ADD ", '+')) return *r;
  if (auto *r = arith(" SUBTRACT ", "SUBTRACT ", '-')) return *r;
  if (auto *r = arith(" MULTIPLY ", "MULTIPLY ", '*')) return *r;
  if (auto *r = arith(" DIVIDE ", "DIVIDE ", '/')) return *r;

  {
    auto pos = U.find(" AND ");
    if (pos != std::string::npos) {
      auto L = coerceText(expr(trim(e.substr(0, pos)), line));
      auto R = coerceText(expr(trim(e.substr(pos + 5)), line));
      return {"luke_text_concat(arena,(" + L.code + "),(" + R.code + "))", Ty::text()};
    }
  }
  if (startsWithCI(e, "NOT ")) {
    auto x = expr(trim(e.substr(4)), line);
    return {"(!(" + x.code + "))", Ty::flag()};
  }
  return primary(e, line);
}

void speak(const Expr &e, std::ostringstream &o) {
  if (e.ty.k == K::Text) o << "  luke_speak_text(" << e.code << ");\n";
  else if (e.ty.k == K::Flag) o << "  luke_speak_flag(" << e.code << ");\n";
  else if (e.ty.k == K::Num) o << "  luke_speak_number(" << e.code << ");\n";
  else if (e.ty.k == K::Json)
    o << "  luke_speak_text(luke_json_stringify(arena, " << e.code << "));\n";
  else if (e.ty.k == K::Void) o << "  " << e.code << ";\n";
  else o << "  luke_speak_text(luke_text(\"<obj>\"));\n";
}

void stmt(BC &bc, const std::string &text, size_t line, std::ostringstream &o) {
  if (startsWithCI(text, "SPEAK ") || startsWithCI(text, "SAY ") || startsWithCI(text, "YELL ") ||
      startsWithCI(text, "SHOUT ")) {
    auto sp = text.find(' ');
    speak(bc.expr(trim(text.substr(sp + 1)), line), o);
    return;
  }
  if (startsWithCI(text, "ASK ")) {
    o << "  " << bc.expr(text, line).code << ";\n";
    return;
  }
  if (startsWithCI(text, "CALL PARENT ") || startsWithCI(text, "CALL SUPER ")) {
    auto rest = startsWithCI(text, "CALL PARENT ") ? trim(text.substr(12)) : trim(text.substr(11));
    auto U = toUpper(rest);
    auto w = U.find(" WITH ");
    std::string method;
    std::vector<std::string> args;
    if (w == std::string::npos) method = rest;
    else {
      method = trim(rest.substr(0, w));
      args = splitArgs(trim(rest.substr(w + 6)));
    }
    auto parent = bc.bps[bc.curClass].parent;
    if (parent.empty()) {
      bc.fail(line, "CALL PARENT needs a parent — this blueprint doesn't FOLLOWS anyone");
      return;
    }
    Method *meth = nullptr;
    for (std::string c = parent; !c.empty(); c = bc.bps[c].parent) {
      for (auto &m : bc.bps[c].methods) {
        if (!m.ctor && m.name == method) {
          meth = &m;
          parent = c;
          break;
        }
      }
      if (meth) break;
    }
    if (!meth) {
      bc.fail(line, "Parent has no method '" + method + "'");
      return;
    }
    auto checked = bc.checkCallArgs(line, "CALL PARENT " + method, meth->params, args);
    if (bc.bad) return;
    o << "  " << cIdent(parent) << "_" << cIdent(method) << "(arena, (" << cIdent(parent)
      << "*)self";
    for (auto &a : checked) o << ", " << a.code;
    o << ");\n";
    return;
  }
  if (startsWithCI(text, "GIVE BACK ") || startsWithCI(text, "SEND BACK ") ||
      startsWithCI(text, "HAND BACK ")) {
    auto U = toUpper(text);
    auto b = U.find(" BACK ");
    auto e = bc.expr(trim(text.substr(b + 6)), line);
    if (bc.hasCurRet) bc.expectTy(line, e.ty, bc.curRet, "GIVE BACK");
    o << "  return " << e.code << ";\n";
    return;
  }
  if (toUpper(text) == "GIVE BACK") {
    if (bc.hasCurRet && bc.curRet.k != K::Void && bc.curRet.k != K::Num)
      bc.fail(line, "GIVE BACK with no value — this function should GIVE BACK " + tyName(bc.curRet));
    o << "  return 0;\n";
    return;
  }
  if (startsWithCI(text, "MY NAME IS ")) {
    auto rest = trim(text.substr(11));
    auto U = toUpper(rest);
    Ty forced = Ty::vod();
    auto asPos = U.find(" AS ");
    auto setPos = U.find(" SET TO ");
    std::string name;
    if (asPos != std::string::npos && (setPos == std::string::npos || asPos < setPos)) {
      name = trim(rest.substr(0, asPos));
      auto after = trim(rest.substr(asPos + 4));
      auto set2 = toUpper(after).find(" SET TO ");
      if (set2 != std::string::npos) {
        auto tyRaw = trim(after.substr(0, set2));
        forced = bc.parseTy(tyRaw);
        if (forced.k == K::Void) {
          bc.fail(line, "Unknown type '" + tyRaw +
                            "' — use NUMBER, TEXT, FLAG, JSON, or a blueprint name");
          return;
        }
        rest = name + " SET TO " + trim(after.substr(set2 + 8));
        U = toUpper(rest);
        setPos = U.find(" SET TO ");
      } else {
        forced = bc.parseTy(after);
        if (forced.k == K::Void) {
          bc.fail(line, "Unknown type '" + after +
                            "' — use NUMBER, TEXT, FLAG, JSON, or a blueprint name");
          return;
        }
        rest = name;
        setPos = std::string::npos;
      }
    }
    Expr e{"0", Ty::num()};
    if (setPos == std::string::npos) {
      name = trim(rest);
      if (forced.k == K::Text) e = {"luke_text(\"\")", Ty::text()};
      else if (forced.k == K::Flag) e = {"0", Ty::flag()};
      else if (forced.k == K::Json) e = {"((LukeJson*)0)", Ty::json()};
      else if (forced.k == K::Ptr) e = {"((" + cIdent(forced.klass) + "*)0)", forced};
      else if (forced.k == K::Void) e = {"luke_text(\"\")", Ty::text()};
      else e = {"0.0", Ty::num()};
      if (forced.k != K::Void) e.ty = forced;
    } else {
      name = trim(rest.substr(0, setPos));
      e = bc.expr(trim(rest.substr(setPos + 8)), line);
      if (forced.k != K::Void) {
        bc.expectTy(line, e.ty, forced, "MY NAME IS " + name + " AS " + tyName(forced));
        e.ty = forced;
      }
    }
    if (!bc.locals.count(name)) {
      bc.locals[name] = e.ty;
      o << "  " << cTy(e.ty) << " " << cIdent(name) << " = " << e.code << ";\n";
    } else {
      bc.expectTy(line, e.ty, bc.locals[name], "MY NAME IS " + name);
      o << "  " << cIdent(name) << " = " << e.code << ";\n";
    }
    return;
  }
  if (startsWithCI(text, "SET ")) {
    auto rest = trim(text.substr(4));
    auto U = toUpper(rest);
    auto to = U.find(" TO ");
    if (to == std::string::npos) {
      bc.fail(line, "Expected SET x TO v — tell me what to change and what to put there");
      return;
    }
    auto target = trim(rest.substr(0, to));
    auto e = bc.expr(trim(rest.substr(to + 4)), line);
    if (startsWithCI(target, "SELF.")) {
      auto field = trim(target.substr(5));
      for (auto &f : bc.flatFields(bc.curClass)) {
        if (f.name == field) {
          if (f.priv && f.owner != bc.curClass) {
            bc.fail(line, "Field '" + field + "' is PRIVATE/SECRET on " + f.owner +
                              " — only that blueprint's methods may touch it");
            return;
          }
          bc.expectTy(line, e.ty, f.ty, "SET SELF." + field);
          o << "  self->" << bc.fname(f) << " = " << e.code << ";\n";
          return;
        }
      }
      bc.fail(line, "Unknown field '" + field + "' on " + bc.curClass + " — declare it with HAS");
      return;
    }
    auto dot = target.find('.');
    if (dot != std::string::npos) {
      auto obj = target.substr(0, dot), field = target.substr(dot + 1);
      if (bc.locals.count(obj) && bc.locals[obj].k == K::Ptr) {
        for (auto &f : bc.flatFields(bc.locals[obj].klass)) {
          if (f.name == field) {
            bc.expectTy(line, e.ty, f.ty, "SET " + target);
            o << "  " << cIdent(obj) << "->" << bc.fname(f) << " = " << e.code << ";\n";
            return;
          }
        }
        bc.fail(line, "No field '" + field + "' on " + bc.locals[obj].klass);
        return;
      }
    }
    if (!bc.locals.count(target)) {
      bc.locals[target] = e.ty;
      o << "  " << cTy(e.ty) << " " << cIdent(target) << " = " << e.code << ";\n";
    } else {
      bc.expectTy(line, e.ty, bc.locals[target], "SET " + target);
      o << "  " << cIdent(target) << " = " << e.code << ";\n";
    }
    return;
  }
  if (startsWithCI(text, "IF ")) {
    auto rest = trim(text.substr(3));
    stripDo(rest);
    auto cond = bc.expr(rest, line);
    if (cond.ty.k != K::Flag && cond.ty.k != K::Num)
      bc.fail(line, "IF needs a FLAG (or NUMBER) condition — got " + tyName(cond.ty));
    o << "  if (" << cond.code << ") {\n";
    return;
  }
  if (toUpper(text) == "END IF" || toUpper(text) == "ENDIF") {
    o << "  }\n";
    return;
  }
  if (startsWithCI(text, "WHILE ")) {
    auto rest = trim(text.substr(6));
    stripDo(rest);
    auto cond = bc.expr(rest, line);
    if (cond.ty.k != K::Flag && cond.ty.k != K::Num)
      bc.fail(line, "WHILE needs a FLAG (or NUMBER) condition — got " + tyName(cond.ty));
    o << "  while (" << cond.code << ") {\n";
    return;
  }
  if (toUpper(text) == "END WHILE" || toUpper(text) == "ENDWHILE") {
    o << "  }\n";
    return;
  }
  bc.fail(line, "Unsupported Build statement: " + text +
                    " — Build doesn't understand this yet (Play-only feature?)");
}

bool parse(BC &bc, const std::string &source) {
  std::istringstream in(source);
  std::string raw;
  size_t lineNo = 0;
  enum Mode { Top, InFn, InBp, InMeth };
  Mode mode = Top;
  Fn curFn;
  BP curBp;
  Method curM;
  bool skipContract = false;

  while (std::getline(in, raw)) {
    ++lineNo;
    auto text = trim(raw);
    if (text.empty() || startsWithCI(text, "//")) continue;
    if (startsWithCI(text, "LET'S START") || startsWithCI(text, "LETS START") ||
        startsWithCI(text, "GET OUTTA HERE"))
      continue;
    if (skipContract) {
      if (toUpper(text) == "END CONTRACT" || toUpper(text) == "ENDCONTRACT") skipContract = false;
      continue;
    }
    if (startsWithCI(text, "CONTRACT ")) {
      skipContract = true;
      continue;
    }

    if (mode == Top) {
      if (startsWithCI(text, "THIS IS FUNCTION ") || startsWithCI(text, "MAKE FUNCTION ") ||
          startsWithCI(text, "RECIPE ")) {
        std::string rest;
        if (startsWithCI(text, "THIS IS FUNCTION ")) rest = trim(text.substr(17));
        else if (startsWithCI(text, "MAKE FUNCTION ")) rest = trim(text.substr(14));
        else rest = trim(text.substr(7));
        stripDo(rest);
        curFn = {};
        auto U = toUpper(rest);
        Ty declaredRet = Ty::vod();
        auto gb = U.find(" GIVES BACK ");
        if (gb == std::string::npos) gb = U.find(" RETURNS ");
        if (gb != std::string::npos) {
          size_t kwLen = (U.compare(gb, 12, " GIVES BACK ") == 0) ? 12 : 9;
          auto tyRaw = trim(rest.substr(gb + kwLen));
          declaredRet = bc.parseTy(tyRaw);
          if (declaredRet.k == K::Void) {
            bc.fail(lineNo, "Unknown return type '" + tyRaw +
                                "' — use NUMBER, TEXT, FLAG, JSON, or a blueprint");
            return false;
          }
          rest = trim(rest.substr(0, gb));
          U = toUpper(rest);
        }
        auto w = U.find(" WITH ");
        if (w == std::string::npos) curFn.name = rest;
        else {
          curFn.name = trim(rest.substr(0, w));
          for (auto &a : splitArgs(trim(rest.substr(w + 6)))) {
            auto p = bc.parseParam(a);
            if (p.ty.k == K::Void) {
              bc.fail(lineNo, "Unknown parameter type on '" + p.name +
                                  "' — use AS NUMBER/TEXT/FLAG/JSON or a blueprint");
              return false;
            }
            curFn.params.push_back(p);
          }
        }
        curFn.ret = declaredRet.k == K::Void ? Ty::num() : declaredRet;
        curFn.retDeclared = declaredRet.k != K::Void;
        mode = InFn;
        continue;
      }
      if (startsWithCI(text, "BLUEPRINT ") || startsWithCI(text, "CLASS ")) {
        std::string rest =
            startsWithCI(text, "BLUEPRINT ") ? trim(text.substr(10)) : trim(text.substr(6));
        stripDo(rest);
        auto impl = toUpper(rest).find(" IMPLEMENTS ");
        if (impl != std::string::npos) rest = trim(rest.substr(0, impl));
        curBp = {};
        auto U = toUpper(rest);
        auto fol = U.find(" FOLLOWS ");
        if (fol == std::string::npos) fol = U.find(" EXTENDS ");
        if (fol == std::string::npos) curBp.name = rest;
        else {
          curBp.name = trim(rest.substr(0, fol));
          auto parent = trim(rest.substr(fol));
          if (startsWithCI(parent, "FOLLOWS ")) parent = trim(parent.substr(8));
          else if (startsWithCI(parent, "EXTENDS ")) parent = trim(parent.substr(8));
          curBp.parent = parent;
        }
        mode = InBp;
        continue;
      }
      bc.top.push_back({lineNo, text});
      continue;
    }

    if (mode == InFn) {
      if (toUpper(text) == "END FUNCTION" || toUpper(text) == "ENDFUNCTION") {
        bc.fns[curFn.name] = curFn;
        bc.fnOrder.push_back(curFn.name);
        mode = Top;
        continue;
      }
      curFn.body.push_back(text);
      curFn.lines.push_back(lineNo);
      continue;
    }

    if (mode == InBp) {
      if (toUpper(text) == "END CLASS" || toUpper(text) == "ENDCLASS" ||
          toUpper(text) == "END BLUEPRINT") {
        bc.bps[curBp.name] = curBp;
        bc.bpOrder.push_back(curBp.name);
        mode = Top;
        continue;
      }
      if (startsWithCI(text, "HAS ") || startsWithCI(text, "PRIVATE ") ||
          startsWithCI(text, "SECRET ")) {
        bool priv = startsWithCI(text, "PRIVATE ") || startsWithCI(text, "SECRET ");
        std::string rest = text;
        if (startsWithCI(rest, "PRIVATE ")) rest = trim(rest.substr(8));
        else if (startsWithCI(rest, "SECRET ")) rest = trim(rest.substr(7));
        if (startsWithCI(rest, "METHOD ")) {
          // method below
        } else {
          if (startsWithCI(rest, "HAS ")) rest = trim(rest.substr(4));
          Field f;
          f.priv = priv;
          f.owner = curBp.name;
          auto U = toUpper(rest);
          auto as = U.find(" AS ");
          auto set = U.find(" SET TO ");
          if (as != std::string::npos && (set == std::string::npos || as < set)) {
            f.name = trim(rest.substr(0, as));
            auto after = trim(rest.substr(as + 4));
            auto set2 = toUpper(after).find(" SET TO ");
            if (set2 != std::string::npos) {
              f.ty = bc.parseTy(trim(after.substr(0, set2)));
              f.defRaw = trim(after.substr(set2 + 8));
            } else
              f.ty = bc.parseTy(after);
          } else if (set != std::string::npos) {
            f.name = trim(rest.substr(0, set));
            f.defRaw = trim(rest.substr(set + 8));
            if (f.defRaw.size() >= 2 && f.defRaw.front() == '"') f.ty = Ty::text();
            else if (toUpper(f.defRaw) == "TRUE" || toUpper(f.defRaw) == "FALSE")
              f.ty = Ty::flag();
            else {
              char *end = nullptr;
              std::strtod(f.defRaw.c_str(), &end);
              f.ty = (end && *end == '\0') ? Ty::num() : Ty::text();
            }
          } else {
            f.name = rest;
            f.ty = Ty::text();
          }
          if (f.ty.k == K::Void) f.ty = Ty::text();
          curBp.fields.push_back(f);
          continue;
        }
      }
      if (startsWithCI(text, "WHEN BORN") ||
          (startsWithCI(text, "BORN") && !startsWithCI(text, "BORNED"))) {
        curM = {};
        curM.ctor = true;
        curM.name = "born";
        std::string rest = startsWithCI(text, "WHEN BORN") ? trim(text.substr(9)) : trim(text.substr(4));
        stripDo(rest);
        if (startsWithCI(rest, "WITH "))
          for (auto &a : splitArgs(trim(rest.substr(5)))) curM.params.push_back(bc.parseParam(a));
        mode = InMeth;
        continue;
      }
      std::string ml = text;
      if (startsWithCI(ml, "PRIVATE METHOD ") || startsWithCI(ml, "SECRET METHOD "))
        ml = trim(ml.substr(ml.find("METHOD")));
      if (startsWithCI(ml, "METHOD ") || startsWithCI(ml, "ACTION ")) {
        curM = {};
        std::string rest = trim(ml.substr(7));
        stripDo(rest);
        auto U = toUpper(rest);
        auto w = U.find(" WITH ");
        if (w == std::string::npos) curM.name = rest;
        else {
          curM.name = trim(rest.substr(0, w));
          for (auto &a : splitArgs(trim(rest.substr(w + 6)))) curM.params.push_back(bc.parseParam(a));
        }
        mode = InMeth;
        continue;
      }
      bc.fail(lineNo, "Unknown blueprint member: " + text);
      return false;
    }

    if (mode == InMeth) {
      if (toUpper(text) == "END BORN" || toUpper(text) == "ENDBORN" ||
          toUpper(text) == "END METHOD" || toUpper(text) == "ENDMETHOD") {
        curBp.methods.push_back(curM);
        mode = InBp;
        continue;
      }
      curM.body.push_back(text);
      curM.lines.push_back(lineNo);
      continue;
    }
  }
  if (mode != Top) {
    bc.fail(lineNo, "Unclosed block");
    return false;
  }
  // Infer / validate function return types from GIVE BACK expressions.
  for (auto &name : bc.fnOrder) {
    auto &fn = bc.fns[name];
    BC probe = bc;
    probe.locals.clear();
    probe.curClass.clear();
    probe.bad = false;
    probe.err.clear();
    for (auto &p : fn.params) probe.locals[p.name] = p.ty;
    Ty inferred = Ty::vod();
    bool sawReturn = false;
    for (size_t i = 0; i < fn.body.size(); ++i) {
      auto &t = fn.body[i];
      if (startsWithCI(t, "GIVE BACK ") || startsWithCI(t, "SEND BACK ") ||
          startsWithCI(t, "HAND BACK ")) {
        auto U = toUpper(t);
        auto b = U.find(" BACK ");
        auto e = probe.expr(trim(t.substr(b + 6)), fn.lines[i]);
        if (probe.bad) {
          bc.fail(fn.lines[i], probe.err);
          return false;
        }
        if (!sawReturn) {
          inferred = e.ty;
          sawReturn = true;
        } else if (!typesEqual(inferred, e.ty)) {
          bc.fail(fn.lines[i], "Function '" + name + "' GIVE BACK types disagree — saw " +
                                   tyName(inferred) + " then " + tyName(e.ty));
          return false;
        }
      }
    }
    if (fn.retDeclared) {
      if (sawReturn && !typesEqual(inferred, fn.ret)) {
        bc.fail(fn.lines.empty() ? 1 : fn.lines[0],
                "Function '" + name + "' should GIVE BACK " + tyName(fn.ret) + " but returns " +
                    tyName(inferred));
        return false;
      }
    } else if (sawReturn) {
      fn.ret = inferred;
    } else {
      fn.ret = Ty::num();
    }
  }
  return !bc.bad;
}

std::string defInit(BC &bc, const Field &f) {
  if (f.defRaw.empty()) {
    if (f.ty.k == K::Text) return "luke_text(\"\")";
    if (f.ty.k == K::Flag) return "0";
    return "0.0";
  }
  return bc.expr(f.defRaw, 1).code;
}

std::string emit(BC &bc) {
  std::ostringstream o;
  o << "/* Generated by Luke Build — native, no GC */\n";
  o << "#include \"luke_rt.h\"\n";
  o << "#include \"luke_std.h\"\n";
  o << "#include <stdio.h>\n#include <string.h>\n\n";
  o << "static LukeText luke_number_to_text(LukeArena *arena, double n) {\n";
  o << "  char buf[64]; int k = snprintf(buf, sizeof(buf), \"%.10g\", n); if (k<0) k=0;\n";
  o << "  char *p=(char*)luke_arena_alloc(arena,(size_t)k+1,1); memcpy(p,buf,(size_t)k+1);\n";
  o << "  return luke_text_n(p,(size_t)k);\n}\n";
  o << "static int luke_text_eq(LukeText a, LukeText b) {\n";
  o << "  return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);\n}\n\n";

  for (auto &n : bc.bpOrder) o << "typedef struct " << cIdent(n) << " " << cIdent(n) << ";\n";
  o << "\n";
  for (auto &n : bc.bpOrder) {
    o << "struct " << cIdent(n) << " {\n";
    auto fs = bc.flatFields(n);
    if (fs.empty()) o << "  char _pad;\n";
    for (auto &f : fs) o << "  " << cTy(f.ty) << " " << bc.fname(f) << ";\n";
    o << "};\n\n";
  }

  for (auto &n : bc.fnOrder) {
    auto &fn = bc.fns[n];
    o << "static " << cTy(fn.ret) << " " << cIdent(n) << "(LukeArena *arena";
    for (auto &p : fn.params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
    o << ");\n";
  }
  for (auto &n : bc.bpOrder) {
    auto &bp = bc.bps[n];
    std::vector<Param> ctorP;
        for (auto &m : bp.methods)
      if (m.ctor || m.name == "born") ctorP = m.params;
    // Inherit constructor signature from nearest ancestor with WHEN BORN.
    if (ctorP.empty()) {
      for (std::string c = bp.parent; !c.empty(); c = bc.bps[c].parent) {
        for (auto &m : bc.bps[c].methods) {
          if (m.ctor || m.name == "born") {
            ctorP = m.params;
            break;
          }
        }
        if (!ctorP.empty()) break;
      }
    }
    o << "static " << cIdent(n) << " *" << cIdent(n) << "_new(LukeArena *arena";
    for (auto &p : ctorP) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
    o << ");\n";
    for (auto &m : bp.methods) {
      if (m.ctor) continue;
      o << "static void " << cIdent(n) << "_" << cIdent(m.name) << "(LukeArena *arena, "
        << cIdent(n) << " *self";
      for (auto &p : m.params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
      o << ");\n";
    }
  }
  o << "\n";

  for (auto &n : bc.fnOrder) {
    auto &fn = bc.fns[n];
    bc.locals.clear();
    bc.curClass.clear();
    bc.curRet = fn.ret;
    bc.hasCurRet = true;
    for (auto &p : fn.params) bc.locals[p.name] = p.ty;
    std::ostringstream body;
    for (size_t i = 0; i < fn.body.size(); ++i) stmt(bc, fn.body[i], fn.lines[i], body);
    if (bc.bad) return {};
    o << "static " << cTy(fn.ret) << " " << cIdent(n) << "(LukeArena *arena";
    for (auto &p : fn.params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
    o << ") {\n" << body.str();
    if (fn.ret.k == K::Num) o << "  return 0.0;\n";
    else if (fn.ret.k == K::Flag) o << "  return 0;\n";
    else if (fn.ret.k == K::Text) o << "  return luke_text(\"\");\n";
    else if (fn.ret.k == K::Json) o << "  return (LukeJson*)0;\n";
    else if (fn.ret.k == K::Ptr) o << "  return (" << cIdent(fn.ret.klass) << "*)0;\n";
    o << "}\n\n";
  }

  for (auto &n : bc.bpOrder) {
    auto &bp = bc.bps[n];
    bc.curClass = n;
    bc.hasCurRet = false;
    bc.curRet = Ty::vod();
    for (auto &m : bp.methods) {
      if (m.ctor) continue;
      bc.locals.clear();
      bc.locals["SELF"] = Ty::ptr(n);
      for (auto &p : m.params) bc.locals[p.name] = p.ty;
      std::ostringstream body;
      for (size_t i = 0; i < m.body.size(); ++i) stmt(bc, m.body[i], m.lines[i], body);
      if (bc.bad) return {};
      o << "static void " << cIdent(n) << "_" << cIdent(m.name) << "(LukeArena *arena, "
        << cIdent(n) << " *self";
      for (auto &p : m.params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
      o << ") {\n" << body.str() << "}\n\n";
    }
    Method *ctor = nullptr;
    for (auto &m : bp.methods)
      if (m.ctor || m.name == "born") ctor = &m;
    // Nearest ancestor ctor if this blueprint has none.
    Method *inheritedCtor = ctor;
    std::string ctorOwner = n;
    if (!inheritedCtor) {
      for (std::string c = bp.parent; !c.empty(); c = bc.bps[c].parent) {
        for (auto &m : bc.bps[c].methods) {
          if (m.ctor || m.name == "born") {
            inheritedCtor = &m;
            ctorOwner = c;
            break;
          }
        }
        if (inheritedCtor) break;
      }
    }
    o << "static " << cIdent(n) << " *" << cIdent(n) << "_new(LukeArena *arena";
    if (inheritedCtor)
      for (auto &p : inheritedCtor->params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
    o << ") {\n";
    o << "  " << cIdent(n) << " *self = (" << cIdent(n) << "*)luke_arena_alloc(arena, sizeof("
      << cIdent(n) << "), sizeof(void*));\n";
    o << "  memset(self, 0, sizeof(*self));\n";
    bc.locals.clear();
    bc.locals["SELF"] = Ty::ptr(n);
    if (inheritedCtor)
      for (auto &p : inheritedCtor->params) bc.locals[p.name] = p.ty;
    for (auto &f : bc.flatFields(n)) {
      o << "  self->" << bc.fname(f) << " = " << defInit(bc, f) << ";\n";
      if (bc.bad) return {};
    }
    if (inheritedCtor) {
      // Run ctor body with self typed as defining class when inherited.
      std::string saved = bc.curClass;
      bc.curClass = ctorOwner;
      // For inherited ctor, methods use Parent* field names — same flattened names on child.
      bc.locals["SELF"] = Ty::ptr(n);  // still child pointer; field names match
      bc.curClass = n;                 // allow child private? use owner for private checks in ctor of parent fields
      if (ctorOwner != n) bc.curClass = ctorOwner;
      for (size_t i = 0; i < inheritedCtor->body.size(); ++i)
        stmt(bc, inheritedCtor->body[i], inheritedCtor->lines[i], o);
      bc.curClass = saved;
      if (bc.bad) return {};
    }
    o << "  return self;\n}\n\n";
  }

  bc.locals.clear();
  bc.curClass.clear();
  bc.hasCurRet = false;
  bc.curRet = Ty::vod();
  o << "int main(void) {\n";
  o << "  LukeArena arena_storage; LukeArena *arena = &arena_storage;\n";
  o << "  luke_arena_init(arena, 1u<<20);\n";
  for (auto &tl : bc.top) {
    stmt(bc, tl.second, tl.first, o);
    if (bc.bad) return {};
  }
  o << "  luke_arena_free(arena);\n  return 0;\n}\n";
  return o.str();
}

}  // namespace

BuildResult compileLukeToC(const std::string &source, const BuildOptions &options) {
  BuildResult r;

  auto dirname = [](std::string p) -> std::string {
    auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return p.substr(0, slash);
  };
  auto readFile = [](const std::string &path) -> std::string {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };

  std::string stdlib = options.stdlibPath;
  if (stdlib.empty()) {
    if (std::ifstream("stdlib/files.luke")) stdlib = "stdlib";
    else if (std::ifstream("vm/stdlib/files.luke")) stdlib = "vm/stdlib";
    else if (std::ifstream("../stdlib/files.luke")) stdlib = "../stdlib";
  }

  std::set<std::string> seen;
  std::vector<std::string> imported;
  std::function<std::string(const std::string &, const std::string &)> expand;
  expand = [&](const std::string &src, const std::string &baseDir) -> std::string {
    std::istringstream in(src);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
      auto t = trim(line);
      if (startsWithCI(t, "IMPORT ")) {
        auto spec = trim(t.substr(7));
        if (spec.size() >= 2 && ((spec.front() == '"' && spec.back() == '"') ||
                                 (spec.front() == '\'' && spec.back() == '\'')))
          spec = spec.substr(1, spec.size() - 2);
        std::string path;
        if (startsWithCI(spec, "std/") || startsWithCI(spec, "STD/")) {
          path = stdlib + "/" + spec.substr(4) + ".luke";
        } else {
          if (spec.size() < 5 || spec.substr(spec.size() - 5) != ".luke") spec += ".luke";
          path = baseDir + "/" + spec;
        }
        if (seen.count(path)) continue;
        seen.insert(path);
        auto body = readFile(path);
        if (body.empty()) {
          r.ok = false;
          r.error = "Build error: IMPORT could not open '" + path +
                    "' — check the path or INSTALL stdlib next to the compiler";
          return {};
        }
        imported.push_back(path);
        out << "// begin IMPORT " << path << "\n";
        out << expand(body, dirname(path));
        out << "// end IMPORT " << path << "\n";
        continue;
      }
      out << line << "\n";
    }
    return out.str();
  };

  std::string base = options.sourcePath.empty() ? "." : dirname(options.sourcePath);
  std::string expanded = expand(source, base);
  if (!r.error.empty()) return r;

  BC bc;
  if (!parse(bc, expanded)) {
    r.ok = false;
    r.error = bc.err.empty() ? "Build parse failed" : bc.err;
    return r;
  }
  for (auto &n : bc.bpOrder) {
    if (!bc.bps[n].parent.empty() && !bc.bps.count(bc.bps[n].parent)) {
      r.ok = false;
      r.error = "Build error: unknown parent blueprint '" + bc.bps[n].parent +
                "' — FOLLOWS a name that was never defined (IMPORT it?)";
      return r;
    }
  }
  auto c = emit(bc);
  if (bc.bad) {
    r.ok = false;
    r.error = bc.err;
    return r;
  }
  if (options.forWasm) {
    // WASI provides main(); keep as-is. Marker comment for tooling.
    c = "/* luke target: wasm/wasi */\n" + c;
  }
  r.ok = true;
  r.cSource = std::move(c);
  r.importedFiles = std::move(imported);
  return r;
}

}  // namespace luke
