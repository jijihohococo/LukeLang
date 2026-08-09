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

enum class K { Num, Flag, Text, Json, List, Map, Void, Ptr };
struct Ty {
  K k = K::Void;
  std::string klass;
  static Ty num() { return {K::Num, ""}; }
  static Ty flag() { return {K::Flag, ""}; }
  static Ty text() { return {K::Text, ""}; }
  static Ty json() { return {K::Json, ""}; }
  static Ty list() { return {K::List, ""}; }
  static Ty map() { return {K::Map, ""}; }
  static Ty vod() { return {K::Void, ""}; }
  static Ty ptr(const std::string &c) { return {K::Ptr, c}; }
};
std::string cTy(const Ty &t) {
  switch (t.k) {
    case K::Num: return "double";
    case K::Flag: return "int";
    case K::Text: return "LukeText";
    case K::Json: return "LukeJson *";
    case K::List: return "LukeList *";
    case K::Map: return "LukeMap *";
    case K::Ptr:
      if (t.klass == "__HttpServer") return "LukeHttpServer *";
      if (t.klass == "__HttpReq") return "LukeHttpRequest *";
      if (t.klass == "__Db") return "LukeDb *";
      return cIdent(t.klass) + " *";
    default: return "void";
  }
}
std::string tyName(const Ty &t) {
  switch (t.k) {
    case K::Num: return "NUMBER";
    case K::Flag: return "FLAG";
    case K::Text: return "TEXT";
    case K::Json: return "JSON";
    case K::List: return "LIST";
    case K::Map: return "MAP";
    case K::Ptr:
      if (t.klass == "__HttpServer") return "SERVER";
      if (t.klass == "__HttpReq") return "REQUEST";
      if (t.klass == "__Db") return "DATABASE";
      return t.klass.empty() ? "blueprint" : t.klass;
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
  bool foreign = false;
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
  bool unsupportedHint = false;
  bool forBrowser = false;
  int arenaSeq = 0;
  int attemptSeq = 0;
  std::vector<std::string> arenaMarks;
  std::vector<std::string> attemptLabels;
  std::vector<bool> attemptHasOtherwise;
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
    if (U == "LIST") return Ty::list();
    if (U == "MAP") return Ty::map();
    if (U == "SERVER" || U == "HTTPSERVER") return Ty::ptr("__HttpServer");
    if (U == "REQUEST" || U == "HTTPREQ") return Ty::ptr("__HttpReq");
    if (U == "DATABASE" || U == "DB") return Ty::ptr("__Db");
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
      if (callee == "__luke_http_listen")
        return mapCall("luke_http_listen", Ty::ptr("__HttpServer"), true);
      if (callee == "__luke_http_accept")
        return mapCall("luke_http_accept", Ty::ptr("__HttpReq"), true);
      if (callee == "__luke_http_reply") return mapCall("luke_http_reply", Ty::flag(), false);
      if (callee == "__luke_http_path") return mapCall("luke_http_path", Ty::text(), false);
      if (callee == "__luke_http_method") return mapCall("luke_http_method", Ty::text(), false);
      if (callee == "__luke_http_query") return mapCall("luke_http_query", Ty::text(), false);
      if (callee == "__luke_http_body") return mapCall("luke_http_body", Ty::text(), false);
      if (callee == "__luke_db_open") return mapCall("luke_db_open", Ty::ptr("__Db"), true);
      if (callee == "__luke_db_exec") return mapCall("luke_db_exec", Ty::flag(), false);
      if (callee == "__luke_db_query") return mapCall("luke_db_query_text", Ty::text(), true);
      if (callee == "__luke_db_close") return mapCall("luke_db_close", Ty::flag(), false);
      if (callee == "__luke_list_new") return mapCall("luke_list_new", Ty::list(), true);
      if (callee == "__luke_list_add") return mapCall("luke_list_add", Ty::vod(), true);
      if (callee == "__luke_list_get") return mapCall("luke_list_get", Ty::text(), false);
      if (callee == "__luke_list_len") return mapCall("luke_list_len", Ty::num(), false);
      if (callee == "__luke_map_new") return mapCall("luke_map_new", Ty::map(), true);
      if (callee == "__luke_map_put") return mapCall("luke_map_put", Ty::vod(), true);
      if (callee == "__luke_map_get") return mapCall("luke_map_get", Ty::text(), false);
      if (callee == "__luke_map_has") return mapCall("luke_map_has", Ty::flag(), false);
      if (callee == "__luke_map_len") return mapCall("luke_map_len", Ty::num(), false);
      if (callee == "__luke_js_fetch") return mapCall("luke_js_fetch", Ty::text(), true);
      if (callee == "__luke_js_on_click") return mapCall("luke_js_on_click", Ty::flag(), false);
      if (callee == "__luke_arg_count") return {"((double)luke_arg_count())", Ty::num()};
      if (callee == "__luke_get_arg") return mapCall("luke_get_arg", Ty::text(), true);
      if (callee == "__luke_get_env") return mapCall("luke_get_env", Ty::text(), true);
      if (callee == "__luke_set_env") return mapCall("luke_set_env", Ty::flag(), false);
      if (callee == "__luke_cwd") return mapCall("luke_cwd", Ty::text(), true);
      if (callee == "__luke_path_join") return mapCall("luke_path_join", Ty::text(), true);
      if (callee == "__luke_path_basename") return mapCall("luke_path_basename", Ty::text(), true);
      if (callee == "__luke_path_dirname") return mapCall("luke_path_dirname", Ty::text(), true);
      if (callee == "__luke_shell") return mapCall("luke_shell", Ty::text(), true);
      if (callee == "__luke_exit") return mapCall("luke_exit_code", Ty::num(), false);
      if (callee == "__luke_js_set_text") return mapCall("luke_js_set_text", Ty::flag(), false);
      if (callee == "__luke_js_set_html") return mapCall("luke_js_set_html", Ty::flag(), false);
      if (callee == "__luke_js_get_value") return mapCall("luke_js_get_value", Ty::text(), true);
      fail(line, "Unknown native helper '" + callee +
                     "' — IMPORT std/files, std/json, std/http, std/server, std/sqlite, "
                     "std/args, std/env, std/paths, std/process, or std/js");
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
    if (fns[name].foreign) {
      call << cIdent(name) << "(";
      for (size_t i = 0; i < checked.size(); ++i) {
        if (i) call << ", ";
        call << checked[i].code;
      }
      call << ")";
    } else {
      call << cIdent(name) << "(arena";
      for (auto &a : checked) call << ", " << a.code;
      call << ")";
    }
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

  /* Collections / problems — conversational expressions */
  if (U == "THE PROBLEM") return {"luke_the_problem()", Ty::text()};
  if (startsWithCI(e, "ITEM ")) {
    auto rest = trim(e.substr(5));
    auto rU = toUpper(rest);
    auto of = rU.find(" OF ");
    if (of != std::string::npos) {
      auto idx = expr(trim(rest.substr(0, of)), line);
      auto listE = expr(trim(rest.substr(of + 4)), line);
      expectTy(line, idx.ty, Ty::num(), "ITEM … OF");
      expectTy(line, listE.ty, Ty::list(), "ITEM … OF");
      return {"luke_list_get(" + listE.code + ", " + idx.code + ")", Ty::text()};
    }
  }
  if (startsWithCI(e, "HOW MANY IN ")) {
    auto col = expr(trim(e.substr(12)), line);
    if (col.ty.k == K::List) return {"luke_list_len(" + col.code + ")", Ty::num()};
    if (col.ty.k == K::Map) return {"luke_map_len(" + col.code + ")", Ty::num()};
    fail(line, "HOW MANY IN needs a LIST or MAP — got " + tyName(col.ty));
    return {"0", Ty::num()};
  }
  if (startsWithCI(e, "GET ")) {
    auto rest = trim(e.substr(4));
    auto rU = toUpper(rest);
    auto fr = rU.find(" FROM ");
    if (fr != std::string::npos) {
      auto key = coerceText(expr(trim(rest.substr(0, fr)), line));
      auto mapE = expr(trim(rest.substr(fr + 6)), line);
      expectTy(line, mapE.ty, Ty::map(), "GET … FROM");
      return {"luke_map_get(" + mapE.code + ", " + key.code + ")", Ty::text()};
    }
  }
  if (startsWithCI(e, "HAS KEY ")) {
    auto rest = trim(e.substr(8));
    auto rU = toUpper(rest);
    auto in = rU.find(" IN ");
    if (in != std::string::npos) {
      auto key = coerceText(expr(trim(rest.substr(0, in)), line));
      auto mapE = expr(trim(rest.substr(in + 4)), line);
      expectTy(line, mapE.ty, Ty::map(), "HAS KEY … IN");
      return {"luke_map_has(" + mapE.code + ", " + key.code + ")", Ty::flag()};
    }
  }

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
  else if (e.ty.k == K::List)
    o << "  luke_speak_number(luke_list_len(" << e.code << "));\n";
  else if (e.ty.k == K::Map)
    o << "  luke_speak_number(luke_map_len(" << e.code << "));\n";
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
                            "' — use NUMBER, TEXT, FLAG, JSON, LIST, MAP, SERVER, "
                            "REQUEST, DATABASE, or a blueprint name");
          return;
        }
        rest = name + " SET TO " + trim(after.substr(set2 + 8));
        U = toUpper(rest);
        setPos = U.find(" SET TO ");
      } else {
        forced = bc.parseTy(after);
        if (forced.k == K::Void) {
          bc.fail(line, "Unknown type '" + after +
                            "' — use NUMBER, TEXT, FLAG, JSON, LIST, MAP, SERVER, "
                            "REQUEST, DATABASE, or a blueprint name");
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
      else if (forced.k == K::List) e = {"luke_list_new(arena)", Ty::list()};
      else if (forced.k == K::Map) e = {"luke_map_new(arena)", Ty::map()};
      else if (forced.k == K::Ptr) {
        if (forced.klass == "__HttpServer")
          e = {"((LukeHttpServer*)0)", forced};
        else if (forced.klass == "__HttpReq")
          e = {"((LukeHttpRequest*)0)", forced};
        else if (forced.klass == "__Db")
          e = {"((LukeDb*)0)", forced};
        else
          e = {"((" + cIdent(forced.klass) + "*)0)", forced};
      } else if (forced.k == K::Void) e = {"luke_text(\"\")", Ty::text()};
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
  if (startsWithCI(text, "IN ARENA") || toUpper(text) == "IN ARENA DO") {
    std::string rest = startsWithCI(text, "IN ARENA") ? trim(text.substr(8)) : "";
    stripDo(rest);
    std::string mark = "_luke_m" + std::to_string(++bc.arenaSeq);
    bc.arenaMarks.push_back(mark);
    o << "  {\n";
    o << "    LukeArenaMark " << mark << " = luke_arena_mark(arena);\n";
    return;
  }
  if (toUpper(text) == "END ARENA" || toUpper(text) == "ENDARENA") {
    if (bc.arenaMarks.empty()) {
      bc.fail(line, "END ARENA without matching IN ARENA");
      return;
    }
    std::string mark = bc.arenaMarks.back();
    bc.arenaMarks.pop_back();
    o << "    luke_arena_reset(arena, " << mark << ");\n";
    o << "  }\n";
    return;
  }

  /* Collections — conversational */
  if (startsWithCI(text, "ADD ")) {
    auto rest = trim(text.substr(4));
    auto U = toUpper(rest);
    auto to = U.find(" TO ");
    if (to != std::string::npos) {
      auto val = bc.expr(trim(rest.substr(0, to)), line);
      auto listName = trim(rest.substr(to + 4));
      if (!bc.locals.count(listName) || bc.locals[listName].k != K::List) {
        bc.fail(line, "ADD … TO needs a LIST — declare MY NAME IS " + listName + " AS LIST");
        return;
      }
      auto v = bc.coerceText(val);
      o << "  luke_list_add(arena, " << cIdent(listName) << ", " << v.code << ");\n";
      return;
    }
  }
  if (startsWithCI(text, "PUT ")) {
    auto rest = trim(text.substr(4));
    auto U = toUpper(rest);
    auto to = U.find(" TO ");
    auto in = U.find(" IN ");
    if (to != std::string::npos && in != std::string::npos && in > to) {
      auto key = bc.expr(trim(rest.substr(0, to)), line);
      auto val = bc.expr(trim(rest.substr(to + 4, in - (to + 4))), line);
      auto mapName = trim(rest.substr(in + 4));
      if (!bc.locals.count(mapName) || bc.locals[mapName].k != K::Map) {
        bc.fail(line, "PUT … IN needs a MAP — declare MY NAME IS " + mapName + " AS MAP");
        return;
      }
      auto k = bc.coerceText(key);
      auto v = bc.coerceText(val);
      o << "  luke_map_put(arena, " << cIdent(mapName) << ", " << k.code << ", " << v.code
        << ");\n";
      return;
    }
  }

  /* ATTEMPT / OTHERWISE / GIVE UP — conversational errors */
  if (toUpper(text) == "ATTEMPT" || toUpper(text) == "ATTEMPT DO" ||
      startsWithCI(text, "ATTEMPT ")) {
    int id = ++bc.attemptSeq;
    std::string lab = "luke_attempt_" + std::to_string(id);
    bc.attemptLabels.push_back(lab);
    bc.attemptHasOtherwise.push_back(false);
    o << "  luke_clear_problem();\n";
    o << "  {\n";
    return;
  }
  if (startsWithCI(text, "OTHERWISE")) {
    if (bc.attemptLabels.empty()) {
      bc.fail(line, "OTHERWISE without matching ATTEMPT");
      return;
    }
    if (bc.attemptHasOtherwise.back()) {
      bc.fail(line, "ATTEMPT already has OTHERWISE — one recovery path only");
      return;
    }
    bc.attemptHasOtherwise.back() = true;
    auto rest = trim(text);
    if (startsWithCI(rest, "OTHERWISE")) rest = trim(rest.substr(9));
    stripDo(rest);
    std::string bind = "problem";
    if (startsWithCI(rest, "WITH ")) {
      bind = trim(rest.substr(5));
      if (bind.empty()) bind = "problem";
    }
    std::string lab = bc.attemptLabels.back();
    o << "    goto " << lab << "_end;\n";
    o << "  " << lab << "_fail:\n";
    o << "    {\n";
    o << "      LukeText " << cIdent(bind) << " = luke_the_problem();\n";
    bc.locals[bind] = Ty::text();
    return;
  }
  if (toUpper(text) == "END ATTEMPT" || toUpper(text) == "ENDATTEMPT") {
    if (bc.attemptLabels.empty()) {
      bc.fail(line, "END ATTEMPT without matching ATTEMPT");
      return;
    }
    std::string lab = bc.attemptLabels.back();
    bool hasO = bc.attemptHasOtherwise.back();
    bc.attemptLabels.pop_back();
    bc.attemptHasOtherwise.pop_back();
    if (hasO) {
      o << "    }\n";
    } else {
      o << "    goto " << lab << "_end;\n";
      o << "  " << lab << "_fail: ;\n";
    }
    o << "  " << lab << "_end: ;\n";
    o << "  }\n";
    return;
  }
  if (startsWithCI(text, "GIVE UP")) {
    if (bc.attemptLabels.empty()) {
      bc.fail(line, "GIVE UP only works inside ATTEMPT … END ATTEMPT");
      return;
    }
    auto rest = trim(text.substr(7));
    Expr msg{"luke_text(\"gave up\")", Ty::text()};
    if (startsWithCI(rest, "WITH ")) {
      msg = bc.coerceText(bc.expr(trim(rest.substr(5)), line));
    } else if (!rest.empty()) {
      msg = bc.coerceText(bc.expr(rest, line));
    }
    std::string lab = bc.attemptLabels.back();
    o << "  luke_set_problem(" << msg.code << ");\n";
    o << "  goto " << lab << "_fail;\n";
    return;
  }

  /* TEST / MAKE SURE */
  if (startsWithCI(text, "TEST ")) {
    auto rest = trim(text.substr(5));
    stripDo(rest);
    auto name = bc.coerceText(bc.expr(rest, line));
    o << "  luke_speak_text(luke_text_concat(arena, luke_text(\"TEST \"), " << name.code
      << "));\n";
    o << "  {\n";
    return;
  }
  if (toUpper(text) == "END TEST" || toUpper(text) == "ENDTEST") {
    o << "  }\n";
    o << "  luke_speak_text(luke_text(\"  ok\"));\n";
    return;
  }
  if (startsWithCI(text, "MAKE SURE ")) {
    auto rest = trim(text.substr(10));
    auto U = toUpper(rest);
    auto eq = U.find(" EQUALS ");
    if (eq == std::string::npos) eq = U.find(" IS EQUAL TO ");
    if (eq == std::string::npos) {
      bc.fail(line, "MAKE SURE needs … EQUALS … — tell me what must match");
      return;
    }
    size_t kwLen = (U.compare(eq, 8, " EQUALS ") == 0) ? 8 : 13;
    auto L = bc.expr(trim(rest.substr(0, eq)), line);
    auto R = bc.expr(trim(rest.substr(eq + kwLen)), line);
    std::string cond;
    if (L.ty.k == K::Text || R.ty.k == K::Text) {
      auto Lt = bc.coerceText(L);
      auto Rt = bc.coerceText(R);
      cond = "luke_text_eq((" + Lt.code + "),(" + Rt.code + "))";
    } else {
      bc.expectTy(line, L.ty, R.ty, "MAKE SURE");
      cond = "((" + L.code + ") == (" + R.code + "))";
    }
    o << "  if (!(" << cond << ")) {\n";
    o << "    luke_speak_text(luke_text(\"MAKE SURE failed on line " << line << "\"));\n";
    o << "    exit(1);\n";
    o << "  }\n";
    return;
  }

  bc.fail(line, "Unsupported Build statement: " + text +
                    " — Build doesn't understand this yet (Play-only feature?)");
  bc.unsupportedHint = true;
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
      if (startsWithCI(text, "FOREIGN FUNCTION ") || startsWithCI(text, "FOREIGN ")) {
        std::string rest =
            startsWithCI(text, "FOREIGN FUNCTION ") ? trim(text.substr(17)) : trim(text.substr(8));
        stripDo(rest);
        Fn f;
        f.foreign = true;
        auto U = toUpper(rest);
        Ty declaredRet = Ty::num();
        auto gb = U.find(" GIVES BACK ");
        if (gb == std::string::npos) gb = U.find(" RETURNS ");
        if (gb != std::string::npos) {
          size_t kwLen = (U.compare(gb, 12, " GIVES BACK ") == 0) ? 12 : 9;
          auto tyRaw = trim(rest.substr(gb + kwLen));
          declaredRet = bc.parseTy(tyRaw);
          if (declaredRet.k == K::Void) {
            bc.fail(lineNo, "Unknown FOREIGN return type '" + tyRaw + "'");
            return false;
          }
          rest = trim(rest.substr(0, gb));
          U = toUpper(rest);
        }
        f.ret = declaredRet;
        f.retDeclared = true;
        auto w = U.find(" WITH ");
        if (w == std::string::npos) f.name = rest;
        else {
          f.name = trim(rest.substr(0, w));
          for (auto &a : splitArgs(trim(rest.substr(w + 6)))) {
            auto p = bc.parseParam(a);
            if (p.ty.k == K::Void) {
              bc.fail(lineNo, "Unknown FOREIGN parameter type on '" + p.name + "'");
              return false;
            }
            f.params.push_back(p);
          }
        }
        if (f.name.empty()) {
          bc.fail(lineNo, "FOREIGN FUNCTION needs a name");
          return false;
        }
        bc.fns[f.name] = f;
        bc.fnOrder.push_back(f.name);
        continue;
      }
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
  if (bc.forBrowser) o << "#define LUKE_BROWSER 1\n";
  o << "#include \"luke_rt.h\"\n";
  o << "#include \"luke_std.h\"\n";
  o << "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n";
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
    if (fn.foreign) {
      o << "extern " << cTy(fn.ret) << " " << cIdent(n) << "(";
      for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i) o << ", ";
        o << cTy(fn.params[i].ty) << " " << cIdent(fn.params[i].name);
      }
      o << ");\n";
    } else {
      o << "static " << cTy(fn.ret) << " " << cIdent(n) << "(LukeArena *arena";
      for (auto &p : fn.params) o << ", " << cTy(p.ty) << " " << cIdent(p.name);
      o << ");\n";
    }
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
    if (fn.foreign) continue;
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
    else if (fn.ret.k == K::List) o << "  return luke_list_new(arena);\n";
    else if (fn.ret.k == K::Map) o << "  return luke_map_new(arena);\n";
    else if (fn.ret.k == K::Ptr) o << "  return (" << cTy(fn.ret) << ")0;\n";
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
  o << "int main(int argc, char **argv) {\n";
  o << "  luke_runtime_set_args(argc, argv);\n";
  o << "  LukeArena arena_storage; LukeArena *arena = &arena_storage;\n";
  o << "  luke_arena_init(arena, 1u<<20);\n";
  for (auto &tl : bc.top) {
    stmt(bc, tl.second, tl.first, o);
    if (bc.bad) return {};
  }
  if (!bc.arenaMarks.empty()) {
    bc.fail(1, "Unclosed IN ARENA — missing END ARENA");
    return {};
  }
  o << "  luke_arena_free(arena);\n  return 0;\n}\n";
  return o.str();
}

