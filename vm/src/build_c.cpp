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
/* Conversational "the price" → "price" for reactive / locals. */
std::string stripThe(std::string n) {
  n = trim(n);
  if (startsWithCI(n, "THE ")) n = trim(n.substr(4));
  return n;
}
/* Phase 7 — Entity.field cell id when inside BEGIN ENTITY. */
std::string rxScopedCellName(const std::vector<std::string> &entityStack, const std::string &name) {
  auto n = stripThe(trim(name));
  if (!entityStack.empty()) return entityStack.back() + "." + n;
  return n;
}
std::string resolveRxCellName(const std::map<std::string, bool> &rxCells,
                              const std::vector<std::string> &entityStack, std::string name) {
  name = stripThe(trim(name));
  if (rxCells.count(name)) return name;
  if (name.find('.') != std::string::npos && rxCells.count(name)) return name;
  if (!entityStack.empty()) {
    auto scoped = entityStack.back() + "." + name;
    if (rxCells.count(scoped)) return scoped;
  }
  return name;
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

/* Find needle in haystack (already uppercased) but ignore quoted spans in original. */
size_t findOutsideQuotes(const std::string &orig, const std::string &origUpper,
                         const std::string &needleUpper) {
  bool q = false;
  char qc = 0;
  for (size_t i = 0; i + needleUpper.size() <= origUpper.size(); ++i) {
    char c = orig[i];
    if (q) {
      if (c == qc) q = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      q = true;
      qc = c;
      continue;
    }
    if (origUpper.compare(i, needleUpper.size(), needleUpper) == 0) return i;
  }
  return std::string::npos;
}

std::string unquoteText(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
    s = s.substr(1, s.size() - 2);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        char n = s[++i];
        if (n == 'n') out.push_back('\n');
        else if (n == 't') out.push_back('\t');
        else if (n == 'r') out.push_back('\r');
        else out.push_back(n);
      } else
        out.push_back(s[i]);
    }
    return out;
  }
  return s;
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
  int whenSeq = 0;
  std::vector<std::string> arenaMarks;
  std::vector<std::string> hankaStack; /* "COLUMN" | "ROW" | "STACK" */
  std::vector<std::string> forEachVars;
  int forEachSeq = 0;
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

  /* Page manifest (browser) */
  bool hasPage = false;
  std::string pageTitle;
  std::string pageStyle;
  std::string pageBody;
  std::vector<BrowserFont> pageFonts;
  std::vector<BrowserWhen> pageWhens;
  BrowserWhen *curWhen = nullptr;

  /* Reactive Phase 1/2 — cells / derived / UI binds (see docs/REACTIVE.md) */
  bool usesRx = false;
  bool usesRxUi = false;
  std::string rxGraphVar = "_luke_rx";
  std::map<std::string, bool> rxCells;     /* name → cell or derived id */
  std::map<std::string, bool> rxDerived;   /* name → derived */
  std::map<std::string, Ty> rxCellTy;      /* name → NUMBER or TEXT */
  std::vector<std::string> rxCellOrder;
  struct RxDerivedDef {
    std::string name;
    std::string exprRaw;
    size_t line = 0;
  };
  std::vector<RxDerivedDef> rxDerivedDefs;
  struct RxBindDef {
    std::string argusId; /* unquoted element id */
    std::string exprRaw;
    size_t line = 0;
    int seq = 0;
  };
  std::vector<RxBindDef> rxBindDefs;
  int rxBindSeq = 0;
  /* Phase 5 — BIND LIST name AS "prefix" */
  struct RxListBindDef {
    std::string listName;
    std::string prefixRaw;
    size_t line = 0;
    int seq = 0;
  };
  std::vector<RxListBindDef> rxListBindDefs;
  struct RxOpacityBindDef {
    std::string argusId;
    std::string exprRaw;
    size_t line = 0;
    int seq = 0;
  };
  std::vector<RxOpacityBindDef> rxOpacityBindDefs;
  std::vector<std::string> rxEntityStack;
  struct RxTimelineBind {
    std::string jobId;
    std::string progressCell;
    double from = 0;
    double to = 1;
    double ms = 0;
    size_t line = 0;
  };
  std::vector<RxTimelineBind> rxTimelineBinds;
  bool usesTimeline = false;
  struct RxQueryDef {
    std::string name;
    std::string dbLocal;
    std::string sql;
    size_t line = 0;
  };
  std::vector<RxQueryDef> rxQueryDefs;
  struct RxWhenDef {
    std::string cellName;
    std::vector<std::string> body;
    std::vector<size_t> lines;
    size_t line = 0;
    int seq = 0;
    bool background = false;
    bool weak = false;
  };
  std::vector<RxWhenDef> rxWhenDefs;
  int rxWhenSeq = 0;
  std::vector<std::string> rxComponentStack; /* open BEGIN COMPONENT names */
  std::vector<std::string> rxBoundaryStack;  /* open BEGIN ERROR BOUNDARY names */
  /* Phase 4 — async fetch → reactive cells */
  struct RxFetchBind {
    std::string jobId;
    std::string bodyCell;
    std::string statusCell;
    std::string readyCell;
    size_t line = 0;
  };
  std::vector<RxFetchBind> rxFetchBinds;

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
      if (callee == "__luke_js_add_style") return mapCall("luke_js_add_style", Ty::flag(), false);
      if (callee == "__luke_js_load_font") return mapCall("luke_js_load_font", Ty::flag(), false);
      if (callee == "__luke_js_set_title") return mapCall("luke_js_set_title", Ty::flag(), false);
      if (callee == "__argus_place_text" || callee == "__luke_render_place_text")
        return mapCall("argus_place_text", Ty::flag(), true);
      if (callee == "__argus_place_button" || callee == "__luke_render_place_button")
        return mapCall("argus_place_button", Ty::flag(), true);
      if (callee == "__argus_place_image" || callee == "__luke_render_place_image")
        return mapCall("argus_place_image", Ty::flag(), true);
      if (callee == "__argus_place_box" || callee == "__luke_render_place_box")
        return mapCall("argus_place_box", Ty::flag(), true);
      if (callee == "__argus_place_input") return mapCall("argus_place_input", Ty::flag(), true);
      if (callee == "__argus_paint" || callee == "__luke_render_paint") {
        return {"(argus_paint(arena), 1)", Ty::flag()};
      }
      if (callee == "__argus_clear") {
        return {"(argus_clear(arena), 1)", Ty::flag()};
      }
      if (callee == "__luke_js_route_go") return mapCall("luke_js_route_go", Ty::flag(), false);
      if (callee == "__luke_js_fetch_start") return mapCall("luke_js_fetch_start", Ty::flag(), false);
      if (callee == "__luke_js_fetch_body") return mapCall("luke_js_fetch_body", Ty::text(), true);
      if (callee == "__luke_js_fetch_status")
        return mapCall("luke_js_fetch_status", Ty::num(), false);
      if (callee == "__luke_js_fetch_ready") return mapCall("luke_js_fetch_ready", Ty::flag(), false);
      if (callee == "__luke_looks_like_email")
        return mapCall("luke_looks_like_email", Ty::flag(), false);
      if (callee == "__luke_text_nonempty") return mapCall("luke_text_nonempty", Ty::flag(), false);
      if (callee == "__hanka_begin_column")
        return mapCall("hanka_begin_column", Ty::flag(), true);
      if (callee == "__hanka_begin_row") return mapCall("hanka_begin_row", Ty::flag(), true);
      if (callee == "__hanka_begin_stack") return mapCall("hanka_begin_stack", Ty::flag(), true);
      if (callee == "__hanka_set_align") return mapCall("hanka_set_align", Ty::flag(), true);
      if (callee == "__hanka_set_wrap") return mapCall("hanka_set_wrap", Ty::flag(), true);
      if (callee == "__hanka_slot_text") return mapCall("hanka_slot_text", Ty::flag(), true);
      if (callee == "__hanka_slot_button") return mapCall("hanka_slot_button", Ty::flag(), true);
      if (callee == "__hanka_slot_image") return mapCall("hanka_slot_image", Ty::flag(), true);
      if (callee == "__hanka_slot_input") return mapCall("hanka_slot_input", Ty::flag(), true);
      if (callee == "__hanka_end") return {"(hanka_end(arena), 1)", Ty::flag()};
      if (callee == "__hanka_layout") return {"(hanka_layout(arena), 1)", Ty::flag()};
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

  /* "THE price" → local/reactive "price" when declared. */
  if (startsWithCI(e, "THE ")) {
    auto naked = trim(e.substr(4));
    if (locals.count(naked) || rxCells.count(naked)) e = naked;
  }

  if (!rxCells.count(e)) {
    for (auto &kv : rxCells) {
      auto dot = kv.first.find('.');
      if (dot == std::string::npos) continue;
      if (kv.first.substr(dot + 1) == e) {
        e = kv.first;
        break;
      }
    }
  }

  if (!rxEntityStack.empty()) {
    auto scoped = rxEntityStack.back() + "." + e;
    if (rxCells.count(scoped)) e = scoped;
  }

  bool words = !e.empty();
  for (char c : e)
    if (!(isalpha((unsigned char)c) || isspace((unsigned char)c))) words = false;
  if (words && e.find(' ') != std::string::npos)
    return {"luke_text(\"" + esc(e) + "\")", Ty::text()};

  if (rxCells.count(e)) {
    Ty ty = rxCellTy.count(e) ? rxCellTy[e] : (locals.count(e) ? locals[e] : Ty::num());
    if (ty.k == K::List)
      return {"luke_rx_list_ptr(" + rxGraphVar + ", _luke_rx_id_" + cIdent(e) + ")", Ty::list()};
    if (ty.k == K::Map)
      return {"luke_rx_map_ptr(" + rxGraphVar + ", _luke_rx_id_" + cIdent(e) + ")", Ty::map()};
    if (ty.k == K::Text)
      return {"luke_rx_read_text(" + rxGraphVar + ", _luke_rx_id_" + cIdent(e) + ")", Ty::text()};
    return {"luke_rx_read_num(" + rxGraphVar + ", _luke_rx_id_" + cIdent(e) + ")", Ty::num()};
  }
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

  {
    auto U0 = toUpper(e);
    if (U0 == "THE VIEWPORT WIDTH" || U0 == "THE WINDOW WIDTH")
      return {"argus_viewport_width()", Ty::num()};
    if (U0 == "THE VIEWPORT HEIGHT" || U0 == "THE WINDOW HEIGHT")
      return {"argus_viewport_height()", Ty::num()};
    if (U0 == "THE CLOCK" || U0 == "THE TIME IN MILLISECONDS" || U0 == "THE CLOCK IN MILLISECONDS")
      return {"argus_now_ms()", Ty::num()};
    if (U0 == "THE GRANULAR PAINT COUNT" || U0 == "GRANULAR PAINTS" ||
        U0 == "THE GRANULAR PAINTS") {
      usesRx = true;
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->granular_paints : 0.0)", Ty::num()};
    }
    if (U0 == "THE EPOCH" || U0 == "THE REACTIVE EPOCH")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->epoch : 0.0)", Ty::num()};
    if (U0 == "THE FLUSH COUNT" || U0 == "THE REACTIVE FLUSH COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->flush_count : 0.0)", Ty::num()};
    if (U0 == "THE DERIVED RUN COUNT" || U0 == "THE LAST DERIVED RUN COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_derived : 0.0)",
              Ty::num()};
    if (U0 == "THE EFFECT RUN COUNT" || U0 == "THE LAST EFFECT RUN COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_effects : 0.0)",
              Ty::num()};
    if (U0 == "THE STALE EDGE COUNT" || U0 == "THE STALE EDGES CLEARED" ||
        U0 == "THE LAST STALE EDGE COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_deps_cleared : 0.0)",
              Ty::num()};
    if (U0 == "THE TOTAL STALE EDGE COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->total_deps_cleared : 0.0)",
              Ty::num()};
    if (U0 == "THE FLUSH PASS COUNT" || U0 == "THE REACTIVE FLUSH PASS COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_passes : 0.0)",
              Ty::num()};
    if (U0 == "THE DEFERRED FLUSH COUNT" || U0 == "THE NESTED FLUSH COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->deferred_flush_count : 0.0)",
              Ty::num()};
    if (U0 == "THE DIRTY DEDUP COUNT" || U0 == "THE DIRTY DEDUP HITS")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_dedup_hits : 0.0)",
              Ty::num()};
    if (U0 == "THE DIRTY QUEUE SIZE" || U0 == "THE LAST DIRTY QUEUE SIZE")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_dirty_q_size : 0.0)",
              Ty::num()};
    if (U0 == "THE SCHEDULER STEP COUNT" || U0 == "THE REACTIVE STEP COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_flush_steps : 0.0)",
              Ty::num()};
    if (U0 == "THE SCHEDULER UI BEFORE BACKGROUND" || U0 == "THE UI BEFORE BACKGROUND")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->ui_before_bg : 0.0)", Ty::num()};
    if (U0 == "THE SCHEDULER FIRST STEP" || U0 == "THE FIRST EFFECT STEP")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_first_effect : 0.0)",
              Ty::num()};
    if (U0 == "THE SCHEDULER LAST STEP" || U0 == "THE LAST EFFECT STEP")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_last_effect : 0.0)",
              Ty::num()};
    if (U0 == "THE REGION PAINT COUNT" || U0 == "THE GRANULAR REGION PAINTS")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->region_paints : 0.0)", Ty::num()};
    if (U0 == "THE REGION LAYOUT COUNT" || U0 == "THE GRANULAR REGION LAYOUTS")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->region_layouts : 0.0)", Ty::num()};
    if (U0 == "THE SUBTREE INVALID COUNT" || U0 == "THE SUBTREE INVALIDATIONS")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->subtree_invals : 0.0)", Ty::num()};
    if (U0 == "THE ALIVE NODE COUNT" || U0 == "THE LIVE NODE COUNT")
      return {"(double)luke_rx_alive_count(" + rxGraphVar + ")", Ty::num()};
    if (U0 == "THE DEAD NODE COUNT")
      return {"(double)luke_rx_dead_count(" + rxGraphVar + ")", Ty::num()};
    if (U0 == "THE DISPOSED COUNT" || U0 == "THE DISPOSED NODE COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->disposed_count : 0.0)", Ty::num()};
    if (U0 == "THE LEAK EDGE COUNT" || U0 == "THE LEAK COUNT") {
      usesRx = true;
      return {"(double)((" + rxGraphVar + " ? luke_rx_audit_graph(" + rxGraphVar + ") : 0), "
              + rxGraphVar + " ? (double)" + rxGraphVar + "->last_leak_edges : 0.0)",
              Ty::num()};
    }
    if (U0 == "THE WEAK READ COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->weak_read_count : 0.0)", Ty::num()};
    if (U0 == "THE SCOPE GC COUNT" || U0 == "THE SCOPE FRAMES GC'D")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->scope_gc_count : 0.0)", Ty::num()};
    if (U0 == "THE SCOPE FRAME COUNT" || U0 == "THE OPEN SCOPE COUNT")
      return {"(double)luke_rx_scope_frame_count(" + rxGraphVar + ")", Ty::num()};
    if (U0 == "THE GRAPH CELL COUNT" || U0 == "THE REACTIVE CELL COUNT")
      return {"(double)luke_rx_count_kind(" + rxGraphVar + ", LUKE_RX_CELL)", Ty::num()};
    if (U0 == "THE GRAPH DERIVED COUNT" || U0 == "THE REACTIVE DERIVED COUNT")
      return {"(double)luke_rx_count_kind(" + rxGraphVar + ", LUKE_RX_DERIVED)", Ty::num()};
    if (U0 == "THE GRAPH EFFECT COUNT" || U0 == "THE REACTIVE EFFECT COUNT")
      return {"(double)luke_rx_count_kind(" + rxGraphVar + ", LUKE_RX_EFFECT)", Ty::num()};
    if (U0 == "THE GRAPH EDGE COUNT" || U0 == "THE REACTIVE EDGE COUNT")
      return {"(double)luke_rx_edge_count(" + rxGraphVar + ")", Ty::num()};
    if (U0 == "THE LAST WRITE ID" || U0 == "THE LAST WRITE NODE")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_write_id : 0.0)", Ty::num()};
    if (U0 == "THE GRAPH DUMP COUNT" || U0 == "THE REACTIVE DUMP COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->graph_dump_count : 0.0)", Ty::num()};
    if (U0 == "THE WHY TRACE COUNT" || U0 == "THE REACTIVE TRACE COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->why_trace_count : 0.0)", Ty::num()};
    if (U0 == "THE NEED PAINT FLAG" || U0 == "THE NEED PAINT COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->need_paint : 0.0)", Ty::num()};
    if (U0 == "THE NEED LAYOUT FLAG" || U0 == "THE NEED LAYOUT COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->need_layout : 0.0)", Ty::num()};
    if (U0 == "THE REACTIVE ERROR COUNT" || U0 == "THE ERROR COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->error_count : 0.0)", Ty::num()};
    if (U0 == "THE ERROR ISOLATION COUNT" || U0 == "THE ISOLATED ERROR COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->error_isolation_count : 0.0)",
              Ty::num()};
    if (U0 == "THE REACTIVE RETRY COUNT" || U0 == "THE RETRY COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->retry_count : 0.0)", Ty::num()};
    if (U0 == "THE LAST ERROR NODE" || U0 == "THE ERROR NODE")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_error_node : 0.0)", Ty::num()};
    if (U0 == "THE ASYNC FAILURE COUNT" || U0 == "THE FETCH FAILURE COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->async_failure_count : 0.0)",
              Ty::num()};
    if (U0 == "THE BOUNDARY TRIP COUNT" || U0 == "THE ERROR BOUNDARY TRIP COUNT")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->boundary_trip_count : 0.0)",
              Ty::num()};
    if (U0 == "THE LAST BOUNDARY TRIPPED" || U0 == "THE BOUNDARY TRIPPED ID")
      return {"(" + rxGraphVar + " ? (double)" + rxGraphVar + "->last_boundary_tripped : 0.0)",
              Ty::num()};
  }

  if (startsWithCI(e, "THE TEXT WIDTH OF ") || startsWithCI(e, "THE MEASURED WIDTH OF ")) {
    size_t prefix = startsWithCI(e, "THE TEXT WIDTH OF ") ? 18 : 22;
    auto t = coerceText(expr(trim(e.substr(prefix)), line));
    return {"argus_measure_text(" + t.code + ")", Ty::num()};
  }

  if (startsWithCI(e, "THE BOUNDARY TRIPPED FOR ") ||
      startsWithCI(e, "THE ERROR BOUNDARY TRIPPED FOR ")) {
    usesRx = true;
    size_t prefix = startsWithCI(e, "THE BOUNDARY TRIPPED FOR ") ? 25 : 31;
    auto name = stripThe(trim(e.substr(prefix)));
    return {"(double)luke_rx_boundary_tripped(" + rxGraphVar + ", \"" + esc(name) + "\")",
            Ty::num()};
  }

  if (startsWithCI(e, "THE TIMELINE STEP ID AT ") || startsWithCI(e, "THE TIMELINE STEP WAVE AT ")) {
    usesRx = true;
    bool wave = startsWithCI(e, "THE TIMELINE STEP WAVE AT ");
    size_t prefix = wave ? 26 : 24;
    auto idxE = expr(trim(e.substr(prefix)), line);
    expectTy(line, idxE.ty, Ty::num(), wave ? "THE TIMELINE STEP WAVE AT" : "THE TIMELINE STEP ID AT");
    if (wave)
      return {"(double)luke_rx_timeline_step_wave(" + rxGraphVar + ", (size_t)(" + idxE.code + "))",
              Ty::num()};
    return {"(double)luke_rx_timeline_step_id(" + rxGraphVar + ", (size_t)(" + idxE.code + "))",
            Ty::num()};
  }

  if (startsWithCI(e, "THE NODE ID OF ")) {
    usesRx = true;
    auto name = resolveRxCellName(rxCells, rxEntityStack, stripThe(trim(e.substr(15))));
    if (!rxCells.count(name)) {
      fail(line, "THE NODE ID OF needs a reactive cell — not '" + trim(e.substr(15)) + "'");
      return {"0", Ty::num()};
    }
    return {"(double)_luke_rx_id_" + cIdent(name), Ty::num()};
  }
  if (startsWithCI(e, "THE DEP COUNT OF ")) {
    usesRx = true;
    auto name = resolveRxCellName(rxCells, rxEntityStack, stripThe(trim(e.substr(17))));
    if (!rxCells.count(name)) {
      fail(line, "THE DEP COUNT OF needs a reactive cell — not '" + trim(e.substr(17)) + "'");
      return {"0", Ty::num()};
    }
    return {"(double)luke_rx_dep_count(" + rxGraphVar + ", _luke_rx_id_" + cIdent(name) + ")",
            Ty::num()};
  }
  if (startsWithCI(e, "THE SUB COUNT OF ")) {
    usesRx = true;
    auto name = resolveRxCellName(rxCells, rxEntityStack, stripThe(trim(e.substr(17))));
    if (!rxCells.count(name)) {
      fail(line, "THE SUB COUNT OF needs a reactive cell — not '" + trim(e.substr(17)) + "'");
      return {"0", Ty::num()};
    }
    return {"(double)luke_rx_sub_count(" + rxGraphVar + ", _luke_rx_id_" + cIdent(name) + ")",
            Ty::num()};
  }
  if (startsWithCI(e, "THE WHY ROOT OF ")) {
    usesRx = true;
    auto name = resolveRxCellName(rxCells, rxEntityStack, stripThe(trim(e.substr(16))));
    if (!rxCells.count(name)) {
      fail(line, "THE WHY ROOT OF needs a reactive cell — not '" + trim(e.substr(16)) + "'");
      return {"0", Ty::num()};
    }
    return {"(double)luke_rx_why_root(" + rxGraphVar + ", _luke_rx_id_" + cIdent(name) + ")",
            Ty::num()};
  }
  if (startsWithCI(e, "THE WHY DEPTH OF ")) {
    usesRx = true;
    auto name = resolveRxCellName(rxCells, rxEntityStack, stripThe(trim(e.substr(17))));
    if (!rxCells.count(name)) {
      fail(line, "THE WHY DEPTH OF needs a reactive cell — not '" + trim(e.substr(17)) + "'");
      return {"0", Ty::num()};
    }
    return {"(double)luke_rx_why_depth(" + rxGraphVar + ", _luke_rx_id_" + cIdent(name) + ")",
            Ty::num()};
  }

  if (startsWithCI(e, "THE WEAK VALUE OF ")) {
    usesRx = true;
    auto inner = trim(e.substr(18));
    auto pe = primary(inner, line);
    std::string w = pe.code;
    for (size_t pos = 0; (pos = w.find("luke_rx_read_num(", pos)) != std::string::npos;) {
      w.replace(pos, 17, "luke_rx_read_num_weak(");
      pos += 22;
    }
    for (size_t pos = 0; (pos = w.find("luke_rx_read_text(", pos)) != std::string::npos;) {
      w.replace(pos, 18, "luke_rx_read_text_weak(");
      pos += 23;
    }
    pe.code = w;
    return pe;
  }

  if (startsWithCI(e, "THE VALUE OF ")) {
    auto id = coerceText(expr(trim(e.substr(13)), line));
    return {"luke_js_get_value(arena, " + id.code + ")", Ty::text()};
  }
  if (startsWithCI(e, "THE BODY OF FETCH ")) {
    auto id = coerceText(expr(trim(e.substr(18)), line));
    return {"luke_js_fetch_body(arena, " + id.code + ")", Ty::text()};
  }
  if (startsWithCI(e, "THE STATUS OF FETCH ")) {
    auto id = coerceText(expr(trim(e.substr(20)), line));
    return {"luke_js_fetch_status(" + id.code + ")", Ty::num()};
  }
  if (startsWithCI(e, "FETCH ") && toUpper(e).find(" IS READY") != std::string::npos) {
    auto U = toUpper(e);
    auto ready = U.find(" IS READY");
    auto id = coerceText(expr(trim(e.substr(6, ready - 6)), line));
    return {"luke_js_fetch_ready(" + id.code + ")", Ty::flag()};
  }

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
    auto pos = findOutsideQuotes(e, U, needle);
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
  if (auto *r = cmp(" IS LESS THAN OR EQUAL TO ", "<=")) return *r;
  if (auto *r = cmp(" IS GREATER THAN OR EQUAL TO ", ">=")) return *r;

  auto arith = [&](const std::string &mid, const std::string &pref, char op) -> Expr * {
    static Expr r;
    auto finish = [&](Expr L, Expr R) {
      expectTy(line, L.ty, Ty::num(), "Arithmetic");
      expectTy(line, R.ty, Ty::num(), "Arithmetic");
      r = {"(" + L.code + op + R.code + ")", Ty::num()};
      return &r;
    };
    auto pos = findOutsideQuotes(e, U, mid);
    if (pos != std::string::npos) {
      return finish(expr(trim(e.substr(0, pos)), line), expr(trim(e.substr(pos + mid.size())), line));
    }
    if (startsWithCI(e, pref)) {
      auto rest = trim(e.substr(pref.size()));
      auto ap = findOutsideQuotes(rest, toUpper(rest), " AND ");
      if (ap != std::string::npos) {
        return finish(expr(trim(rest.substr(0, ap)), line), expr(trim(rest.substr(ap + 5)), line));
      }
    }
    return nullptr;
  };
  {
    size_t pos = findOutsideQuotes(e, U, " MULTIPLIED BY ");
    size_t mid = 15;
    if (pos == std::string::npos) {
      pos = findOutsideQuotes(e, U, " MULTIPLY BY ");
      mid = 13;
    }
    if (pos != std::string::npos) {
      auto L = expr(trim(e.substr(0, pos)), line);
      auto R = expr(trim(e.substr(pos + mid)), line);
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
    size_t pos = findOutsideQuotes(e, U, " DIVIDED BY ");
    if (pos != std::string::npos) {
      auto L = expr(trim(e.substr(0, pos)), line);
      auto R = expr(trim(e.substr(pos + 12)), line);
      expectTy(line, L.ty, Ty::num(), "Arithmetic");
      expectTy(line, R.ty, Ty::num(), "Arithmetic");
      return {"(" + L.code + "/" + R.code + ")", Ty::num()};
    }
  }

  {
    auto pos = findOutsideQuotes(e, U, " AND ");
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
  if (startsWithCI(e, "IF ")) {
    auto rest = trim(e.substr(3));
    auto rU = toUpper(rest);
    auto thenPos = rU.find(" THEN ");
    auto otherwisePos = rU.find(" OTHERWISE ");
    if (thenPos != std::string::npos && otherwisePos != std::string::npos &&
        otherwisePos > thenPos + 6) {
      auto cond = expr(trim(rest.substr(0, thenPos)), line);
      auto thenE = expr(trim(rest.substr(thenPos + 6, otherwisePos - (thenPos + 6))), line);
      auto elseE = expr(trim(rest.substr(otherwisePos + 11)), line);
      if (cond.ty.k != K::Flag && cond.ty.k != K::Num)
        fail(line, "IF … THEN … OTHERWISE needs a FLAG or NUMBER condition");
      expectTy(line, thenE.ty, Ty::num(), "IF … THEN … OTHERWISE");
      expectTy(line, elseE.ty, Ty::num(), "IF … THEN … OTHERWISE");
      return {"((" + cond.code + ") ? (" + thenE.code + ") : (" + elseE.code + "))", Ty::num()};
    }
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
  if (startsWithCI(text, "REMEMBER ")) {
    auto rest = trim(text.substr(9));
    auto U = toUpper(rest);
    auto asPos = U.find(" AS ");
    if (asPos == std::string::npos) {
      bc.fail(line, "REMEMBER needs: REMEMBER name AS value");
      return;
    }
    auto name = stripThe(trim(rest.substr(0, asPos)));
    auto valRaw = trim(rest.substr(asPos + 4));
    if (name.empty()) {
      bc.fail(line, "REMEMBER needs a name");
      return;
    }
    /* REMEMBER notes AS QUERY ON db AS "SELECT …" */
    if (startsWithCI(valRaw, "QUERY ON ")) {
      auto qrest = trim(valRaw.substr(9));
      auto qU = toUpper(qrest);
      auto qAs = qU.find(" AS ");
      if (qAs == std::string::npos) {
        bc.fail(line, "REMEMBER QUERY needs: REMEMBER name AS QUERY ON db AS \"sql\"");
        return;
      }
      auto dbName = trim(qrest.substr(0, qAs));
      auto sqlE = bc.coerceText(bc.expr(trim(qrest.substr(qAs + 4)), line));
      if (!bc.locals.count(dbName) || bc.locals[dbName].k != K::Ptr ||
          bc.locals[dbName].klass != "__Db") {
        bc.fail(line, "QUERY ON needs a DATABASE — MY NAME IS " + dbName + " AS DATABASE");
        return;
      }
      std::string key = rxScopedCellName(bc.rxEntityStack, name);
      bc.usesRx = true;
      bc.locals[name] = Ty::text();
      bc.rxCellTy[key] = Ty::text();
      if (!bc.rxCells.count(key)) bc.rxCellOrder.push_back(key);
      bc.rxCells[key] = true;
      bc.rxQueryDefs.push_back({key, dbName, unquoteText(trim(qrest.substr(qAs + 4))), line});
      o << "  _luke_rx_id_" << cIdent(key) << " = luke_rx_cell_text(_luke_rx, luke_text(\"\"));\n";
      o << "  luke_rx_query_refresh(_luke_rx, _luke_rx_id_" << cIdent(key) << ", "
        << cIdent(dbName) << ", " << sqlE.code << ");\n";
      return;
    }
    /* Optional: REMEMBER x AS NUMBER SET TO 100 */
    auto vU = toUpper(valRaw);
    auto setPos = vU.find(" SET TO ");
    Ty forced = Ty::vod();
    if (setPos != std::string::npos) {
      forced = bc.parseTy(trim(valRaw.substr(0, setPos)));
      valRaw = trim(valRaw.substr(setPos + 8));
    } else {
      Ty maybe = bc.parseTy(valRaw);
      if (maybe.k != K::Void && valRaw.find(' ') == std::string::npos &&
          !std::isdigit((unsigned char)valRaw[0]) && valRaw[0] != '-' && valRaw[0] != '"' &&
          toUpper(valRaw) != "TRUE" && toUpper(valRaw) != "FALSE") {
        /* REMEMBER x AS NUMBER|TEXT|LIST|MAP — typed empty cell/collection */
        forced = maybe;
        valRaw.clear();
      }
    }
    /* Reactive collections */
    std::string cellKey = rxScopedCellName(bc.rxEntityStack, name);
    if (forced.k == K::List || (valRaw.empty() && forced.k == K::List)) {
      bc.usesRx = true;
      bc.locals[name] = Ty::list();
      bc.rxCellTy[cellKey] = Ty::list();
      if (!bc.rxCells.count(cellKey)) bc.rxCellOrder.push_back(cellKey);
      bc.rxCells[cellKey] = true;
      o << "  _luke_rx_id_" << cIdent(cellKey) << " = luke_rx_list(_luke_rx);\n";
      return;
    }
    if (forced.k == K::Map) {
      bc.usesRx = true;
      bc.locals[name] = Ty::map();
      bc.rxCellTy[cellKey] = Ty::map();
      if (!bc.rxCells.count(cellKey)) bc.rxCellOrder.push_back(cellKey);
      bc.rxCells[cellKey] = true;
      o << "  _luke_rx_id_" << cIdent(cellKey) << " = luke_rx_map(_luke_rx);\n";
      return;
    }
    Expr e{"0.0", Ty::num()};
    if (valRaw.empty()) {
      if (forced.k == K::Text) e = {"luke_text(\"\")", Ty::text()};
      else if (forced.k == K::Flag) e = {"0", Ty::flag()};
      else e = {"0.0", Ty::num()};
      if (forced.k != K::Void) e.ty = forced;
    } else {
      e = bc.expr(valRaw, line);
      if (forced.k != K::Void) {
        bc.expectTy(line, e.ty, forced, "REMEMBER " + name);
        e.ty = forced;
      }
    }
    if (e.ty.k != K::Num && e.ty.k != K::Text) {
      bc.fail(line, "REMEMBER supports NUMBER, TEXT, LIST, or MAP — got " + tyName(e.ty));
      return;
    }
    bc.usesRx = true;
    bc.locals[name] = e.ty;
    bc.rxCellTy[cellKey] = e.ty;
    if (!bc.rxCells.count(cellKey)) bc.rxCellOrder.push_back(cellKey);
    bc.rxCells[cellKey] = true;
    if (e.ty.k == K::Text)
      o << "  _luke_rx_id_" << cIdent(cellKey) << " = luke_rx_cell_text(_luke_rx, " << e.code
        << ");\n";
    else
      o << "  _luke_rx_id_" << cIdent(cellKey) << " = luke_rx_cell(_luke_rx, " << e.code << ");\n";
    return;
  }
  if (startsWithCI(text, "CHANGE ")) {
    auto rest = trim(text.substr(7));
    auto U = toUpper(rest);
    auto to = U.find(" TO ");
    if (to == std::string::npos) {
      bc.fail(line, "CHANGE needs: CHANGE name TO value");
      return;
    }
    auto name = resolveRxCellName(bc.rxCells, bc.rxEntityStack, trim(rest.substr(0, to)));
    auto e = bc.expr(trim(rest.substr(to + 4)), line);
    if (!bc.rxCells.count(name)) {
      bc.fail(line, "CHANGE '" + name + "' — REMEMBER it first (reactive cell)");
      return;
    }
    if (bc.rxDerived.count(name)) {
      bc.fail(line, "Cannot CHANGE derived '" + name + "' — change its inputs instead");
      return;
    }
    Ty want = bc.rxCellTy.count(name) ? bc.rxCellTy[name] : Ty::num();
    if (want.k == K::List || want.k == K::Map) {
      bc.fail(line, "Cannot CHANGE a LIST/MAP — use ADD / SET ITEM / PUT");
      return;
    }
    bc.expectTy(line, e.ty, want, "CHANGE " + name);
    bc.usesRx = true;
    if (want.k == K::Text)
      o << "  luke_rx_write_text(_luke_rx, _luke_rx_id_" << cIdent(name) << ", " << e.code << ");\n";
    else
      o << "  luke_rx_write_num(_luke_rx, _luke_rx_id_" << cIdent(name) << ", " << e.code << ");\n";
    return;
  }
  if (startsWithCI(text, "INCREASE ")) {
    auto rest = trim(text.substr(9));
    auto U = toUpper(rest);
    auto by = U.find(" BY ");
    if (by == std::string::npos) {
      bc.fail(line, "INCREASE needs: INCREASE name BY amount");
      return;
    }
    auto name = resolveRxCellName(bc.rxCells, bc.rxEntityStack, trim(rest.substr(0, by)));
    auto e = bc.expr(trim(rest.substr(by + 4)), line);
    if (!bc.rxCells.count(name) || bc.rxDerived.count(name)) {
      bc.fail(line, "INCREASE needs a REMEMBER'd NUMBER cell — not '" + name + "'");
      return;
    }
    bc.expectTy(line, e.ty, Ty::num(), "INCREASE " + name);
    bc.usesRx = true;
    o << "  luke_rx_write_num(_luke_rx, _luke_rx_id_" << cIdent(name) << ", luke_rx_read_num(_luke_rx, _luke_rx_id_"
      << cIdent(name) << ") + (" << e.code << "));\n";
    return;
  }
  if (toUpper(text) == "BEGIN REACTIVE BATCH" || toUpper(text) == "BEGIN BATCH") {
    bc.usesRx = true;
    o << "  luke_rx_batch_begin(_luke_rx);\n";
    return;
  }
  if (toUpper(text) == "END REACTIVE BATCH" || toUpper(text) == "END BATCH") {
    bc.usesRx = true;
    o << "  luke_rx_batch_end(_luke_rx);\n";
    return;
  }
  if (toUpper(text) == "FLUSH REACTIVE" || toUpper(text) == "FLUSH THE REACTIVE GRAPH") {
    bc.usesRx = true;
    o << "  luke_rx_flush(_luke_rx);\n";
    return;
  }
  if (toUpper(text) == "AUDIT REACTIVE" || toUpper(text) == "AUDIT THE REACTIVE GRAPH") {
    bc.usesRx = true;
    o << "  luke_rx_audit_graph(_luke_rx);\n";
    return;
  }
  if (toUpper(text) == "DUMP REACTIVE GRAPH" || toUpper(text) == "DUMP THE REACTIVE GRAPH") {
    bc.usesRx = true;
    o << "  luke_rx_dump_graph(_luke_rx);\n";
    return;
  }
  if (startsWithCI(text, "TRACE WHY ") || startsWithCI(text, "WHY DID ")) {
    std::string nameRaw;
    if (startsWithCI(text, "TRACE WHY "))
      nameRaw = trim(text.substr(10));
    else {
      auto rest = trim(text.substr(8));
      auto U = toUpper(rest);
      auto ch = U.find(" CHANGE");
      nameRaw = ch != std::string::npos ? trim(rest.substr(0, ch)) : rest;
    }
    auto name = resolveRxCellName(bc.rxCells, bc.rxEntityStack, stripThe(nameRaw));
    if (!bc.rxCells.count(name)) {
      bc.fail(line, "TRACE WHY needs a reactive cell — not '" + nameRaw + "'");
      return;
    }
    bc.usesRx = true;
    o << "  luke_rx_trace_why(_luke_rx, _luke_rx_id_" << cIdent(name) << ");\n";
    return;
  }
  if (toUpper(text) == "RETRY REACTIVE ERROR" || toUpper(text) == "RETRY THE REACTIVE ERROR") {
    bc.usesRx = true;
    o << "  luke_rx_retry_error(_luke_rx);\n";
    return;
  }
  if (toUpper(text) == "CLEAR REACTIVE ERROR" || toUpper(text) == "CLEAR THE REACTIVE ERROR") {
    bc.usesRx = true;
    o << "  luke_rx_clear_reactive_error(_luke_rx);\n";
    return;
  }
  if (startsWithCI(text, "REPORT REACTIVE FAILURE FOR ") ||
      startsWithCI(text, "REPORT ASYNC FAILURE FOR ")) {
    size_t prefix =
        startsWithCI(text, "REPORT REACTIVE FAILURE FOR ") ? 28 : 26;
    auto rest = trim(text.substr(prefix));
    auto U = toUpper(rest);
    auto withPos = U.find(" WITH ");
    if (withPos == std::string::npos) {
      bc.fail(line, "REPORT REACTIVE FAILURE needs: … FOR cell WITH message");
      return;
    }
    auto cellRaw = trim(rest.substr(0, withPos));
    auto msgE = bc.coerceText(bc.expr(trim(rest.substr(withPos + 6)), line));
    auto cellName = resolveRxCellName(bc.rxCells, bc.rxEntityStack, stripThe(cellRaw));
    if (!bc.rxCells.count(cellName)) {
      bc.fail(line, "REPORT REACTIVE FAILURE needs a reactive cell — not '" + cellRaw + "'");
      return;
    }
    bc.usesRx = true;
    o << "  luke_rx_report_async_failure(_luke_rx, _luke_rx_id_" << cIdent(cellName) << ", "
      << msgE.code << ");\n";
    return;
  }
  if (toUpper(text) == "PAINT DIRTY" || toUpper(text) == "PAINT THE DIRTY NODES") {
    o << "  argus_paint(arena);\n";
    return;
  }
  if (startsWithCI(text, "REACTIVE WATCH REGISTER ")) {
    auto seq = std::atoi(trim(text.substr(24)).c_str());
    const BC::RxWhenDef *wd = nullptr;
    for (auto &w : bc.rxWhenDefs)
      if (w.seq == seq) {
        wd = &w;
        break;
      }
    if (!wd) {
      bc.fail(line, "Internal: unknown reactive watch #" + std::to_string(seq));
      return;
    }
    bc.usesRx = true;
    if (wd->weak)
      o << "  _luke_rx_id_when_" << seq << " = luke_rx_effect_weak(_luke_rx, _luke_rx_when_" << seq
        << ", NULL, "
        << (wd->background ? "LUKE_RX_PRIO_BACKGROUND" : "LUKE_RX_PRIO_UI") << ");\n";
    else
      o << "  _luke_rx_id_when_" << seq << " = luke_rx_effect_prio(_luke_rx, _luke_rx_when_" << seq
        << ", NULL, "
        << (wd->background ? "LUKE_RX_PRIO_BACKGROUND" : "LUKE_RX_PRIO_UI") << ");\n";
    return;
  }
  /* BIND BACKGROUND "tag" TO expr — low-priority reactive effect (Scheduler 2.0). */
  if (startsWithCI(text, "BIND BACKGROUND ")) {
    auto rest = trim(text.substr(16));
    auto U = toUpper(rest);
    auto toPos = U.find(" TO ");
    if (toPos == std::string::npos) {
      bc.fail(line, "BIND BACKGROUND needs: BIND BACKGROUND \"element\" TO expression");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, toPos)), line));
    auto exprRaw = trim(rest.substr(toPos + 4));
    if (exprRaw.empty()) {
      bc.fail(line, "BIND BACKGROUND needs an expression after TO");
      return;
    }
    bc.usesRx = true;
    int seq = ++bc.rxBindSeq;
    bc.rxBindDefs.push_back({idE.code, exprRaw, line, seq});
    o << "  _luke_rx_id_bind_" << seq << " = luke_rx_effect_prio(_luke_rx, _luke_rx_bind_" << seq
      << ", NULL, LUKE_RX_PRIO_BACKGROUND);\n";
    o << "  luke_rx_flush(_luke_rx);\n";
    return;
  }
  /* BIND OPACITY "panel" TO progress — reactive Argus opacity (layout frame). */
  if (startsWithCI(text, "BIND OPACITY ")) {
    auto rest = trim(text.substr(13));
    auto U = toUpper(rest);
    auto toPos = U.find(" TO ");
    if (toPos == std::string::npos) {
      bc.fail(line, "BIND OPACITY needs: BIND OPACITY \"element\" TO expression");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, toPos)), line));
    auto exprRaw = trim(rest.substr(toPos + 4));
    if (exprRaw.empty()) {
      bc.fail(line, "BIND OPACITY needs an expression after TO");
      return;
    }
    bc.usesRx = true;
    bc.usesRxUi = true;
    int seq = ++bc.rxBindSeq;
    bc.rxOpacityBindDefs.push_back({idE.code, exprRaw, line, seq});
    o << "  _luke_rx_id_bind_" << seq << " = luke_rx_effect(_luke_rx, _luke_rx_bind_opacity_" << seq
      << ", NULL);\n";
    o << "  luke_rx_flush(_luke_rx);\n";
    return;
  }
  /* BIND LIST players AS "row" — granular Argus row paints (prefix_index). */
  if (startsWithCI(text, "BIND LIST ")) {
    auto rest = trim(text.substr(10));
    auto U = toUpper(rest);
    auto asPos = U.find(" AS ");
    if (asPos == std::string::npos) {
      bc.fail(line, "BIND LIST needs: BIND LIST name AS \"prefix\"");
      return;
    }
    auto listName = stripThe(trim(rest.substr(0, asPos)));
    auto prefixRaw = trim(rest.substr(asPos + 4));
    auto prefixE = bc.coerceText(bc.expr(prefixRaw, line));
    (void)prefixE;
    if (!bc.rxCells.count(listName) ||
        (bc.rxCellTy.count(listName) && bc.rxCellTy[listName].k != K::List)) {
      bc.fail(line, "BIND LIST needs a REMEMBER'd LIST — not '" + listName + "'");
      return;
    }
    bc.usesRx = true;
    bc.usesRxUi = true;
    int seq = ++bc.rxBindSeq;
    bc.rxListBindDefs.push_back({listName, prefixRaw, line, seq});
    o << "  _luke_rx_id_bind_" << seq << " = luke_rx_effect(_luke_rx, _luke_rx_bind_list_" << seq
      << ", NULL);\n";
    o << "  luke_rx_flush(_luke_rx);\n";
    return;
  }
  /* BIND "greeting" TO "Welcome, " AND username — reactive Argus text (no CLEAR). */
  if (startsWithCI(text, "BIND ")) {
    auto rest = trim(text.substr(5));
    auto U = toUpper(rest);
    auto toPos = U.find(" TO ");
    if (toPos == std::string::npos) {
      bc.fail(line, "BIND needs: BIND \"element\" TO expression");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, toPos)), line));
    auto exprRaw = trim(rest.substr(toPos + 4));
    if (exprRaw.empty()) {
      bc.fail(line, "BIND needs an expression after TO");
      return;
    }
    bc.usesRx = true;
    bc.usesRxUi = true;
    int seq = ++bc.rxBindSeq;
    std::string argusId;
    /* Best-effort extract literal id for metadata; runtime uses expr. */
    if (!idE.code.empty()) {
      /* keep seq-based symbol; store raw id expr in def */
    }
    (void)argusId;
    bc.rxBindDefs.push_back({idE.code, exprRaw, line, seq});
    o << "  _luke_rx_id_bind_" << seq << " = luke_rx_effect(_luke_rx, _luke_rx_bind_" << seq
      << ", NULL);\n";
    o << "  luke_rx_flush(_luke_rx);\n";
    return;
  }
  /* UPDATE "greeting" WITH expr — one-shot Argus text write + dirty paint. */
  if (startsWithCI(text, "UPDATE ")) {
    auto rest = trim(text.substr(7));
    auto U = toUpper(rest);
    auto withPos = U.find(" WITH ");
    if (withPos == std::string::npos) {
      bc.fail(line, "UPDATE needs: UPDATE \"element\" WITH expression");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, withPos)), line));
    auto t = bc.coerceText(bc.expr(trim(rest.substr(withPos + 6)), line));
    bc.usesRx = true;
    bc.usesRxUi = true;
    o << "  luke_rx_ui_set_text(_luke_rx, " << idE.code << ", " << t.code << ");\n";
    o << "  if (_luke_rx) { _luke_rx->need_paint = 1; luke_rx_ui_after_flush(_luke_rx); }\n";
    return;
  }
  /* Phase 14 — error boundaries (component-scoped containment) */
  if (startsWithCI(text, "BEGIN ERROR BOUNDARY ") || startsWithCI(text, "ERROR BOUNDARY ")) {
    std::string rest = startsWithCI(text, "BEGIN ERROR BOUNDARY ") ? trim(text.substr(21))
                                                                   : trim(text.substr(15));
    stripDo(rest);
    auto name = stripThe(rest);
    if (name.empty()) {
      bc.fail(line, "ERROR BOUNDARY needs a name — BEGIN ERROR BOUNDARY Panel");
      return;
    }
    bool simple = true;
    for (char c : name)
      if (!(isalnum((unsigned char)c) || c == '_')) simple = false;
    if (!simple) {
      bc.fail(line, "ERROR BOUNDARY name must be a simple identifier");
      return;
    }
    bc.usesRx = true;
    bc.rxBoundaryStack.push_back(name);
    o << "  luke_rx_boundary_begin(_luke_rx, \"" << esc(name) << "\");\n";
    return;
  }
  if (startsWithCI(text, "END ERROR BOUNDARY") || toUpper(text) == "ENDERROR BOUNDARY") {
    std::string rest;
    if (startsWithCI(text, "END ERROR BOUNDARY"))
      rest = trim(text.substr(19));
    else
      rest = "";
    if (bc.rxBoundaryStack.empty()) {
      bc.fail(line, "END ERROR BOUNDARY without matching BEGIN ERROR BOUNDARY");
      return;
    }
    std::string open = bc.rxBoundaryStack.back();
    if (!rest.empty() && stripThe(rest) != open) {
      bc.fail(line, "END ERROR BOUNDARY '" + stripThe(rest) + "' but open boundary is '" + open +
                         "'");
      return;
    }
    bc.rxBoundaryStack.pop_back();
    bc.usesRx = true;
    o << "  luke_rx_boundary_end(_luke_rx, \"" << esc(open) << "\");\n";
    return;
  }
  if (startsWithCI(text, "RESET ERROR BOUNDARY ") || startsWithCI(text, "CLEAR ERROR BOUNDARY ")) {
    size_t prefix = startsWithCI(text, "RESET ERROR BOUNDARY ") ? 21 : 21;
    auto name = stripThe(trim(text.substr(prefix)));
    if (name.empty()) {
      bc.fail(line, "RESET ERROR BOUNDARY needs a name");
      return;
    }
    bc.usesRx = true;
    o << "  luke_rx_boundary_reset(_luke_rx, \"" << esc(name) << "\");\n";
    return;
  }
  /* Phase 3/7 — component / entity scopes */
  if (startsWithCI(text, "BEGIN ENTITY ") || startsWithCI(text, "ENTITY ")) {
    std::string rest = startsWithCI(text, "BEGIN ENTITY ") ? trim(text.substr(13))
                                                         : trim(text.substr(7));
    stripDo(rest);
    auto name = stripThe(rest);
    if (name.empty()) {
      bc.fail(line, "ENTITY needs a name — BEGIN ENTITY Player");
      return;
    }
    bool simple = true;
    for (char c : name)
      if (!(isalnum((unsigned char)c) || c == '_')) simple = false;
    if (!simple) {
      bc.fail(line, "ENTITY name must be a simple identifier");
      return;
    }
    bc.usesRx = true;
    bc.rxEntityStack.push_back(name);
    bc.rxComponentStack.push_back(name);
    o << "  luke_rx_scope_begin(_luke_rx, \"" << esc(name) << "\");\n";
    return;
  }
  if (startsWithCI(text, "BEGIN COMPONENT ") || startsWithCI(text, "COMPONENT ")) {
    std::string rest = startsWithCI(text, "BEGIN COMPONENT ") ? trim(text.substr(16))
                                                              : trim(text.substr(10));
    stripDo(rest);
    auto name = stripThe(rest);
    if (name.empty()) {
      bc.fail(line, "COMPONENT needs a name — BEGIN COMPONENT Counter");
      return;
    }
    bool simple = true;
    for (char c : name)
      if (!(isalnum((unsigned char)c) || c == '_')) simple = false;
    if (!simple) {
      bc.fail(line, "COMPONENT name must be a simple identifier");
      return;
    }
    bc.usesRx = true;
    bc.rxComponentStack.push_back(name);
    o << "  luke_rx_scope_begin(_luke_rx, \"" << esc(name) << "\");\n";
    return;
  }
  if (startsWithCI(text, "END COMPONENT") || startsWithCI(text, "END ENTITY") ||
      toUpper(text) == "ENDCOMPONENT" || toUpper(text) == "ENDENTITY") {
    std::string rest;
    if (startsWithCI(text, "END COMPONENT"))
      rest = trim(text.substr(13));
    else if (startsWithCI(text, "END ENTITY"))
      rest = trim(text.substr(10));
    else
      rest = "";
    if (bc.rxComponentStack.empty()) {
      bc.fail(line, "END COMPONENT without matching BEGIN COMPONENT / ENTITY");
      return;
    }
    std::string open = bc.rxComponentStack.back();
    if (!rest.empty() && stripThe(rest) != open) {
      bc.fail(line, "END COMPONENT '" + stripThe(rest) + "' but open scope is '" + open + "'");
      return;
    }
    if (!bc.rxEntityStack.empty() && bc.rxEntityStack.back() == open) bc.rxEntityStack.pop_back();
    bc.rxComponentStack.pop_back();
    bc.usesRx = true;
    o << "  luke_rx_scope_pause(_luke_rx);\n";
    /* Scope stays alive until DESTROY COMPONENT — handlers can still touch cells. */
    return;
  }
  if (startsWithCI(text, "DESTROY COMPONENT ") || startsWithCI(text, "DESTROY ENTITY ") ||
      startsWithCI(text, "UNMOUNT COMPONENT ") || startsWithCI(text, "UNMOUNT ENTITY ") ||
      (startsWithCI(text, "DESTROY ") && !startsWithCI(text, "DESTROY COMPONENT ") &&
       !startsWithCI(text, "DESTROY ENTITY "))) {
    std::string rest;
    if (startsWithCI(text, "DESTROY COMPONENT "))
      rest = trim(text.substr(18));
    else if (startsWithCI(text, "DESTROY ENTITY "))
      rest = trim(text.substr(15));
    else if (startsWithCI(text, "UNMOUNT COMPONENT "))
      rest = trim(text.substr(18));
    else if (startsWithCI(text, "UNMOUNT ENTITY "))
      rest = trim(text.substr(15));
    else
      rest = trim(text.substr(8));
    auto name = stripThe(rest);
    if (name.empty()) {
      bc.fail(line, "DESTROY COMPONENT needs a name");
      return;
    }
    bc.usesRx = true;
    o << "  luke_rx_scope_end(_luke_rx, \"" << esc(name) << "\");\n";
    return;
  }
  /* Derived: THE total IS price MULTIPLIED BY quantity */
  if (startsWithCI(text, "REFRESH QUERY ")) {
    auto qname = resolveRxCellName(bc.rxCells, bc.rxEntityStack, trim(text.substr(14)));
    const BC::RxQueryDef *qd = nullptr;
    for (auto &q : bc.rxQueryDefs)
      if (q.name == qname) {
        qd = &q;
        break;
      }
    if (!qd) {
      bc.fail(line, "REFRESH QUERY needs a REMEMBER'd QUERY cell — not '" + qname + "'");
      return;
    }
    bc.usesRx = true;
    o << "  luke_rx_query_refresh(_luke_rx, _luke_rx_id_" << cIdent(qname) << ", "
      << cIdent(qd->dbLocal) << ", luke_text(\"" << esc(qd->sql) << "\"));\n";
    return;
  }
  if (startsWithCI(text, "START TIMELINE ") || startsWithCI(text, "RUN TIMELINE ")) {
    size_t prefix = startsWithCI(text, "START TIMELINE ") ? 15 : 13;
    auto rest = trim(text.substr(prefix));
    auto U = toUpper(rest);
    /* Parse: "id" FOR ms MILLISECONDS FROM a TO b INTO cell */
    auto forPos = findOutsideQuotes(rest, U, " FOR ");
    auto fromPos = findOutsideQuotes(rest, U, " FROM ");
    auto toKw = findOutsideQuotes(rest, U, " TO ");
    auto intoPos = findOutsideQuotes(rest, U, " INTO ");
    if (forPos == std::string::npos || fromPos == std::string::npos || toKw == std::string::npos ||
        intoPos == std::string::npos || intoPos < toKw) {
      bc.fail(line, "TIMELINE needs: START TIMELINE \"id\" FOR ms MILLISECONDS FROM a TO b INTO cell");
      return;
    }
    auto idRaw = trim(rest.substr(0, forPos));
    auto idLit = unquoteText(idRaw);
    auto forClause = trim(rest.substr(forPos + 5, fromPos - (forPos + 5)));
    auto fU = toUpper(forClause);
    auto msPos = fU.find(" MILLISECONDS");
    if (msPos != std::string::npos) forClause = trim(forClause.substr(0, msPos));
    auto msE = bc.expr(forClause, line);
    auto fromE = bc.expr(trim(rest.substr(fromPos + 6, toKw - (fromPos + 6))), line);
    auto toE = bc.expr(trim(rest.substr(toKw + 4, intoPos - (toKw + 4))), line);
    auto intoName = resolveRxCellName(bc.rxCells, bc.rxEntityStack, trim(rest.substr(intoPos + 6)));
    if (!bc.rxCells.count(intoName)) {
      bc.fail(line, "TIMELINE INTO needs a REMEMBER'd NUMBER cell — '" + intoName + "'");
      return;
    }
    if (idLit.empty()) idLit = idRaw;
    bc.usesRx = true;
    bc.usesTimeline = true;
    bc.rxTimelineBinds.push_back({idLit, intoName, 0, 1, 0, line});
    o << "  _luke_active_timeline_id = luke_text(\"" << esc(idLit) << "\");\n";
    o << "  luke_rx_timeline_register(_luke_rx, _luke_active_timeline_id, _luke_rx_id_"
      << cIdent(intoName) << ", " << fromE.code << ", " << toE.code << ");\n";
    if (bc.forBrowser) {
      o << "  luke_js_timeline_start(luke_text(\"" << esc(idLit) << "\"), " << msE.code << ");\n";
    } else {
      o << "  luke_rx_timeline_run_sync(_luke_rx, luke_text(\"" << esc(idLit) << "\"), "
        << fromE.code << ", " << toE.code << ", _luke_rx_id_" << cIdent(intoName) << ", 10);\n";
    }
    return;
  }
  if (startsWithCI(text, "THE ") && !startsWithCI(text, "THE VALUE OF ") &&
      !startsWithCI(text, "THE WEAK VALUE OF ") &&
      !startsWithCI(text, "THE NODE ID OF ") && !startsWithCI(text, "THE DEP COUNT OF ") &&
      !startsWithCI(text, "THE SUB COUNT OF ") && !startsWithCI(text, "THE WHY ROOT OF ") &&
      !startsWithCI(text, "THE WHY DEPTH OF ") &&
      !startsWithCI(text, "THE TIMELINE STEP ID AT ") &&
      !startsWithCI(text, "THE TIMELINE STEP WAVE AT ") &&
      !startsWithCI(text, "THE BODY OF ") && !startsWithCI(text, "THE STATUS OF ")) {
    auto rest = trim(text.substr(4));
    auto U = toUpper(rest);
    auto isPos = U.find(" IS ");
    if (isPos != std::string::npos) {
      auto name = stripThe(trim(rest.substr(0, isPos)));
      auto exprRaw = trim(rest.substr(isPos + 4));
      bool simple = !name.empty();
      for (char c : name)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) simple = false;
      if (simple && !exprRaw.empty()) {
        std::string dkey =
            name.find('.') != std::string::npos ? name : rxScopedCellName(bc.rxEntityStack, name);
        bc.usesRx = true;
        bc.locals[name] = Ty::num();
        if (!bc.rxCells.count(dkey)) bc.rxCellOrder.push_back(dkey);
        bc.rxCells[dkey] = true;
        bc.rxDerived[dkey] = true;
        int found = 0;
        for (auto &d : bc.rxDerivedDefs)
          if (d.name == dkey) {
            d.exprRaw = exprRaw;
            d.line = line;
            found = 1;
          }
        if (!found) bc.rxDerivedDefs.push_back({dkey, exprRaw, line});
        o << "  _luke_rx_id_" << cIdent(dkey) << " = luke_rx_derived(_luke_rx, _luke_rx_fn_"
          << cIdent(dkey) << ", NULL);\n";
        return;
      }
    }
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
    /* SET ITEM n OF list TO v — reactive or plain list slot write */
    if (startsWithCI(rest, "ITEM ")) {
      auto itemRest = trim(rest.substr(5));
      auto iU = toUpper(itemRest);
      auto ofPos = iU.find(" OF ");
      auto toPos = iU.find(" TO ");
      if (ofPos != std::string::npos && toPos != std::string::npos && toPos > ofPos) {
        auto idxE = bc.expr(trim(itemRest.substr(0, ofPos)), line);
        auto listName = stripThe(trim(itemRest.substr(ofPos + 4, toPos - (ofPos + 4))));
        auto valE = bc.coerceText(bc.expr(trim(itemRest.substr(toPos + 4)), line));
        bc.expectTy(line, idxE.ty, Ty::num(), "SET ITEM … OF");
        if (bc.rxCells.count(listName) && bc.rxCellTy.count(listName) &&
            bc.rxCellTy[listName].k == K::List) {
          bc.usesRx = true;
          o << "  luke_rx_list_set(_luke_rx, _luke_rx_id_" << cIdent(listName) << ", " << idxE.code
            << ", " << valE.code << ");\n";
          return;
        }
        if (!bc.locals.count(listName) || bc.locals[listName].k != K::List) {
          bc.fail(line, "SET ITEM … OF needs a LIST — not '" + listName + "'");
          return;
        }
        o << "  luke_list_set(" << cIdent(listName) << ", " << idxE.code << ", " << valE.code
          << ");\n";
        return;
      }
    }
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
  if (startsWithCI(text, "FOR EACH ")) {
    auto rest = trim(text.substr(9));
    stripDo(rest);
    auto U = toUpper(rest);
    auto inPos = findOutsideQuotes(rest, U, " IN ");
    if (inPos == std::string::npos) {
      bc.fail(line, "FOR EACH needs: FOR EACH item IN list DO");
      return;
    }
    auto varName = trim(rest.substr(0, inPos));
    auto listE = bc.expr(trim(rest.substr(inPos + 4)), line);
    bc.expectTy(line, listE.ty, Ty::list(), "FOR EACH … IN");
    if (varName.empty()) {
      bc.fail(line, "FOR EACH needs an item name");
      return;
    }
    int id = ++bc.forEachSeq;
    o << "  {\n";
    o << "    double _luke_fe_n" << id << " = luke_list_len(" << listE.code << ");\n";
    o << "    for (double _luke_fe_i" << id << " = 0; _luke_fe_i" << id << " < _luke_fe_n" << id
      << "; _luke_fe_i" << id << " += 1) {\n";
    o << "      LukeText " << cIdent(varName) << " = luke_list_get(" << listE.code << ", _luke_fe_i"
      << id << ");\n";
    bc.locals[varName] = Ty::text();
    bc.forEachVars.push_back(varName);
    return;
  }
  if (toUpper(text) == "END FOR" || toUpper(text) == "ENDFOR" || toUpper(text) == "END FOR EACH") {
    if (bc.forEachVars.empty()) {
      bc.fail(line, "END FOR without matching FOR EACH");
      return;
    }
    bc.locals.erase(bc.forEachVars.back());
    bc.forEachVars.pop_back();
    o << "    }\n  }\n";
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
      auto listName = stripThe(trim(rest.substr(to + 4)));
      auto v = bc.coerceText(val);
      if (bc.rxCells.count(listName) && bc.rxCellTy.count(listName) &&
          bc.rxCellTy[listName].k == K::List) {
        bc.usesRx = true;
        o << "  luke_rx_list_add(_luke_rx, _luke_rx_id_" << cIdent(listName) << ", " << v.code
          << ");\n";
        return;
      }
      if (!bc.locals.count(listName) || bc.locals[listName].k != K::List) {
        bc.fail(line, "ADD … TO needs a LIST — declare MY NAME IS " + listName +
                          " AS LIST (or REMEMBER " + listName + " AS LIST)");
        return;
      }
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
      auto mapName = stripThe(trim(rest.substr(in + 4)));
      auto k = bc.coerceText(key);
      auto v = bc.coerceText(val);
      if (bc.rxCells.count(mapName) && bc.rxCellTy.count(mapName) &&
          bc.rxCellTy[mapName].k == K::Map) {
        bc.usesRx = true;
        o << "  luke_rx_map_put(_luke_rx, _luke_rx_id_" << cIdent(mapName) << ", " << k.code
          << ", " << v.code << ");\n";
        return;
      }
      if (!bc.locals.count(mapName) || bc.locals[mapName].k != K::Map) {
        bc.fail(line, "PUT … IN needs a MAP — declare MY NAME IS " + mapName +
                          " AS MAP (or REMEMBER " + mapName + " AS MAP)");
        return;
      }
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

  /* Browser page ownership — conversational */
  if (startsWithCI(text, "BRING FONT FROM ")) {
    auto raw = trim(text.substr(16));
    auto href = bc.coerceText(bc.expr(raw, line));
    BrowserFont f;
    f.family = "LukeFont";
    f.hrefOrPath = unquoteText(raw);
    f.local = !(startsWithCI(f.hrefOrPath, "http://") || startsWithCI(f.hrefOrPath, "https://"));
    if (f.local) {
      auto slash = f.hrefOrPath.find_last_of("/\\");
      auto base = slash == std::string::npos ? f.hrefOrPath : f.hrefOrPath.substr(slash + 1);
      f.outRelPath = "fonts/" + base;
    }
    bc.pageFonts.push_back(f);
    bc.hasPage = true;
    if (f.local) {
      std::ostringstream face;
      face << "@font-face{font-family:\"" << esc(f.family) << "\";src:url(\"" << esc(f.outRelPath)
           << "\") format(\"woff2\");font-display:swap;}";
      o << "  luke_js_add_style(luke_text(\"" << esc(face.str()) << "\"));\n";
    } else {
      o << "  luke_js_load_font(" << href.code << ");\n";
    }
    return;
  }
  if (startsWithCI(text, "BRING FONT ")) {
    auto rest = trim(text.substr(11));
    auto fr = findOutsideQuotes(rest, toUpper(rest), " FROM ");
    if (fr != std::string::npos) {
      auto famRaw = trim(rest.substr(0, fr));
      auto pathRaw = trim(rest.substr(fr + 6));
      auto href = bc.coerceText(bc.expr(pathRaw, line));
      BrowserFont f;
      f.family = unquoteText(famRaw);
      f.hrefOrPath = unquoteText(pathRaw);
      f.local = !(startsWithCI(f.hrefOrPath, "http://") || startsWithCI(f.hrefOrPath, "https://"));
      bc.pageFonts.push_back(f);
      bc.hasPage = true;
      if (f.local) {
        auto slash = f.hrefOrPath.find_last_of("/\\");
        auto base =
            slash == std::string::npos ? f.hrefOrPath : f.hrefOrPath.substr(slash + 1);
        bc.pageFonts.back().outRelPath = "fonts/" + base;
        std::ostringstream face;
        face << "@font-face{font-family:\"" << esc(f.family) << "\";src:url(\""
             << esc(bc.pageFonts.back().outRelPath)
             << "\") format(\"woff2\");font-display:swap;}";
        o << "  luke_js_add_style(luke_text(\"" << esc(face.str()) << "\"));\n";
      } else {
        o << "  luke_js_load_font(" << href.code << ");\n";
      }
      return;
    }
  }
  if (startsWithCI(text, "WEAR STYLE ")) {
    auto raw = trim(text.substr(11));
    auto css = bc.coerceText(bc.expr(raw, line));
    auto cssText = unquoteText(raw);
    if (!cssText.empty()) {
      if (!bc.pageStyle.empty()) bc.pageStyle += "\n";
      bc.pageStyle += cssText;
      bc.hasPage = true;
    }
    o << "  luke_js_add_style(" << css.code << ");\n";
    return;
  }
  if (startsWithCI(text, "NAME THE PAGE ")) {
    auto raw = trim(text.substr(14));
    auto title = bc.coerceText(bc.expr(raw, line));
    bc.pageTitle = unquoteText(raw);
    bc.hasPage = true;
    o << "  luke_js_set_title(" << title.code << ");\n";
    return;
  }
  if (startsWithCI(text, "FILL ") && findOutsideQuotes(text, toUpper(text), " WITH ") != std::string::npos) {
    auto rest = trim(text.substr(5));
    auto w = findOutsideQuotes(rest, toUpper(rest), " WITH ");
    auto idRaw = trim(rest.substr(0, w));
    auto htmlRaw = trim(rest.substr(w + 6));
    auto id = bc.coerceText(bc.expr(idRaw, line));
    auto html = bc.coerceText(bc.expr(htmlRaw, line));
    if (unquoteText(idRaw) == "root") {
      bc.pageBody = unquoteText(htmlRaw);
      bc.hasPage = true;
    }
    o << "  luke_js_set_html(" << id.code << ", " << html.code << ");\n";
    return;
  }

  /* Production web — hash routing + async fetch */
  if (startsWithCI(text, "GO TO ")) {
    auto path = bc.coerceText(bc.expr(trim(text.substr(6)), line));
    o << "  luke_js_route_go(" << path.code << ");\n";
    return;
  }
  if (startsWithCI(text, "START FETCH ")) {
    auto rest = trim(text.substr(12));
    auto U = toUpper(rest);
    /* START FETCH "job" GET "url" [INTO body] [STATUS st] [READY ready]
     * START FETCH "job" POST "url" [WITH body] [INTO …] [STATUS …] [READY …] */
    auto getPos = findOutsideQuotes(rest, U, " GET ");
    auto postPos = findOutsideQuotes(rest, U, " POST ");
    bool isPost = postPos != std::string::npos && (getPos == std::string::npos || postPos < getPos);
    size_t methPos = isPost ? postPos : getPos;
    if (methPos == std::string::npos) {
      bc.fail(line, "START FETCH needs GET or POST — START FETCH \"job\" GET \"url\"");
      return;
    }
    auto idRaw = trim(rest.substr(0, methPos));
    auto idE = bc.coerceText(bc.expr(idRaw, line));
    auto after = trim(rest.substr(methPos + (isPost ? 6 : 5)));
    auto aU = toUpper(after);
    size_t withPos = isPost ? findOutsideQuotes(after, aU, " WITH ") : std::string::npos;
    size_t intoPos = findOutsideQuotes(after, aU, " INTO ");
    size_t statusPos = findOutsideQuotes(after, aU, " STATUS ");
    size_t readyPos = findOutsideQuotes(after, aU, " READY ");

    size_t firstClause = std::string::npos;
    auto consider = [&](size_t p) {
      if (p != std::string::npos && (firstClause == std::string::npos || p < firstClause))
        firstClause = p;
    };
    consider(withPos);
    consider(intoPos);
    consider(statusPos);
    consider(readyPos);

    auto urlPart = firstClause == std::string::npos ? after : trim(after.substr(0, firstClause));
    auto urlE = bc.coerceText(bc.expr(urlPart, line));
    Expr bodyE{"luke_text(\"\")", Ty::text()};
    if (withPos != std::string::npos) {
      size_t bEnd = after.size();
      auto limit = [&](size_t p) {
        if (p != std::string::npos && p > withPos && p < bEnd) bEnd = p;
      };
      limit(intoPos);
      limit(statusPos);
      limit(readyPos);
      bodyE = bc.coerceText(bc.expr(trim(after.substr(withPos + 6, bEnd - (withPos + 6))), line));
    }

    auto parseCellRef = [&](size_t pos, size_t keyLen, const char *clause,
                            bool wantText) -> std::string {
      if (pos == std::string::npos) return {};
      size_t start = pos + keyLen;
      size_t end = after.size();
      auto limit = [&](size_t p) {
        if (p != std::string::npos && p > pos && p < end) end = p;
      };
      limit(intoPos);
      limit(statusPos);
      limit(readyPos);
      limit(withPos);
      auto name = stripThe(trim(after.substr(start, end - start)));
      if (name.empty()) {
        bc.fail(line, std::string("START FETCH ") + clause + " needs a cell name");
        return {};
      }
      if (!bc.rxCells.count(name)) {
        bc.fail(line, std::string("START FETCH ") + clause + " '" + name +
                          "' — REMEMBER it first as a reactive cell");
        return {};
      }
      Ty ty = bc.rxCellTy.count(name) ? bc.rxCellTy[name] : Ty::num();
      if (wantText)
        bc.expectTy(line, ty, Ty::text(), std::string("START FETCH ") + clause);
      else if (ty.k != K::Num && ty.k != K::Flag)
        bc.fail(line, std::string("START FETCH ") + clause + " wants NUMBER/FLAG cell");
      return name;
    };

    std::string intoCell = parseCellRef(intoPos, 6, "INTO", true);
    if (bc.bad) return;
    std::string statusCell = parseCellRef(statusPos, 8, "STATUS", false);
    if (bc.bad) return;
    std::string readyCell = parseCellRef(readyPos, 7, "READY", false);
    if (bc.bad) return;

    if (!intoCell.empty() || !statusCell.empty() || !readyCell.empty()) {
      std::string jobLit = unquoteText(idRaw);
      if (jobLit.empty()) jobLit = idRaw;
      bc.rxFetchBinds.push_back({jobLit, intoCell, statusCell, readyCell, line});
      bc.usesRx = true;
    }

    o << "  luke_js_fetch_start(" << idE.code << ", luke_text(\"" << (isPost ? "POST" : "GET")
      << "\"), " << urlE.code << ", " << bodyE.code << ");\n";
    if (!readyCell.empty())
      o << "  luke_rx_write_num(_luke_rx, _luke_rx_id_" << cIdent(readyCell) << ", 0);\n";
    return;
  }

  /* Hanka — layout boxes → Argus frames */
  if (toUpper(text) == "LAY OUT THE SCREEN" || toUpper(text) == "LAYOUT THE SCREEN" ||
      toUpper(text) == "LAY OUT" || toUpper(text) == "LAYOUT") {
    if (bc.usesRxUi) o << "  hanka_set_keep_roots(arena, 1);\n";
    o << "  hanka_layout(arena);\n";
    return;
  }
  if (startsWithCI(text, "BEGIN COLUMN") || startsWithCI(text, "BEGIN ROW") ||
      startsWithCI(text, "BEGIN STACK")) {
    std::string axis =
        startsWithCI(text, "BEGIN COLUMN") ? "COLUMN" : startsWithCI(text, "BEGIN ROW") ? "ROW" : "STACK";
    /* "BEGIN COLUMN"=12, "BEGIN ROW"=9, "BEGIN STACK"=11 */
    auto rest = trim(text.substr(axis == "COLUMN" ? 12 : axis == "ROW" ? 9 : 11));
    stripDo(rest);
    auto U = toUpper(rest);
    size_t atPos = std::string::npos;
    size_t atLen = 4;
    if (startsWithCI(rest, "AT ")) {
      atPos = 0;
      atLen = 3;
    } else {
      atPos = findOutsideQuotes(rest, U, " AT ");
    }
    auto sizePos = findOutsideQuotes(rest, U, " SIZE ");
    if (atPos == std::string::npos || sizePos == std::string::npos || atPos > sizePos) {
      bc.fail(line, "BEGIN " + axis + " needs AT x, y SIZE w, h [PAD n] [GAP n] [ALIGN START|CENTER|END] [WRAP]");
      return;
    }
    auto atPart = trim(rest.substr(atPos + atLen, sizePos - (atPos + atLen)));
    auto afterSize = trim(rest.substr(sizePos + 6));
    auto padPos = findOutsideQuotes(afterSize, toUpper(afterSize), " PAD ");
    auto gapPos = findOutsideQuotes(afterSize, toUpper(afterSize), " GAP ");
    auto alignPos = findOutsideQuotes(afterSize, toUpper(afterSize), " ALIGN ");
    auto wrapPos = findOutsideQuotes(afterSize, toUpper(afterSize), " WRAP");
    std::string sizePart = afterSize;
    std::string padRaw = "0";
    std::string gapRaw = "0";
    int alignVal = 0; /* start */
    int wrapVal = 0;
    size_t cut = afterSize.size();
    if (padPos != std::string::npos) cut = padPos;
    if (gapPos != std::string::npos && gapPos < cut) cut = gapPos;
    if (alignPos != std::string::npos && alignPos < cut) cut = alignPos;
    if (wrapPos != std::string::npos && wrapPos < cut) cut = wrapPos;
    sizePart = trim(afterSize.substr(0, cut));
    if (wrapPos != std::string::npos) {
      /* WRAP is a flag token; allow trailing keywords after it */
      wrapVal = 1;
    }
    if (padPos != std::string::npos) {
      size_t padEnd = afterSize.size();
      if (gapPos != std::string::npos && gapPos > padPos) padEnd = gapPos;
      if (alignPos != std::string::npos && alignPos > padPos && alignPos < padEnd) padEnd = alignPos;
      if (wrapPos != std::string::npos && wrapPos > padPos && wrapPos < padEnd) padEnd = wrapPos;
      padRaw = trim(afterSize.substr(padPos + 5, padEnd - (padPos + 5)));
    }
    if (gapPos != std::string::npos) {
      size_t gapEnd = afterSize.size();
      if (padPos != std::string::npos && padPos > gapPos) gapEnd = padPos;
      if (alignPos != std::string::npos && alignPos > gapPos && alignPos < gapEnd) gapEnd = alignPos;
      if (wrapPos != std::string::npos && wrapPos > gapPos && wrapPos < gapEnd) gapEnd = wrapPos;
      gapRaw = trim(afterSize.substr(gapPos + 5, gapEnd - (gapPos + 5)));
    }
    if (alignPos != std::string::npos) {
      size_t alignEnd = afterSize.size();
      if (padPos != std::string::npos && padPos > alignPos) alignEnd = padPos;
      if (gapPos != std::string::npos && gapPos > alignPos && gapPos < alignEnd) alignEnd = gapPos;
      if (wrapPos != std::string::npos && wrapPos > alignPos && wrapPos < alignEnd) alignEnd = wrapPos;
      auto alignTok = toUpper(trim(afterSize.substr(alignPos + 7, alignEnd - (alignPos + 7))));
      if (alignTok == "CENTER" || alignTok == "MIDDLE") alignVal = 1;
      else if (alignTok == "END" || alignTok == "RIGHT" || alignTok == "BOTTOM") alignVal = 2;
      else if (alignTok == "START" || alignTok == "LEFT" || alignTok == "TOP" || alignTok.empty())
        alignVal = 0;
      else {
        bc.fail(line, "ALIGN needs START, CENTER, or END — got " + alignTok);
        return;
      }
    }
    auto commaAt = findOutsideQuotes(atPart, toUpper(atPart), ",");
    auto commaSz = findOutsideQuotes(sizePart, toUpper(sizePart), ",");
    if (commaAt == std::string::npos || commaSz == std::string::npos) {
      bc.fail(line, "BEGIN " + axis + " AT needs x, y and SIZE needs w, h");
      return;
    }
    auto x = bc.expr(trim(atPart.substr(0, commaAt)), line);
    auto y = bc.expr(trim(atPart.substr(commaAt + 1)), line);
    auto w = bc.expr(trim(sizePart.substr(0, commaSz)), line);
    auto h = bc.expr(trim(sizePart.substr(commaSz + 1)), line);
    auto pad = bc.expr(padRaw, line);
    auto gap = bc.expr(gapRaw, line);
    bc.expectTy(line, x.ty, Ty::num(), "BEGIN AT x");
    bc.expectTy(line, y.ty, Ty::num(), "BEGIN AT y");
    bc.expectTy(line, w.ty, Ty::num(), "BEGIN SIZE w");
    bc.expectTy(line, h.ty, Ty::num(), "BEGIN SIZE h");
    bc.expectTy(line, pad.ty, Ty::num(), "BEGIN PAD");
    bc.expectTy(line, gap.ty, Ty::num(), "BEGIN GAP");
    const char *fn = axis == "COLUMN"   ? "hanka_begin_column"
                     : axis == "ROW"    ? "hanka_begin_row"
                                        : "hanka_begin_stack";
    o << "  " << fn << "(arena, " << x.code << ", " << y.code << ", " << w.code << ", " << h.code
      << ", " << pad.code << ", " << gap.code << ");\n";
    if (alignVal != 0) o << "  hanka_set_align(arena, " << alignVal << ");\n";
    if (wrapVal != 0) o << "  hanka_set_wrap(arena, 1);\n";
    bc.hankaStack.push_back(axis);
    return;
  }
  if (toUpper(text) == "END COLUMN" || toUpper(text) == "END ROW" || toUpper(text) == "END STACK" ||
      toUpper(text) == "END HANKA") {
    if (bc.hankaStack.empty()) {
      bc.fail(line, "END without matching BEGIN COLUMN|ROW|STACK");
      return;
    }
    std::string want = bc.hankaStack.back();
    std::string got = toUpper(text) == "END HANKA" ? want : trim(toUpper(text).substr(4));
    if (got != want) {
      bc.fail(line, "END " + got + " but open box is " + want);
      return;
    }
    bc.hankaStack.pop_back();
    o << "  hanka_end(arena);\n";
    return;
  }
  if (startsWithCI(text, "SLOT ")) {
    auto rest = trim(text.substr(5));
    auto U = toUpper(rest);
    auto kindEnd = rest.find(' ');
    if (kindEnd == std::string::npos) {
      bc.fail(line, "SLOT needs TEXT|BUTTON|IMAGE|BOX|INPUT|SELECT|TABLE|MODAL \"id\" …");
      return;
    }
    auto kindRaw = toUpper(trim(rest.substr(0, kindEnd)));
    rest = trim(rest.substr(kindEnd));
    U = toUpper(rest);
    auto atPos = findOutsideQuotes(rest, U, " AT ");
    auto sizePos = findOutsideQuotes(rest, U, " SIZE ");
    if (sizePos == std::string::npos) {
      bc.fail(line, "SLOT needs SIZE w, h (use AUTO for measured text width)");
      return;
    }
    std::string idPart;
    bool hasAt = atPos != std::string::npos && atPos < sizePos;
    if (hasAt) {
      idPart = trim(rest.substr(0, atPos));
    } else {
      idPart = trim(rest.substr(0, sizePos));
    }
    int inputType = 0;
    {
      auto idU = toUpper(idPart);
      if (startsWithCI(idPart, "AS ")) {
        /* SLOT INPUT AS CHECKBOX "id" */
        auto afterAs = trim(idPart.substr(3));
        auto sp = afterAs.find(' ');
        if (sp == std::string::npos) {
          bc.fail(line, "INPUT AS needs a type and id — SLOT INPUT AS CHECKBOX \"agree\" …");
          return;
        }
        auto ty = toUpper(trim(afterAs.substr(0, sp)));
        idPart = trim(afterAs.substr(sp));
        if (ty == "PASSWORD") inputType = 1;
        else if (ty == "EMAIL") inputType = 2;
        else if (ty == "TEXT") inputType = 0;
        else if (ty == "CHECKBOX" || ty == "CHECK") inputType = 3;
        else if (ty == "RADIO") inputType = 4;
        else {
          bc.fail(line, "INPUT AS needs TEXT, EMAIL, PASSWORD, CHECKBOX, or RADIO — got " + ty);
          return;
        }
      } else {
        auto asTy = findOutsideQuotes(idPart, idU, " AS ");
        if (asTy != std::string::npos) {
          auto ty = toUpper(trim(idPart.substr(asTy + 4)));
          idPart = trim(idPart.substr(0, asTy));
          if (ty == "PASSWORD") inputType = 1;
          else if (ty == "EMAIL") inputType = 2;
          else if (ty == "TEXT") inputType = 0;
          else if (ty == "CHECKBOX" || ty == "CHECK") inputType = 3;
          else if (ty == "RADIO") inputType = 4;
          else {
            bc.fail(line, "INPUT AS needs TEXT, EMAIL, PASSWORD, CHECKBOX, or RADIO — got " + ty);
            return;
          }
        }
      }
    }
    if (idPart.empty()) {
      bc.fail(line, "SLOT needs an element id after the kind");
      return;
    }
    auto idE = bc.coerceText(bc.expr(idPart, line));
    std::string atPart;
    if (hasAt) atPart = trim(rest.substr(atPos + 4, sizePos - (atPos + 4)));
    auto afterSize = trim(rest.substr(sizePos + 6));
    {
      auto aU = toUpper(afterSize);
      size_t p = findOutsideQuotes(afterSize, aU, " AS PASSWORD");
      if (p != std::string::npos) {
        inputType = 1;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 12)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS EMAIL")) != std::string::npos) {
        inputType = 2;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 9)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS CHECKBOX")) != std::string::npos) {
        inputType = 3;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 12)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS RADIO")) != std::string::npos) {
        inputType = 4;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 9)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS TEXT")) != std::string::npos) {
        inputType = 0;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 8)));
      }
    }
    auto sayPos = findOutsideQuotes(afterSize, toUpper(afterSize), " SAY ");
    auto fromPos = findOutsideQuotes(afterSize, toUpper(afterSize), " FROM ");
    std::string sizePart;
    std::string trail;
    if (sayPos != std::string::npos) {
      sizePart = trim(afterSize.substr(0, sayPos));
      trail = trim(afterSize.substr(sayPos + 5));
    } else if (fromPos != std::string::npos) {
      sizePart = trim(afterSize.substr(0, fromPos));
      trail = trim(afterSize.substr(fromPos + 6));
    } else {
      sizePart = afterSize;
    }
    auto commaSz = findOutsideQuotes(sizePart, toUpper(sizePart), ",");
    if (commaSz == std::string::npos) {
      bc.fail(line, "SLOT SIZE needs w, h");
      return;
    }
    auto wRaw = trim(sizePart.substr(0, commaSz));
    auto hRaw = trim(sizePart.substr(commaSz + 1));
    Expr w = toUpper(wRaw) == "AUTO" ? Expr{"-1.0", Ty::num()} : bc.expr(wRaw, line);
    Expr h = toUpper(hRaw) == "AUTO" ? Expr{"-1.0", Ty::num()} : bc.expr(hRaw, line);
    bc.expectTy(line, w.ty, Ty::num(), "SLOT SIZE w");
    bc.expectTy(line, h.ty, Ty::num(), "SLOT SIZE h");
    Expr ox{"0", Ty::num()}, oy{"0", Ty::num()};
    if (hasAt) {
      auto commaAt = findOutsideQuotes(atPart, toUpper(atPart), ",");
      if (commaAt == std::string::npos) {
        bc.fail(line, "SLOT AT needs ox, oy");
        return;
      }
      ox = bc.expr(trim(atPart.substr(0, commaAt)), line);
      oy = bc.expr(trim(atPart.substr(commaAt + 1)), line);
      bc.expectTy(line, ox.ty, Ty::num(), "SLOT AT ox");
      bc.expectTy(line, oy.ty, Ty::num(), "SLOT AT oy");
    }
    if (kindRaw == "TEXT") {
      auto t = bc.coerceText(bc.expr(trail, line));
      if (hasAt)
        o << "  hanka_slot_text_at(arena, " << idE.code << ", " << ox.code << ", " << oy.code << ", "
          << w.code << ", " << h.code << ", " << t.code << ");\n";
      else
        o << "  hanka_slot_text(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
          << t.code << ");\n";
    } else if (kindRaw == "BUTTON") {
      auto t = bc.coerceText(bc.expr(trail, line));
      if (hasAt)
        o << "  hanka_slot_button_at(arena, " << idE.code << ", " << ox.code << ", " << oy.code
          << ", " << w.code << ", " << h.code << ", " << t.code << ");\n";
      else
        o << "  hanka_slot_button(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
          << t.code << ");\n";
    } else if (kindRaw == "IMAGE") {
      auto t = bc.coerceText(bc.expr(trail, line));
      if (hasAt)
        o << "  hanka_slot_image_at(arena, " << idE.code << ", " << ox.code << ", " << oy.code
          << ", " << w.code << ", " << h.code << ", " << t.code << ");\n";
      else
        o << "  hanka_slot_image(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
          << t.code << ");\n";
    } else if (kindRaw == "BOX") {
      if (hasAt)
        o << "  hanka_slot_box_at(arena, " << idE.code << ", " << ox.code << ", " << oy.code << ", "
          << w.code << ", " << h.code << ");\n";
      else
        o << "  hanka_slot_box(arena, " << idE.code << ", " << w.code << ", " << h.code << ");\n";
    } else if (kindRaw == "INPUT") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      if (hasAt)
        o << "  hanka_slot_input_at(arena, " << idE.code << ", " << ox.code << ", " << oy.code
          << ", " << w.code << ", " << h.code << ", " << t.code << ", " << inputType << ");\n";
      else
        o << "  hanka_slot_input(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
          << t.code << ", " << inputType << ");\n";
    } else if (kindRaw == "SELECT" || kindRaw == "DROPDOWN") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  hanka_slot_select(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
        << t.code << ");\n";
    } else if (kindRaw == "TABLE") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  hanka_slot_table(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
        << t.code << ");\n";
    } else if (kindRaw == "MODAL" || kindRaw == "DIALOG") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  hanka_slot_modal(arena, " << idE.code << ", " << w.code << ", " << h.code << ", "
        << t.code << ");\n";
    } else {
      bc.fail(line, "SLOT needs TEXT, BUTTON, IMAGE, BOX, INPUT, SELECT, TABLE, or MODAL — got " +
                         kindRaw);
    }
    return;
  }

  /* Argus — conversational scene presentment (DOM backend) */
  if (toUpper(text) == "PAINT THE SCREEN" || toUpper(text) == "PAINT SCREEN") {
    o << "  argus_paint(arena);\n";
    return;
  }
  if (toUpper(text) == "CLEAR THE SCREEN" || toUpper(text) == "CLEAR SCREEN") {
    o << "  hanka_clear(arena);\n";
    o << "  argus_clear(arena);\n";
    return;
  }
  if (startsWithCI(text, "SET THE OPACITY OF ") || startsWithCI(text, "SET OPACITY OF ")) {
    auto rest = startsWithCI(text, "SET THE OPACITY OF ") ? trim(text.substr(18))
                                                          : trim(text.substr(14));
    auto U = toUpper(rest);
    auto toPos = findOutsideQuotes(rest, U, " TO ");
    if (toPos == std::string::npos) {
      bc.fail(line, "SET THE OPACITY OF needs: SET THE OPACITY OF \"id\" TO 0.5");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, toPos)), line));
    auto opE = bc.expr(trim(rest.substr(toPos + 4)), line);
    bc.expectTy(line, opE.ty, Ty::num(), "SET THE OPACITY OF … TO");
    o << "  argus_set_opacity_id(arena, " << idE.code << ", " << opE.code << ");\n";
    return;
  }
  if (startsWithCI(text, "FADE ")) {
    auto rest = trim(text.substr(5));
    auto U = toUpper(rest);
    auto fromPos = findOutsideQuotes(rest, U, " FROM ");
    auto toPos = findOutsideQuotes(rest, U, " TO ");
    auto overPos = findOutsideQuotes(rest, U, " OVER ");
    if (toPos == std::string::npos) {
      bc.fail(line, "FADE needs: FADE \"id\" [FROM 0] TO 1 [OVER 300]");
      return;
    }
    std::string idRaw = trim(rest.substr(0, fromPos != std::string::npos && fromPos < toPos
                                                ? fromPos
                                                : toPos));
    auto idE = bc.coerceText(bc.expr(idRaw, line));
    Expr fromE{"-1.0", Ty::num()};
    if (fromPos != std::string::npos && fromPos < toPos) {
      fromE = bc.expr(trim(rest.substr(fromPos + 6, toPos - (fromPos + 6))), line);
      bc.expectTy(line, fromE.ty, Ty::num(), "FADE FROM");
    }
    std::string afterTo =
        overPos != std::string::npos && overPos > toPos
            ? trim(rest.substr(toPos + 4, overPos - (toPos + 4)))
            : trim(rest.substr(toPos + 4));
    auto toE = bc.expr(afterTo, line);
    bc.expectTy(line, toE.ty, Ty::num(), "FADE TO");
    Expr msE{"300.0", Ty::num()};
    if (overPos != std::string::npos && overPos > toPos) {
      auto overRest = trim(rest.substr(overPos + 6));
      auto ou = toUpper(overRest);
      if (ou.size() >= 3 && ou.compare(ou.size() - 3, 3, " MS") == 0)
        overRest = trim(overRest.substr(0, overRest.size() - 3));
      else if (ou.size() >= 13 && ou.compare(ou.size() - 13, 13, " MILLISECONDS") == 0)
        overRest = trim(overRest.substr(0, overRest.size() - 13));
      msE = bc.expr(overRest, line);
      bc.expectTy(line, msE.ty, Ty::num(), "FADE OVER");
    }
    o << "  argus_fade_to(arena, " << idE.code << ", " << fromE.code << ", " << toE.code << ", "
      << msE.code << ");\n";
    return;
  }
  if (startsWithCI(text, "PLACE ")) {
    auto rest = trim(text.substr(6));
    auto U = toUpper(rest);
    auto asPos = findOutsideQuotes(rest, U, " AS ");
    auto atPos = findOutsideQuotes(rest, U, " AT ");
    auto sizePos = findOutsideQuotes(rest, U, " SIZE ");
    if (asPos == std::string::npos || atPos == std::string::npos || sizePos == std::string::npos ||
        !(asPos < atPos && atPos < sizePos)) {
      bc.fail(line,
              "PLACE needs: PLACE \"id\" AS TEXT|BUTTON|IMAGE|BOX|INPUT|SELECT|TABLE|MODAL AT x, y "
              "SIZE w, h …");
      return;
    }
    auto idE = bc.coerceText(bc.expr(trim(rest.substr(0, asPos)), line));
    auto kindRaw = toUpper(trim(rest.substr(asPos + 4, atPos - (asPos + 4))));
    auto atPart = trim(rest.substr(atPos + 4, sizePos - (atPos + 4)));
    auto afterSize = trim(rest.substr(sizePos + 6));
    int inputType = 0;
    {
      auto aU = toUpper(afterSize);
      size_t p = findOutsideQuotes(afterSize, aU, " AS PASSWORD");
      if (p != std::string::npos) {
        inputType = 1;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 12)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS EMAIL")) != std::string::npos) {
        inputType = 2;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 9)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS CHECKBOX")) != std::string::npos) {
        inputType = 3;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 12)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS RADIO")) != std::string::npos) {
        inputType = 4;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 9)));
      } else if ((p = findOutsideQuotes(afterSize, aU, " AS TEXT")) != std::string::npos) {
        inputType = 0;
        afterSize = trim(trim(afterSize.substr(0, p)) + " " + trim(afterSize.substr(p + 8)));
      }
    }
    auto sayPos = findOutsideQuotes(afterSize, toUpper(afterSize), " SAY ");
    auto fromPos = findOutsideQuotes(afterSize, toUpper(afterSize), " FROM ");
    std::string sizePart;
    std::string trail;
    if (sayPos != std::string::npos) {
      sizePart = trim(afterSize.substr(0, sayPos));
      trail = trim(afterSize.substr(sayPos + 5));
    } else if (fromPos != std::string::npos) {
      sizePart = trim(afterSize.substr(0, fromPos));
      trail = trim(afterSize.substr(fromPos + 6));
    } else {
      sizePart = afterSize;
    }
    auto commaAt = findOutsideQuotes(atPart, toUpper(atPart), ",");
    auto commaSz = findOutsideQuotes(sizePart, toUpper(sizePart), ",");
    if (commaAt == std::string::npos || commaSz == std::string::npos) {
      bc.fail(line, "PLACE AT needs x, y and SIZE needs w, h");
      return;
    }
    auto x = bc.expr(trim(atPart.substr(0, commaAt)), line);
    auto y = bc.expr(trim(atPart.substr(commaAt + 1)), line);
    auto wRaw = trim(sizePart.substr(0, commaSz));
    auto hRaw = trim(sizePart.substr(commaSz + 1));
    Expr w = toUpper(wRaw) == "AUTO" ? Expr{"-1.0", Ty::num()} : bc.expr(wRaw, line);
    Expr h = toUpper(hRaw) == "AUTO" ? Expr{"-1.0", Ty::num()} : bc.expr(hRaw, line);
    if (toUpper(wRaw) == "AUTO" && (kindRaw == "TEXT" || kindRaw == "BUTTON" || kindRaw == "MODAL")) {
      /* resolve at place-time via measure when trail known — use sentinel; paint path measures */
    }
    bc.expectTy(line, x.ty, Ty::num(), "PLACE AT x");
    bc.expectTy(line, y.ty, Ty::num(), "PLACE AT y");
    bc.expectTy(line, w.ty, Ty::num(), "PLACE SIZE w");
    bc.expectTy(line, h.ty, Ty::num(), "PLACE SIZE h");
    if (kindRaw == "TEXT") {
      auto t = bc.coerceText(bc.expr(trail, line));
      if (toUpper(wRaw) == "AUTO")
        o << "  argus_place_text(arena, " << idE.code << ", " << x.code << ", " << y.code
          << ", argus_measure_text(" << t.code << "), " << h.code << ", " << t.code << ");\n";
      else
        o << "  argus_place_text(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
          << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else if (kindRaw == "BUTTON") {
      auto t = bc.coerceText(bc.expr(trail, line));
      o << "  argus_place_button(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else if (kindRaw == "IMAGE") {
      auto t = bc.coerceText(bc.expr(trail, line));
      o << "  argus_place_image(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else if (kindRaw == "BOX") {
      o << "  argus_place_box(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ");\n";
    } else if (kindRaw == "INPUT") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  argus_place_input(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ", " << inputType << ");\n";
    } else if (kindRaw == "SELECT" || kindRaw == "DROPDOWN") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  argus_place_select(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else if (kindRaw == "TABLE") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  argus_place_table(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else if (kindRaw == "MODAL" || kindRaw == "DIALOG") {
      auto t = bc.coerceText(bc.expr(trail.empty() ? "\"\"" : trail, line));
      o << "  argus_place_modal(arena, " << idE.code << ", " << x.code << ", " << y.code << ", "
        << w.code << ", " << h.code << ", " << t.code << ");\n";
    } else {
      bc.fail(line, "PLACE AS needs TEXT, BUTTON, IMAGE, BOX, INPUT, SELECT, TABLE, or MODAL — got " +
                         kindRaw);
    }
    return;
  }

  bc.fail(line, "Unsupported Build statement: " + text +
                    " — Build doesn't understand this yet (Play-only feature?)");
  bc.unsupportedHint = true;
}