static void fillIrSummary(BuildResult &r, BC &bc) {
  std::ostringstream ir;
  ir << "luke-build-ir 1\n";
  ir << "imports " << r.importedFiles.size() << "\n";
  for (auto &p : r.importedFiles) ir << "  " << p << "\n";
  ir << "link " << r.linkLibs.size() << "\n";
  for (auto &l : r.linkLibs) ir << "  -l" << l << "\n";
  ir << "functions " << bc.fnOrder.size() << "\n";
  for (auto &n : bc.fnOrder) {
    auto &fn = bc.fns[n];
    ir << "  " << (fn.foreign ? "foreign " : "fn ") << n << " -> " << tyName(fn.ret) << "\n";
    if (fn.foreign) r.foreignFns.push_back(n);
  }
  ir << "blueprints " << bc.bpOrder.size() << "\n";
  for (auto &n : bc.bpOrder) ir << "  bp " << n << "\n";
  ir << "toplevel " << bc.top.size() << "\n";
  r.irSummary = ir.str();
}

static std::string expandImpl(const std::string &source, const BuildOptions &options, BuildResult &r) {
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

  std::vector<std::string> pkgRoots = options.packagePaths;
  auto addRoot = [&](const std::string &p) {
    if (p.empty()) return;
    for (auto &e : pkgRoots)
      if (e == p) return;
    pkgRoots.push_back(p);
  };
  addRoot("luke_modules");
  addRoot("../luke_modules");
  addRoot("vm/luke_modules");
  addRoot("registry/packages");
  addRoot("../registry/packages");
  if (const char *env = std::getenv("LUKE_PACKAGES")) {
    std::string e = env;
    size_t start = 0;
    while (start <= e.size()) {
      auto colon = e.find(':', start);
      if (colon == std::string::npos) {
        addRoot(e.substr(start));
        break;
      }
      addRoot(e.substr(start, colon - start));
      start = colon + 1;
    }
  }

  auto resolvePackage = [&](const std::string &name) -> std::string {
    for (auto &root : pkgRoots) {
      std::string dir = root + "/" + name;
      auto readPkgEntry = [&](const std::string &pkgFile) -> std::string {
        auto body = readFile(pkgFile);
        if (body.empty()) return {};
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line)) {
          auto t = trim(line);
          if (startsWithCI(t, "entry=")) return trim(t.substr(6));
          auto key = t.find("\"entry\"");
          if (key != std::string::npos) {
            auto colon = t.find(':', key);
            auto q1 = t.find('"', colon + 1);
            auto q2 = t.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
              return t.substr(q1 + 1, q2 - q1 - 1);
          }
        }
        return {};
      };
      if (std::ifstream(dir + "/luke.pkg")) {
        auto entry = readPkgEntry(dir + "/luke.pkg");
        if (entry.empty()) entry = "main.luke";
        std::string path = dir + "/" + entry;
        if (std::ifstream(path)) return path;
      }
      if (std::ifstream(dir + "/main.luke")) return dir + "/main.luke";
      if (std::ifstream(dir + "/" + name + ".luke")) return dir + "/" + name + ".luke";
    }
    return {};
  };

  std::set<std::string> seen;
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

        if (startsWithCI(spec, "c:") || startsWithCI(spec, "C:") || startsWithCI(spec, "ffi:") ||
            startsWithCI(spec, "FFI:")) {
          if (!options.expandCImports) {
            out << "// skipped " << t << "\n";
            continue;
          }
          std::string lib = startsWithCI(spec, "ffi:") || startsWithCI(spec, "FFI:")
                                ? trim(spec.substr(4))
                                : trim(spec.substr(2));
          bool dup = false;
          for (auto &e : r.linkLibs)
            if (e == lib) dup = true;
          if (!dup) r.linkLibs.push_back(lib);
          out << "// IMPORT c:" << lib << "\n";
          continue;
        }

        std::string path;
        if (startsWithCI(spec, "std/") || startsWithCI(spec, "STD/")) {
          if (!options.expandStd) {
            out << "// skipped " << t << "\n";
            continue;
          }
          path = stdlib + "/" + spec.substr(4) + ".luke";
          auto mod = toUpper(spec.substr(4));
          if (mod == "SQLITE") {
            bool dup = false;
            for (auto &e : r.linkLibs)
              if (e == "sqlite3") dup = true;
            if (!dup) r.linkLibs.push_back("sqlite3");
          }
        } else if (startsWithCI(spec, "luke/") || startsWithCI(spec, "LUKE/") ||
                   startsWithCI(spec, "package:") || startsWithCI(spec, "PACKAGE:")) {
          std::string name;
          if (startsWithCI(spec, "luke/") || startsWithCI(spec, "LUKE/"))
            name = spec.substr(5);
          else
            name = spec.substr(8);
          while (!name.empty() && (name.back() == '/' || name.back() == ' ')) name.pop_back();
          path = resolvePackage(name);
          if (path.empty()) {
            r.ok = false;
            r.error = "Build error: package '" + name +
                      "' not found — luke PKG install " + name +
                      " or place it in luke_modules/" + name;
            return {};
          }
        } else {
          if (spec.size() < 5 || spec.substr(spec.size() - 5) != ".luke") spec += ".luke";
          path = baseDir + "/" + spec;
        }
        if (seen.count(path)) continue;
        seen.insert(path);
        auto body = readFile(path);
        if (body.empty()) {
          r.ok = false;
          r.error = "Build error: IMPORT could not open '" + path + "'";
          return {};
        }
        r.importedFiles.push_back(path);
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
  addRoot(base + "/luke_modules");
  return expand(source, base);
}

static BuildResult compileExpanded(const std::string &expanded, const BuildOptions &options,
                                   BuildResult r) {
  if (!r.error.empty()) return r;
  r.expandedSource = expanded;
  BC bc;
  bc.forBrowser = options.forBrowser;
  if (!parse(bc, expanded)) {
    r.ok = false;
    r.error = bc.err.empty() ? "Build parse failed" : bc.err;
    r.unsupportedForBuild = bc.unsupportedHint;
    fillIrSummary(r, bc);
    return r;
  }
  for (auto &n : bc.bpOrder) {
    if (!bc.bps[n].parent.empty() && !bc.bps.count(bc.bps[n].parent)) {
      r.ok = false;
      r.error = "Build error: unknown parent blueprint '" + bc.bps[n].parent + "'";
      return r;
    }
  }
  fillIrSummary(r, bc);
  auto c = emit(bc);
  if (bc.bad) {
    r.ok = false;
    r.error = bc.err;
    r.unsupportedForBuild = bc.unsupportedHint;
    return r;
  }
  if (options.forWasm || options.forBrowser) {
    c = std::string("/* luke target: ") + (options.forBrowser ? "browser" : "wasm/wasi") +
        " */\n" + c;
  }
  r.ok = true;
  r.cSource = std::move(c);
  return r;
}

}  // namespace