bool parse(BC &bc, const std::string &source) {
  /* Triple-quoted TEXT: """ … """ may span lines → single "…" with \n */
  std::string src;
  {
    size_t i = 0;
    while (i < source.size()) {
      if (i + 2 < source.size() && source[i] == '"' && source[i + 1] == '"' && source[i + 2] == '"') {
        i += 3;
        std::string body;
        while (i + 2 < source.size() &&
               !(source[i] == '"' && source[i + 1] == '"' && source[i + 2] == '"')) {
          char c = source[i++];
          if (c == '\\') {
            body.push_back('\\');
            if (i < source.size()) body.push_back(source[i++]);
          } else if (c == '"') {
            body += "\\\"";
          } else if (c == '\n') {
            body += "\\n";
          } else if (c == '\r') {
            /* skip */
          } else {
            body.push_back(c);
          }
        }
        if (i + 2 < source.size()) i += 3;
        src.push_back('"');
        src += body;
        src.push_back('"');
        continue;
      }
      src.push_back(source[i++]);
    }
  }

  std::istringstream in(src);
  std::string raw;
  size_t lineNo = 0;
  enum Mode { Top, InFn, InBp, InMeth, InWhen, InRxWhen };
  Mode mode = Top;
  Fn curFn;
  BP curBp;
  Method curM;
  BrowserWhen curWhen;
  BC::RxWhenDef curRxWhen;
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
      if (startsWithCI(text, "WHEN BACKGROUND REACTIVE ") ||
          startsWithCI(text, "WHEN REACTIVE WEAK ") ||
          startsWithCI(text, "WHEN WEAK REACTIVE ") ||
          startsWithCI(text, "WHEN REACTIVE ")) {
        bool bg = startsWithCI(text, "WHEN BACKGROUND REACTIVE ");
        bool weak = startsWithCI(text, "WHEN REACTIVE WEAK ") ||
                    startsWithCI(text, "WHEN WEAK REACTIVE ");
        size_t prefix = bg ? 25 : (weak ? (startsWithCI(text, "WHEN REACTIVE WEAK ") ? 19 : 18) : 14);
        auto rest = trim(text.substr(prefix));
        stripDo(rest);
        auto U = toUpper(rest);
        auto chPos = U.find(" CHANGES");
        if (chPos == std::string::npos) {
          bc.fail(lineNo, "WHEN REACTIVE needs: WHEN REACTIVE cell CHANGES DO");
          return false;
        }
        auto cellName = stripThe(trim(rest.substr(0, chPos)));
        if (cellName.empty()) {
          bc.fail(lineNo, "WHEN REACTIVE needs a cell name — WHEN REACTIVE count CHANGES DO");
          return false;
        }
        curRxWhen = {};
        curRxWhen.cellName = cellName;
        curRxWhen.background = bg;
        curRxWhen.weak = weak;
        curRxWhen.line = lineNo;
        mode = InRxWhen;
        continue;
      }
      if (startsWithCI(text, "WHEN ")) {
        curWhen = {};
        auto rest = trim(text.substr(5));
        stripDo(rest);
        auto U = toUpper(rest);
        if (startsWithCI(rest, "THE ROUTE IS ")) {
          curWhen.event = "route";
          curWhen.elementId = unquoteText(trim(rest.substr(13)));
          if (curWhen.elementId.empty()) {
            bc.fail(lineNo, "WHEN THE ROUTE IS needs a name — WHEN THE ROUTE IS \"home\" DO");
            return false;
          }
        } else if (startsWithCI(rest, "THE VIEWPORT CHANGES") ||
                   startsWithCI(rest, "THE WINDOW CHANGES") ||
                   toUpper(rest) == "THE VIEWPORT CHANGES" ||
                   startsWithCI(rest, "THE VIEWPORT IS CHANGED")) {
          curWhen.event = "viewport";
          curWhen.elementId = "";
        } else if (startsWithCI(rest, "TIMELINE ") && U.find(" IS FINISHED") != std::string::npos) {
          curWhen.event = "timeline";
          auto fin = U.find(" IS FINISHED");
          curWhen.elementId = unquoteText(trim(rest.substr(9, fin - 9)));
          if (curWhen.elementId.empty()) {
            bc.fail(lineNo, "WHEN TIMELINE … IS FINISHED needs a timeline id");
            return false;
          }
        } else if (startsWithCI(rest, "FETCH ") && U.find(" IS READY") != std::string::npos) {
          curWhen.event = "fetch";
          auto ready = U.find(" IS READY");
          curWhen.elementId = unquoteText(trim(rest.substr(6, ready - 6)));
          if (curWhen.elementId.empty()) {
            bc.fail(lineNo, "WHEN FETCH … IS READY needs a job id");
            return false;
          }
        } else {
          size_t evPos = std::string::npos;
          if ((evPos = U.find(" IS CLICKED")) != std::string::npos)
            curWhen.event = "click";
          else if ((evPos = U.find(" IS CHANGED")) != std::string::npos)
            curWhen.event = "change";
          else if ((evPos = U.find(" IS SUBMITTED")) != std::string::npos)
            curWhen.event = "submit";
          else {
            bc.fail(lineNo, "WHEN needs IS CLICKED|CHANGED|SUBMITTED, THE ROUTE IS, "
                            "THE VIEWPORT CHANGES, TIMELINE … IS FINISHED, or FETCH … IS READY");
            return false;
          }
          curWhen.elementId = unquoteText(trim(rest.substr(0, evPos)));
          if (curWhen.elementId.empty()) {
            bc.fail(lineNo, "WHEN needs an element id — WHEN \"cta\" IS CLICKED DO");
            return false;
          }
        }
        if (curWhen.event.empty()) curWhen.event = "click";
        curWhen.exportName = "luke_when_" + std::to_string(bc.whenSeq++);
        mode = InWhen;
        continue;
      }
      bc.top.push_back({lineNo, text});
      continue;
    }

    if (mode == InRxWhen) {
      if (toUpper(text) == "END WHEN REACTIVE" || toUpper(text) == "ENDWHEN REACTIVE") {
        curRxWhen.seq = ++bc.rxWhenSeq;
        bc.rxWhenDefs.push_back(curRxWhen);
        bc.top.push_back({lineNo, "REACTIVE WATCH REGISTER " + std::to_string(curRxWhen.seq)});
        mode = Top;
        continue;
      }
      curRxWhen.body.push_back(text);
      curRxWhen.lines.push_back(lineNo);
      continue;
    }

    if (mode == InWhen) {
      if (toUpper(text) == "END WHEN" || toUpper(text) == "ENDWHEN") {
        bc.pageWhens.push_back(curWhen);
        bc.hasPage = true;
        mode = Top;
        continue;
      }
      curWhen.body.push_back(text);
      curWhen.lines.push_back(lineNo);
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
  o << "#include \"argus.h\"\n";
  o << "#include \"hanka.h\"\n";
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

  if (bc.forBrowser || !bc.pageWhens.empty())
    o << "static LukeArena *luke_page_arena = NULL;\n\n";

  /* Pre-declare reactive names so THE a IS b … can forward-reference. */
  for (auto &tl : bc.top) {
    const std::string &text = tl.second;
    if (startsWithCI(text, "REMEMBER ")) {
      auto rest = trim(text.substr(9));
      auto U = toUpper(rest);
      auto asPos = U.find(" AS ");
      if (asPos == std::string::npos) continue;
      auto name = stripThe(trim(rest.substr(0, asPos)));
      if (name.empty()) continue;
      auto after = trim(rest.substr(asPos + 4));
      auto aU = toUpper(after);
      auto setPos = aU.find(" SET TO ");
      std::string tyTok = setPos == std::string::npos ? after : trim(after.substr(0, setPos));
      /* First token only for typed empty forms */
      auto sp = tyTok.find(' ');
      if (sp != std::string::npos) tyTok = trim(tyTok.substr(0, sp));
      Ty hint = bc.parseTy(tyTok);
      if (hint.k == K::Void) hint = Ty::num();
      bc.usesRx = true;
      bc.locals[name] = hint;
      if (!bc.rxCells.count(name)) bc.rxCellOrder.push_back(name);
      bc.rxCells[name] = true;
      if (!bc.rxCellTy.count(name)) bc.rxCellTy[name] = hint;
    } else if (startsWithCI(text, "THE ") && !startsWithCI(text, "THE VALUE OF ") &&
               !startsWithCI(text, "THE BODY OF ") && !startsWithCI(text, "THE STATUS OF ")) {
      auto rest = trim(text.substr(4));
      auto U = toUpper(rest);
      auto isPos = U.find(" IS ");
      if (isPos == std::string::npos) continue;
      auto name = stripThe(trim(rest.substr(0, isPos)));
      bool simple = !name.empty();
      for (char c : name)
        if (!(isalnum((unsigned char)c) || c == '_')) simple = false;
      if (!simple) continue;
      bc.usesRx = true;
      bc.locals[name] = Ty::num();
      if (!bc.rxCells.count(name)) bc.rxCellOrder.push_back(name);
      bc.rxCells[name] = true;
      bc.rxDerived[name] = true;
      if (!bc.rxCellTy.count(name)) bc.rxCellTy[name] = Ty::num();
    }
  }

  /* Emit top-level + WHEN bodies first so REMEMBER / THE x IS populate reactive metadata. */
  bc.locals.clear();
  for (auto &kv : bc.rxCells) {
    Ty ty = bc.rxCellTy.count(kv.first) ? bc.rxCellTy[kv.first] : Ty::num();
    bc.locals[kv.first] = ty;
  }
  std::ostringstream mainBody;
  for (auto &tl : bc.top) {
    stmt(bc, tl.second, tl.first, mainBody);
    if (bc.bad) return {};
  }
  if (!bc.arenaMarks.empty()) {
    bc.fail(1, "Unclosed IN ARENA — missing END ARENA");
    return {};
  }
  if (!bc.hankaStack.empty()) {
    bc.fail(1, "Unclosed BEGIN " + bc.hankaStack.back() + " — missing END " + bc.hankaStack.back());
    return {};
  }
  if (!bc.forEachVars.empty()) {
    bc.fail(1, "Unclosed FOR EACH — missing END FOR");
    return {};
  }
  if (!bc.rxComponentStack.empty()) {
    bc.fail(1, "Unclosed COMPONENT " + bc.rxComponentStack.back() + " — missing END COMPONENT");
    return {};
  }
  if (!bc.rxBoundaryStack.empty()) {
    bc.fail(1, "Unclosed ERROR BOUNDARY " + bc.rxBoundaryStack.back() +
                   " — missing END ERROR BOUNDARY");
    return {};
  }

  for (auto &tl : bc.top) {
    if (startsWithCI(tl.second, "START TIMELINE ") || startsWithCI(tl.second, "RUN TIMELINE "))
      bc.usesTimeline = true;
  }
  for (auto &w : bc.pageWhens) {
    for (auto &line : w.body) {
      if (startsWithCI(line, "START TIMELINE ") || startsWithCI(line, "RUN TIMELINE "))
        bc.usesTimeline = true;
    }
  }

  /* Ensure reactive names are visible while compiling derived compute fns. */
  for (auto &kv : bc.rxCells) {
    Ty ty = bc.rxCellTy.count(kv.first) ? bc.rxCellTy[kv.first] : Ty::num();
    bc.locals[kv.first] = ty;
  }

  if (bc.usesRx) {
    o << "static LukeRxGraph *_luke_rx = NULL;\n";
    o << "static LukeRxGraph _luke_rx_storage;\n";
    for (auto &name : bc.rxCellOrder)
      o << "static LukeRxId _luke_rx_id_" << cIdent(name) << ";\n";
    for (auto &b : bc.rxBindDefs)
      o << "static LukeRxId _luke_rx_id_bind_" << b.seq << ";\n";
    for (auto &b : bc.rxListBindDefs)
      o << "static LukeRxId _luke_rx_id_bind_" << b.seq << ";\n";
    for (auto &b : bc.rxOpacityBindDefs)
      o << "static LukeRxId _luke_rx_id_bind_" << b.seq << ";\n";
    for (auto &w : bc.rxWhenDefs)
      o << "static LukeRxId _luke_rx_id_when_" << w.seq << ";\n";
    if (bc.usesTimeline || bc.forBrowser) o << "static LukeText _luke_active_timeline_id;\n";
    o << "\n";
    for (auto &kv : bc.rxCells) {
      auto dot = kv.first.find('.');
      if (dot != std::string::npos) {
        Ty ty = bc.rxCellTy.count(kv.first) ? bc.rxCellTy[kv.first] : Ty::num();
        bc.locals[kv.first.substr(dot + 1)] = ty;
      }
    }
    for (auto &d : bc.rxDerivedDefs) {
      bc.rxGraphVar = "g";
      auto dot = d.name.find('.');
      if (dot != std::string::npos) bc.locals[d.name.substr(dot + 1)] = Ty::num();
      auto e = bc.expr(d.exprRaw, d.line);
      if (bc.bad) return {};
      bc.expectTy(d.line, e.ty, Ty::num(), "THE " + d.name + " IS");
      if (bc.bad) return {};
      o << "static double _luke_rx_fn_" << cIdent(d.name) << "(LukeRxGraph *g, void *ctx) {\n";
      o << "  (void)ctx;\n";
      o << "  return " << e.code << ";\n";
      o << "}\n\n";
    }
    for (auto &w : bc.rxWhenDefs) {
      bc.rxGraphVar = "g";
      o << "static void _luke_rx_when_" << w.seq << "(LukeRxGraph *g, void *ctx) {\n";
      o << "  (void)ctx;\n";
      auto watchCell = resolveRxCellName(bc.rxCells, bc.rxEntityStack, w.cellName);
      if (!bc.rxCells.count(watchCell)) {
        bc.fail(w.line, "WHEN REACTIVE needs a REMEMBER'd cell — not '" + w.cellName + "'");
        return {};
      }
      if (bc.rxCellTy.count(watchCell) && bc.rxCellTy[watchCell].k == K::Text)
        o << "  (void)luke_rx_read_text(g, _luke_rx_id_" << cIdent(watchCell) << ");\n";
      else
        o << "  (void)luke_rx_read_num(g, _luke_rx_id_" << cIdent(watchCell) << ");\n";
      bc.locals.clear();
      for (auto &kv : bc.rxCells) {
        Ty ty = bc.rxCellTy.count(kv.first) ? bc.rxCellTy[kv.first] : Ty::num();
        bc.locals[kv.first] = ty;
        auto dot = kv.first.find('.');
        if (dot != std::string::npos) bc.locals[kv.first.substr(dot + 1)] = ty;
      }
      std::ostringstream wb;
      for (size_t i = 0; i < w.body.size(); ++i) {
        stmt(bc, w.body[i], w.lines[i], wb);
        if (bc.bad) return {};
      }
      o << wb.str();
      o << "}\n\n";
    }
    for (auto &b : bc.rxBindDefs) {
      bc.rxGraphVar = "g";
      auto te = bc.coerceText(bc.expr(b.exprRaw, b.line));
      if (bc.bad) return {};
      o << "static void _luke_rx_bind_" << b.seq << "(LukeRxGraph *g, void *ctx) {\n";
      o << "  (void)ctx;\n";
      o << "  LukeArena *arena = g->arena;\n";
      o << "  LukeText _luke_bind_t = " << te.code << ";\n";
      o << "  luke_rx_ui_set_text(g, " << b.argusId << ", _luke_bind_t);\n";
      o << "}\n\n";
    }
    for (auto &b : bc.rxListBindDefs) {
      bc.rxGraphVar = "g";
      auto pe = bc.coerceText(bc.expr(b.prefixRaw, b.line));
      if (bc.bad) return {};
      o << "static void _luke_rx_bind_list_" << b.seq << "(LukeRxGraph *g, void *ctx) {\n";
      o << "  (void)ctx;\n";
      o << "  LukeArena *arena = g->arena;\n";
      o << "  (void)arena;\n";
      o << "  luke_rx_ui_paint_list(g, _luke_rx_id_" << cIdent(b.listName) << ", " << pe.code
        << ");\n";
      o << "}\n\n";
    }
    for (auto &b : bc.rxOpacityBindDefs) {
      bc.rxGraphVar = "g";
      auto oe = bc.expr(b.exprRaw, b.line);
      if (bc.bad) return {};
      bc.expectTy(b.line, oe.ty, Ty::num(), "BIND OPACITY");
      o << "static void _luke_rx_bind_opacity_" << b.seq << "(LukeRxGraph *g, void *ctx) {\n";
      o << "  (void)ctx;\n";
      o << "  luke_rx_ui_set_opacity(g, " << b.argusId << ", " << oe.code << ");\n";
      o << "}\n\n";
    }
    if (bc.forBrowser && bc.usesRx) {
      o << "__attribute__((export_name(\"luke_timeline_progress\")))\n";
      o << "void luke_timeline_progress(double t) {\n";
      o << "  if (_luke_rx) luke_rx_timeline_progress(_luke_rx, _luke_active_timeline_id, t);\n";
      o << "}\n\n";
      o << "__attribute__((export_name(\"luke_timeline_finish_export\")))\n";
      o << "void luke_timeline_finish_export(void) {\n";
      o << "  if (_luke_rx) luke_rx_timeline_finish(_luke_rx, _luke_active_timeline_id);\n";
      o << "}\n\n";
    }
    bc.rxGraphVar = "_luke_rx";
  }

  /* Collect WHEN bodies; START FETCH inside handlers may register rxFetchBinds. */
  std::vector<std::string> whenBodies;
  if (!bc.pageWhens.empty()) {
    for (auto &w : bc.pageWhens) {
      bc.locals.clear();
      for (auto &kv : bc.rxCells) {
        Ty ty = bc.rxCellTy.count(kv.first) ? bc.rxCellTy[kv.first] : Ty::num();
        bc.locals[kv.first] = ty;
      }
      std::ostringstream wb;
      for (size_t i = 0; i < w.body.size(); ++i) {
        stmt(bc, w.body[i], w.lines[i], wb);
        if (bc.bad) return {};
      }
      if (!bc.hankaStack.empty()) {
        bc.fail(1, "Unclosed BEGIN " + bc.hankaStack.back() + " in WHEN handler");
        return {};
      }
      whenBodies.push_back(wb.str());
    }
  }

  /* Phase 4: ensure every START FETCH … INTO has a FETCH READY continuation. */
  for (auto &fb : bc.rxFetchBinds) {
    bool found = false;
    for (auto &w : bc.pageWhens) {
      if (w.event == "fetch" && w.elementId == fb.jobId) {
        found = true;
        break;
      }
    }
    if (!found) {
      BrowserWhen w;
      w.event = "fetch";
      w.elementId = fb.jobId;
      w.exportName = "luke_when_" + std::to_string(bc.whenSeq++);
      bc.pageWhens.push_back(w);
      bc.hasPage = true;
      whenBodies.push_back("");
    }
  }

  /* Phase 6: ensure START TIMELINE has FINISHED continuation (browser). */
  if (bc.forBrowser || bc.hasPage) {
  for (auto &tb : bc.rxTimelineBinds) {
    bool found = false;
    for (auto &w : bc.pageWhens) {
      if (w.event == "timeline" && w.elementId == tb.jobId) {
        found = true;
        break;
      }
    }
    if (!found) {
      BrowserWhen w;
      w.event = "timeline";
      w.elementId = tb.jobId;
      w.exportName = "luke_when_" + std::to_string(bc.whenSeq++);
      bc.pageWhens.push_back(w);
      bc.hasPage = true;
      whenBodies.push_back("");
    }
  }
  }

  if (!bc.pageWhens.empty()) {
    for (size_t wi = 0; wi < bc.pageWhens.size(); ++wi) {
      auto &w = bc.pageWhens[wi];
      if (bc.forBrowser) o << "__attribute__((export_name(\"" << w.exportName << "\")))\n";
      o << "void " << w.exportName << "(void) {\n";
      o << "  LukeArena *arena = luke_page_arena;\n";
      o << "  if (!arena) return;\n";
      if (w.event == "fetch") {
        /* Continuation edge: result cells first (batched), then user body. */
        for (auto &fb : bc.rxFetchBinds) {
          if (fb.jobId != w.elementId) continue;
          o << "  if (_luke_rx) luke_rx_batch_begin(_luke_rx);\n";
          if (!fb.bodyCell.empty()) {
            o << "  luke_rx_write_text(_luke_rx, _luke_rx_id_" << cIdent(fb.bodyCell)
              << ", luke_js_fetch_body(arena, luke_text(\"" << esc(fb.jobId) << "\")));\n";
          }
          if (!fb.statusCell.empty()) {
            o << "  luke_rx_write_num(_luke_rx, _luke_rx_id_" << cIdent(fb.statusCell)
              << ", luke_js_fetch_status(luke_text(\"" << esc(fb.jobId) << "\")));\n";
          }
          if (!fb.readyCell.empty()) {
            o << "  luke_rx_write_num(_luke_rx, _luke_rx_id_" << cIdent(fb.readyCell) << ", 1);\n";
          }
          o << "  if (_luke_rx) luke_rx_batch_end(_luke_rx);\n";
        }
      }
      o << whenBodies[wi];
      o << "}\n\n";
    }
  }

  o << "int main(int argc, char **argv) {\n";
  o << "  luke_runtime_set_args(argc, argv);\n";
  o << "  LukeArena arena_storage; LukeArena *arena = &arena_storage;\n";
  o << "  luke_arena_init(arena, 1u<<20);\n";
  if (bc.forBrowser || !bc.pageWhens.empty())
    o << "  luke_page_arena = arena;\n";
  if (bc.usesRx) {
    o << "  luke_rx_graph_init(&_luke_rx_storage, arena);\n";
    o << "  _luke_rx = &_luke_rx_storage;\n";
    if (bc.usesRxUi) o << "  luke_rx_ui_enable(_luke_rx);\n";
  }
  o << mainBody.str();
  if (bc.forBrowser || !bc.pageWhens.empty()) {
    /* Keep arena alive for WHEN handlers / page lifetime. */
    o << "  return 0;\n}\n";
  } else {
    o << "  luke_arena_free(arena);\n  return 0;\n}\n";
  }
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
  r.hasPage = bc.hasPage;
  r.pageTitle = bc.pageTitle;
  r.pageStyle = bc.pageStyle;
  r.pageBody = bc.pageBody;
  r.pageFonts = bc.pageFonts;
  r.pageWhens = bc.pageWhens;
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