BuildResult compileLukeToC(const std::string &source, const BuildOptions &options) {
  BuildResult r;
  std::string expanded = expandImpl(source, options, r);
  if (!r.error.empty()) return r;
  return compileExpanded(expanded, options, std::move(r));
}

std::string expandLukeImports(const std::string &source, const BuildOptions &options,
                              BuildResult *meta) {
  BuildResult local;
  BuildResult &r = meta ? *meta : local;
  std::string expanded = expandImpl(source, options, r);
  if (!r.error.empty()) return {};
  r.expandedSource = expanded;
  return expanded;
}

std::string softenBuildSurfaceForPlay(const std::string &expanded) {
  auto trimLine = [](std::string s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
  };
  auto up = [](std::string s) {
    for (char &c : s) c = (char)toupper((unsigned char)c);
    return s;
  };
  auto starts = [&](const std::string &s, const std::string &p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
      if (toupper((unsigned char)s[i]) != toupper((unsigned char)p[i])) return false;
    return true;
  };
  std::istringstream in(expanded);
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    auto t = trimLine(line);
    if (starts(t, "FOREIGN ") || starts(t, "IN ARENA") || up(t) == "END ARENA" ||
        starts(t, "IMPORT C:") || starts(t, "IMPORT FFI:"))
      continue;
    std::string s = line;
    for (;;) {
      auto U = up(s);
      auto gb = U.find(" GIVES BACK ");
      if (gb == std::string::npos) gb = U.find(" RETURNS ");
      if (gb == std::string::npos) break;
      size_t kwLen = (U.compare(gb, 12, " GIVES BACK ") == 0) ? 12 : 9;
      auto after = trimLine(s.substr(gb + kwLen));
      auto doPos = up(after).find(" DO");
      if (doPos != std::string::npos)
        s = trimLine(s.substr(0, gb)) + " DO" + after.substr(doPos + 3);
      else {
        auto sp = after.find(' ');
        s = trimLine(s.substr(0, gb)) + (sp == std::string::npos ? std::string() : after.substr(sp));
      }
    }
    for (;;) {
      auto U = up(s);
      auto as = U.find(" AS ");
      if (as == std::string::npos) break;
      auto after = trimLine(s.substr(as + 4));
      auto sp = after.find_first_of(" ,");
      std::string rest = sp == std::string::npos ? std::string() : after.substr(sp);
      s = trimLine(s.substr(0, as)) + rest;
    }
    out << s << "\n";
  }
  return out.str();
}

BuildResult analyzeLukeBuild(const std::string &source, const BuildOptions &options) {
  BuildResult r;
  std::string expanded = expandImpl(source, options, r);
  if (!r.error.empty()) return r;
  auto full = compileExpanded(expanded, options, std::move(r));
  full.cSource.clear();
  return full;
}

}  // namespace luke
