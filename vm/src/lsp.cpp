#include "luke/build.hpp"
#include "luke2.hpp"
#include "luke_expr.hpp"
#include "luke_parse.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace luke {
namespace {

std::string jsonEscape(const std::string &s) {
  std::ostringstream o;
  for (char c : s) {
    if (c == '"' || c == '\\') o << '\\' << c;
    else if (c == '\n') o << "\\n";
    else if (c == '\r') o << "\\r";
    else if (c == '\t') o << "\\t";
    else o << c;
  }
  return o.str();
}

void writeMessage(const std::string &body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

std::string readMessage() {
  std::string line;
  size_t contentLength = 0;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    auto key = line.substr(0, colon);
    auto val = line.substr(colon + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
    if (key == "Content-Length") contentLength = (size_t)std::stoul(val);
  }
  if (contentLength == 0) return {};
  std::string body(contentLength, '\0');
  std::cin.read(&body[0], (std::streamsize)contentLength);
  if ((size_t)std::cin.gcount() != contentLength) return {};
  return body;
}

std::string extractMethod(const std::string &msg) {
  auto keyPos = msg.find("\"method\"");
  if (keyPos == std::string::npos) return {};
  auto colon = msg.find(':', keyPos);
  if (colon == std::string::npos) return {};
  auto pos = msg.find('"', colon);
  if (pos == std::string::npos) return {};
  auto end = msg.find('"', pos + 1);
  if (end == std::string::npos) return {};
  return msg.substr(pos + 1, end - pos - 1);
}

std::string extractId(const std::string &msg) {
  auto pos = msg.find("\"id\"");
  if (pos == std::string::npos) return "null";
  pos = msg.find_first_of("0123456789\"n", pos + 4);
  if (pos == std::string::npos) return "null";
  if (msg.compare(pos, 4, "null") == 0) return "null";
  if (msg[pos] == '"') {
    auto end = msg.find('"', pos + 1);
    if (end == std::string::npos) return "null";
    return msg.substr(pos, end - pos + 1);
  }
  size_t end = pos;
  while (end < msg.size() && (isdigit((unsigned char)msg[end]))) ++end;
  return msg.substr(pos, end - pos);
}

std::string extractUri(const std::string &msg) {
  auto pos = msg.find("\"uri\"");
  if (pos == std::string::npos) return {};
  pos = msg.find('"', pos + 5);
  if (pos == std::string::npos) return {};
  auto end = msg.find('"', pos + 1);
  if (end == std::string::npos) return {};
  return msg.substr(pos + 1, end - pos - 1);
}

/* Decode JSON string value after the first "text" key (didOpen / Full didChange). */
std::string extractText(const std::string &msg) {
  auto key = msg.find("\"text\"");
  if (key == std::string::npos) return {};
  auto pos = msg.find('"', key + 6);
  if (pos == std::string::npos) return {};
  std::string out;
  for (size_t i = pos + 1; i < msg.size(); ++i) {
    char c = msg[i];
    if (c == '\\' && i + 1 < msg.size()) {
      char n = msg[++i];
      if (n == 'n') out.push_back('\n');
      else if (n == 'r') out.push_back('\r');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

int extractIntAfter(const std::string &msg, const char *key) {
  auto pos = msg.find(key);
  if (pos == std::string::npos) return -1;
  pos = msg.find_first_of("0123456789", pos + strlen(key));
  if (pos == std::string::npos) return -1;
  size_t end = pos;
  while (end < msg.size() && isdigit((unsigned char)msg[end])) ++end;
  try {
    return std::stoi(msg.substr(pos, end - pos));
  } catch (...) {
    return -1;
  }
}

/* Prefer values inside the position object so we don't pick unrelated ints. */
int extractPositionField(const std::string &msg, const char *field) {
  auto block = msg.find("\"position\"");
  if (block == std::string::npos) return extractIntAfter(msg, field);
  return extractIntAfter(msg.substr(block), field);
}

std::vector<std::string> splitLines(const std::string &src) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : src) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else if (c != '\r')
      cur.push_back(c);
  }
  lines.push_back(cur);
  return lines;
}

std::string toUpperCopy(std::string s) {
  for (char &c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

std::string trimCopy(std::string s) {
  size_t b = 0;
  while (b < s.size() && isspace((unsigned char)s[b])) ++b;
  size_t e = s.size();
  while (e > b && isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

int countParams(const std::string &aux) {
  if (trimCopy(aux).empty()) return 0;
  int n = 1;
  for (char c : aux)
    if (c == ',') ++n;
  return n;
}

std::string normalizeType(std::string t) {
  t = trimCopy(t);
  auto U = toUpperCopy(t);
  if (U.empty()) return {};
  if (U == "NUMBER" || U == "NUM") return "NUMBER";
  if (U == "INTEGER" || U == "INT") return "INTEGER";
  if (U == "TEXT" || U == "STRING") return "TEXT";
  if (U == "FLAG" || U == "BOOL" || U == "BOOLEAN") return "FLAG";
  if (U == "LIST") return "LIST";
  if (U == "MAP") return "MAP";
  if (U == "JSON") return "JSON";
  return t;
}

struct Symbol {
  std::string name;
  std::string kind;      /* variable | function | cell | class | parameter | derived */
  std::string typeName;  /* NUMBER / TEXT / … */
  std::string signature; /* full header for functions */
  std::string doc;       /* short markdown doc */
  int arity = -1;
  int line = 0; /* 0-based */
  int character = 0;
  int endLine = 0;
  int endCharacter = 0;
};

std::string functionSignature(const Stmt &s) {
  std::ostringstream o;
  o << "THIS IS FUNCTION " << s.name;
  if (!s.aux.empty()) o << " WITH " << s.aux;
  if (!s.typeName.empty()) o << " GIVES BACK " << s.typeName;
  return o.str();
}

std::string symbolDoc(const Symbol &s) {
  if (!s.doc.empty()) return s.doc;
  if (s.kind == "function") {
    std::ostringstream o;
    o << "Luke function";
    if (s.arity >= 0) o << " (" << s.arity << (s.arity == 1 ? " parameter)" : " parameters)");
    if (!s.typeName.empty()) o << " → `" << s.typeName << "`";
    o << ".";
    return o.str();
  }
  if (s.kind == "cell") return "Reactive cell — changes propagate through derived / effect.";
  if (s.kind == "derived") return "Derived reactive value (`derived x = …`).";
  if (s.kind == "variable") return "Local binding (`let` / `var`).";
  if (s.kind == "class") return "Struct / blueprint.";
  if (s.kind == "parameter") return "Function parameter.";
  return {};
}

/* Phase 5: buffer is usually v2 `.luke` — lower before v1 parseLuke / analyze. */
std::string lspLowered(const std::string &source, const BuildOptions &opts) {
  bool ok = true;
  auto out = luke2::maybeLowerSource("buffer.luke", source, luke2::SyntaxMode::Auto,
                                     opts.stdlibPath, &ok, nullptr, nullptr);
  return ok ? out : source;
}

std::vector<Symbol> scanSymbols(const std::string &source) {
  std::vector<Symbol> out;
  BuildOptions bo;
  Program prog = parseLuke(lspLowered(source, bo));
  std::function<void(const Stmt &)> walk = [&](const Stmt &s) {
    if ((s.kind == StmtKind::Let || s.kind == StmtKind::Remember ||
         s.kind == StmtKind::Function || s.kind == StmtKind::WhenReactive ||
         s.kind == StmtKind::Watch || s.kind == StmtKind::Blueprint) &&
        !s.name.empty()) {
      Symbol sym;
      sym.name = s.name;
      sym.typeName = normalizeType(s.typeName);
      sym.line = s.line > 0 ? (int)s.line - 1 : 0;
      sym.endLine = s.endLine > 0 ? (int)s.endLine - 1 : sym.line;
      auto pos = s.text.find(s.name);
      sym.character = pos != std::string::npos ? (int)pos : 0;
      sym.endCharacter = sym.character + (int)s.name.size();
      if (s.kind == StmtKind::Function) {
        sym.kind = "function";
        sym.signature = functionSignature(s);
        sym.arity = countParams(s.aux);
        sym.doc = symbolDoc(sym);
        out.push_back(sym);
        /* Parameters from WITH a AS NUMBER, b AS TEXT */
        auto aux = s.aux;
        size_t start = 0;
        while (start < aux.size()) {
          auto comma = aux.find(',', start);
          auto piece = trimCopy(aux.substr(start, comma == std::string::npos ? std::string::npos
                                                                              : comma - start));
          auto u = toUpperCopy(piece);
          auto asPos = u.find(" AS ");
          std::string pname = asPos == std::string::npos ? piece : trimCopy(piece.substr(0, asPos));
          std::string pty =
              asPos == std::string::npos ? "" : normalizeType(piece.substr(asPos + 4));
          if (!pname.empty()) {
            Symbol p;
            p.name = pname;
            p.kind = "parameter";
            p.typeName = pty;
            p.line = sym.line;
            p.character = 0;
            p.endLine = sym.endLine;
            p.endCharacter = (int)pname.size();
            p.doc = "Parameter of `" + s.name + "`";
            if (!pty.empty()) p.signature = pname + " AS " + pty;
            out.push_back(p);
          }
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
      } else if (s.kind == StmtKind::Remember || s.kind == StmtKind::WhenReactive ||
                 s.kind == StmtKind::Watch) {
        sym.kind = "cell";
        /* REMEMBER x AS 100 → value, not type name */
        if (!sym.typeName.empty()) {
          auto tu = toUpperCopy(sym.typeName);
          bool knownTy = tu == "NUMBER" || tu == "INTEGER" || tu == "TEXT" || tu == "FLAG" ||
                         tu == "LIST" || tu == "MAP" || tu == "JSON";
          if (!knownTy) {
            if (!sym.typeName.empty() &&
                (isdigit((unsigned char)sym.typeName[0]) || sym.typeName[0] == '-' ||
                 sym.typeName[0] == '.'))
              sym.typeName = "NUMBER";
            else if (!sym.typeName.empty() && sym.typeName[0] == '"')
              sym.typeName = "TEXT";
            else
              sym.typeName.clear();
          }
        }
        if (sym.typeName.empty()) {
          auto U = toUpperCopy(s.text);
          auto asPos = U.find(" AS ");
          if (asPos != std::string::npos) {
            auto after = trimCopy(s.text.substr(asPos + 4));
            auto aU = toUpperCopy(after);
            if (aU == "NUMBER" || aU == "INTEGER" || aU == "TEXT" || aU == "FLAG" ||
                aU == "LIST" || aU == "MAP" || aU == "JSON")
              sym.typeName = normalizeType(after);
            else if (!after.empty() && (isdigit((unsigned char)after[0]) || after[0] == '-' ||
                                        after[0] == '.'))
              sym.typeName = "NUMBER";
            else if (!after.empty() && after[0] == '"')
              sym.typeName = "TEXT";
          }
        }
        sym.doc = symbolDoc(sym);
        out.push_back(sym);
      } else if (s.kind == StmtKind::Blueprint) {
        sym.kind = "class";
        sym.doc = symbolDoc(sym);
        out.push_back(sym);
      } else {
        sym.kind = "variable";
        sym.doc = symbolDoc(sym);
        out.push_back(sym);
      }
    }
    /* THE total IS … — often Raw; still a derived cell. */
    if (s.kind == StmtKind::Raw || s.kind == StmtKind::Empty) {
      auto U = toUpperCopy(trimCopy(s.text));
      if (U.rfind("THE ", 0) == 0 && U.find(" IS ") != std::string::npos) {
        auto rest = trimCopy(s.text.substr(4));
        auto rU = toUpperCopy(rest);
        auto isPos = rU.find(" IS ");
        auto name = trimCopy(rest.substr(0, isPos));
        bool simple = !name.empty();
        for (char c : name)
          if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) simple = false;
        if (simple) {
          Symbol sym;
          sym.name = name;
          sym.kind = "derived";
          sym.typeName = "NUMBER";
          sym.line = s.line > 0 ? (int)s.line - 1 : 0;
          sym.endLine = sym.line;
          auto pos = s.text.find(name);
          sym.character = pos != std::string::npos ? (int)pos : 4;
          sym.endCharacter = sym.character + (int)name.size();
          sym.signature = "THE " + name + " IS …";
          sym.doc = symbolDoc(sym);
          out.push_back(sym);
        }
      }
    }
    for (auto &c : s.body) walk(c);
    for (auto &c : s.elseBody) walk(c);
  };
  for (auto &s : prog.stmts) walk(s);
  return out;
}

std::string wordAt(const std::string &line, int character, int *startOut = nullptr,
                   int *endOut = nullptr) {
  if (character < 0) character = 0;
  if (character > (int)line.size()) character = (int)line.size();
  int s = character;
  while (s > 0 && (isalnum((unsigned char)line[s - 1]) || line[s - 1] == '_')) --s;
  int e = character;
  while (e < (int)line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
  if (startOut) *startOut = s;
  if (endOut) *endOut = e;
  return line.substr(s, e - s);
}

/* Completion prefix: empty when the cursor is not inside an identifier (new token). */
std::string completionPrefixAt(const std::string &line, int character) {
  if (character < 0) character = 0;
  if (character > (int)line.size()) character = (int)line.size();
  if (character == 0) return {};
  char prev = line[character - 1];
  if (!(isalnum((unsigned char)prev) || prev == '_')) return {};
  return wordAt(line, character);
}

struct DiagInfo {
  int line = 0;
  std::string message;
};

std::vector<DiagInfo> collectDiags(const std::string &source, const BuildOptions &opts) {
  std::vector<DiagInfo> out;
  BuildResult r = analyzeLukeBuild(lspLowered(source, opts), opts);
  if (!r.ok && !r.error.empty()) {
    DiagInfo d;
    d.message = r.error;
    d.line = 0;
    std::smatch m;
    std::regex re("line ([0-9]+)");
    if (std::regex_search(r.error, m, re)) d.line = std::stoi(m[1].str());
    if (d.line > 0) d.line -= 1;
    if (d.line < 0) d.line = 0;
    out.push_back(d);
  }
  return out;
}

void publishDiagnostics(const std::string &uri, const std::string &source,
                        const BuildOptions &opts) {
  auto diags = collectDiags(source, opts);
  std::ostringstream arr;
  arr << "[";
  for (size_t i = 0; i < diags.size(); ++i) {
    if (i) arr << ",";
    arr << "{\"range\":{\"start\":{\"line\":" << diags[i].line
        << ",\"character\":0},\"end\":{\"line\":" << diags[i].line
        << ",\"character\":200}},\"severity\":1,\"source\":\"lukelang\",\"message\":\""
        << jsonEscape(diags[i].message) << "\",\"code\":\"luke.build\"}";
  }
  arr << "]";
  std::ostringstream note;
  note << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
       << "\"uri\":\"" << jsonEscape(uri) << "\",\"diagnostics\":" << arr.str() << "}}";
  writeMessage(note.str());
}

/* Dual-syntax window (Phase 4): v2 keywords first, v1 phrases retained until Phase 5. */
const char *kStmtKeywords[] = {
    /* v2 */
    "print", "let", "var", "fn", "return", "if", "else", "while", "for", "in", "break", "try",
    "catch", "throw", "arena", "import", "struct", "init", "private", "test", "assert", "signal",
    "secret", "derived", "effect", "batch", "watch", "bind", "from", "where", "as", "on", "fill",
    "paint", "layout", "page", "raw",
    /* v1 */
    "MY", "NAME", "IS", "SET", "TO", "AS", "SPEAK", "ASK", "WITH", "REMEMBER", "WATCH", "PUSH",
    "WHEN", "IF", "DO", "END", "WHILE", "FOR", "IMPORT", "GIVE", "BACK", "MAKE", "SURE", "CHANGE",
    "INCREASE", "DECREASE", "THE", "THIS", "FUNCTION", "BLUEPRINT", "SECRET", "BIND", "BEGIN",
    "COLUMN", "LAY", "OUT", "PAINT",
    nullptr};

const char *kTypeKeywords[] = {"int",    "float", "str",  "bool", "json", "list", "map",
                               "Server", "Request", "Db",  "NUMBER", "INTEGER", "TEXT", "FLAG",
                               "LIST",   "MAP",   "JSON", "TRUE", "FALSE", "true", "false",
                               nullptr};

const char *kExprKeywords[] = {"ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "AND", "EQUALS",
                               "NOT", "MULTIPLIED", "DIVIDED", "BY", nullptr};

enum class LineCtx { StatementStart, AfterAs, AfterWith, InExpr, AfterAsk, Other };

LineCtx classifyLineContext(const std::string &line, int character) {
  std::string prefix = line.substr(0, std::min(character, (int)line.size()));
  auto U = toUpperCopy(trimCopy(prefix));
  if (U.empty()) return LineCtx::StatementStart;
  auto asPos = U.rfind(" AS ");
  if (asPos != std::string::npos) {
    auto after = trimCopy(U.substr(asPos + 4));
    if (after.empty()) return LineCtx::AfterAs;
  }
  if (U.size() >= 3 && U.compare(U.size() - 3, 3, " AS") == 0) return LineCtx::AfterAs;
  /* Trailing ASK … (optionally after SPEAK) before WITH → function names. */
  {
    size_t askPos = std::string::npos;
    for (size_t i = 0; i + 3 <= U.size(); ++i) {
      if (U.compare(i, 3, "ASK") != 0) continue;
      bool leftOk = (i == 0 || !std::isalnum((unsigned char)U[i - 1]));
      bool rightOk = (i + 3 >= U.size() || !std::isalnum((unsigned char)U[i + 3]));
      if (leftOk && rightOk) askPos = i;
    }
    auto withPos = U.rfind(" WITH");
    if (askPos != std::string::npos) {
      std::string afterAsk =
          askPos + 3 < U.size() ? trimCopy(U.substr(askPos + 3)) : std::string();
      if (withPos == std::string::npos || withPos < askPos) {
        if (afterAsk.empty() || afterAsk.find(' ') == std::string::npos) return LineCtx::AfterAsk;
      } else {
        return LineCtx::AfterWith;
      }
    }
  }
  if (U.rfind("SPEAK ", 0) == 0 || U.rfind("GIVE BACK ", 0) == 0 ||
      U.find(" SET TO ") != std::string::npos || U.rfind("CHANGE ", 0) == 0 ||
      U.rfind("IF ", 0) == 0 || U.rfind("WHILE ", 0) == 0 || U.rfind("MAKE SURE ", 0) == 0 ||
      U.rfind("THE ", 0) == 0)
    return LineCtx::InExpr;
  if (U.find(' ') == std::string::npos) return LineCtx::StatementStart;
  return LineCtx::Other;
}

std::string hoverMarkdown(const Symbol &s) {
  std::ostringstream md;
  md << "**" << s.name << "**";
  if (!s.typeName.empty())
    md << " `" << s.typeName << "`";
  md << " *(" << s.kind << ")*\n\n";
  if (!s.signature.empty())
    md << "```luke\n" << s.signature << "\n```\n\n";
  else if (s.kind == "variable" || s.kind == "cell" || s.kind == "parameter") {
    md << "```luke\n" << s.name;
    if (!s.typeName.empty()) md << " AS " << s.typeName;
    md << "\n```\n\n";
  }
  auto doc = symbolDoc(s);
  if (!doc.empty()) md << doc;
  return md.str();
}

int lspSymbolKind(const std::string &kind) {
  if (kind == "function") return 12; /* Function */
  if (kind == "class") return 5;
  if (kind == "cell" || kind == "derived") return 13; /* Variable-ish → Variable=13 */
  if (kind == "parameter") return 6;                 /* Variable? Method=6 — use Variable 13 */
  return 13;
}

int completionKind(const std::string &kind) {
  if (kind == "function") return 3;
  if (kind == "keyword" || kind == "type") return 14;
  if (kind == "operator") return 24;
  if (kind == "class") return 7;
  return 6;
}

std::string rangeJson(int sl, int sc, int el, int ec) {
  std::ostringstream o;
  o << "{\"start\":{\"line\":" << sl << ",\"character\":" << sc << "},\"end\":{\"line\":" << el
    << ",\"character\":" << ec << "}}";
  return o.str();
}

}  // namespace

int runLspStdio(const BuildOptions &baseOpts) {
  BuildOptions opts = baseOpts;
  std::map<std::string, std::string> docs;
  std::map<std::string, int> versions;

  for (;;) {
    std::string msg = readMessage();
    if (msg.empty()) break;
    std::string method = extractMethod(msg);
    std::string id = extractId(msg);

    if (method == "initialize") {
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id
           << ",\"result\":{\"capabilities\":{"
           << "\"textDocumentSync\":{\"openClose\":true,\"change\":1},"
           << "\"hoverProvider\":true,"
           << "\"definitionProvider\":true,"
           << "\"referencesProvider\":true,"
           << "\"renameProvider\":true,"
           << "\"documentSymbolProvider\":true,"
           << "\"signatureHelpProvider\":{\"triggerCharacters\":[\" \",\",\"]},"
           << "\"completionProvider\":{\"triggerCharacters\":[\".\",\" \"],\"resolveProvider\":false},"
           << "\"documentFormattingProvider\":true,"
           << "\"codeActionProvider\":{\"codeActionKinds\":[\"quickfix\",\"source.fixAll\"]},"
           << "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"keyword\",\"function\","
              "\"variable\",\"class\",\"string\",\"number\",\"type\",\"parameter\"],"
              "\"tokenModifiers\":[\"declaration\",\"definition\"]},\"full\":true,\"range\":false}"
           << "},\"serverInfo\":{\"name\":\"lukelang\",\"version\":\"0.3\"}}}";
      writeMessage(body.str());
    } else if (method == "initialized" || method == "shutdown") {
      if (method == "shutdown") {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":null}";
        writeMessage(body.str());
      }
    } else if (method == "exit") {
      break;
    } else if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
      std::string uri = extractUri(msg);
      std::string text = extractText(msg);
      if (!uri.empty()) {
        if (!text.empty() || method == "textDocument/didOpen") {
          docs[uri] = text;
          versions[uri] = versions.count(uri) ? versions[uri] + 1 : 1;
        }
        publishDiagnostics(uri, docs[uri], opts);
      }
    } else if (method == "textDocument/didClose") {
      std::string uri = extractUri(msg);
      if (!uri.empty()) {
        docs.erase(uri);
        versions.erase(uri);
        std::ostringstream note;
        note << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
             << "\"uri\":\"" << jsonEscape(uri) << "\",\"diagnostics\":[]}}";
        writeMessage(note.str());
      }
    } else if (method == "textDocument/hover") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::string contents;
      if (line >= 0 && line < (int)lines.size()) {
        auto word = wordAt(lines[line], character);
        auto syms = scanSymbols(src);
        for (auto &s : syms) {
          if (s.name == word) {
            contents = hoverMarkdown(s);
            break;
          }
        }
        if (contents.empty() && !word.empty()) {
          auto wU = toUpperCopy(word);
          for (int i = 0; kTypeKeywords[i]; ++i) {
            if (wU == kTypeKeywords[i]) {
              contents = "**" + word + "** *(type)*\n\nLuke type keyword.";
              break;
            }
          }
          if (contents.empty()) {
            for (int i = 0; kExprKeywords[i]; ++i) {
              if (wU == kExprKeywords[i]) {
                contents = "**" + word + "** *(operator)*\n\nExpression operator.";
                break;
              }
            }
          }
          if (contents.empty()) {
            for (int i = 0; kStmtKeywords[i]; ++i) {
              if (wU == kStmtKeywords[i]) {
                contents = "**" + word + "** *(keyword)*\n\nStatement keyword.";
                break;
              }
            }
          }
        }
        if (contents.empty() && !word.empty()) {
          auto formatted = formatExpr(word);
          if (formatted != word || tokenizeExpr(word).size() > 1)
            contents = "expr `" + formatted + "`";
        }
      }
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":";
      if (contents.empty())
        body << "null";
      else
        body << "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" << jsonEscape(contents)
             << "\"}}";
      body << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/definition") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":";
      bool found = false;
      if (line >= 0 && line < (int)lines.size()) {
        auto word = wordAt(lines[line], character);
        for (auto &s : scanSymbols(src)) {
          if (s.name == word && s.kind != "parameter") {
            body << "{\"uri\":\"" << jsonEscape(uri) << "\",\"range\":"
                 << rangeJson(s.line, s.character, s.line, s.endCharacter) << "}";
            found = true;
            break;
          }
        }
      }
      if (!found) body << "null";
      body << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/references") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":[";
      bool first = true;
      if (line >= 0 && line < (int)lines.size()) {
        auto word = wordAt(lines[line], character);
        if (!word.empty()) {
          for (int i = 0; i < (int)lines.size(); ++i) {
            const auto &L = lines[i];
            for (size_t p = 0; p < L.size();) {
              if (!(isalnum((unsigned char)L[p]) || L[p] == '_')) {
                ++p;
                continue;
              }
              size_t e = p;
              while (e < L.size() && (isalnum((unsigned char)L[e]) || L[e] == '_')) ++e;
              auto tok = L.substr(p, e - p);
              if (tok == word) {
                if (!first) body << ",";
                first = false;
                body << "{\"uri\":\"" << jsonEscape(uri) << "\",\"range\":"
                     << rangeJson(i, (int)p, i, (int)e) << "}";
              }
              p = e;
            }
          }
        }
      }
      body << "]}";
      writeMessage(body.str());
    } else if (method == "textDocument/rename") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string newName = {};
      {
        auto pos = msg.find("\"newName\"");
        if (pos != std::string::npos) {
          auto q = msg.find('"', pos + 9);
          if (q != std::string::npos) {
            auto e = msg.find('"', q + 1);
            if (e != std::string::npos) newName = msg.substr(q + 1, e - q - 1);
          }
        }
      }
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::ostringstream edits;
      edits << "[";
      bool first = true;
      std::string word;
      if (line >= 0 && line < (int)lines.size()) word = wordAt(lines[line], character);
      if (!word.empty() && !newName.empty()) {
        for (int i = 0; i < (int)lines.size(); ++i) {
          const auto &L = lines[i];
          for (size_t p = 0; p < L.size();) {
            if (!(isalnum((unsigned char)L[p]) || L[p] == '_')) {
              ++p;
              continue;
            }
            size_t e = p;
            while (e < L.size() && (isalnum((unsigned char)L[e]) || L[e] == '_')) ++e;
            if (L.substr(p, e - p) == word) {
              if (!first) edits << ",";
              first = false;
              edits << "{\"range\":" << rangeJson(i, (int)p, i, (int)e) << ",\"newText\":\""
                    << jsonEscape(newName) << "\"}";
            }
            p = e;
          }
        }
      }
      edits << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":{\"changes\":{\""
           << jsonEscape(uri) << "\":" << edits.str() << "}}}";
      writeMessage(body.str());
    } else if (method == "textDocument/documentSymbol") {
      std::string uri = extractUri(msg);
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto syms = scanSymbols(src);
      std::ostringstream arr;
      arr << "[";
      bool first = true;
      for (auto &s : syms) {
        if (s.kind == "parameter") continue;
        if (!first) arr << ",";
        first = false;
        arr << "{\"name\":\"" << jsonEscape(s.name) << "\",\"detail\":\""
            << jsonEscape(s.signature.empty() ? s.typeName : s.signature)
            << "\",\"kind\":" << lspSymbolKind(s.kind) << ",\"range\":"
            << rangeJson(s.line, 0, s.endLine, 200) << ",\"selectionRange\":"
            << rangeJson(s.line, s.character, s.line, s.endCharacter) << "}";
      }
      arr << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << arr.str() << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/signatureHelp") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":";
      bool ok = false;
      if (line >= 0 && line < (int)lines.size()) {
        auto L = lines[line].substr(0, std::min(character, (int)lines[line].size()));
        auto U = toUpperCopy(L);
        auto ask = U.rfind("ASK ");
        if (ask != std::string::npos) {
          auto after = trimCopy(L.substr(ask + 4));
          auto sp = after.find(' ');
          auto fname = sp == std::string::npos ? after : after.substr(0, sp);
          int activeParam = 0;
          auto withPos = U.find(" WITH ", ask);
          if (withPos != std::string::npos) {
            auto args = L.substr(withPos + 6);
            activeParam = 0;
            for (char c : args)
              if (c == ',') ++activeParam;
          }
          for (auto &s : scanSymbols(src)) {
            if (s.kind == "function" && s.name == fname) {
              body << "{\"signatures\":[{\"label\":\"" << jsonEscape(s.signature) << "\",\"documentation\":\""
                   << jsonEscape(symbolDoc(s)) << "\",\"parameters\":[";
              /* Split params from aux on the Function stmt — recover from signature. */
              auto with = s.signature.find(" WITH ");
              auto gives = s.signature.find(" GIVES BACK ");
              std::string params;
              if (with != std::string::npos) {
                size_t start = with + 6;
                size_t end = gives == std::string::npos ? s.signature.size() : gives;
                params = s.signature.substr(start, end - start);
              }
              bool pf = true;
              size_t st = 0;
              while (st <= params.size()) {
                auto c = params.find(',', st);
                auto piece = trimCopy(params.substr(st, c == std::string::npos ? std::string::npos
                                                                              : c - st));
                if (!piece.empty()) {
                  if (!pf) body << ",";
                  pf = false;
                  body << "{\"label\":\"" << jsonEscape(piece) << "\"}";
                }
                if (c == std::string::npos) break;
                st = c + 1;
              }
              body << "]}],\"activeSignature\":0,\"activeParameter\":" << activeParam << "}";
              ok = true;
              break;
            }
          }
        }
      }
      if (!ok) body << "null";
      body << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/completion") {
      std::string uri = extractUri(msg);
      int line = extractPositionField(msg, "\"line\"");
      int character = extractPositionField(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::string prefix;
      std::string lineText;
      if (line >= 0 && line < (int)lines.size()) {
        lineText = lines[line];
        prefix = completionPrefixAt(lineText, character);
      }
      auto pU = toUpperCopy(prefix);
      LineCtx ctx = classifyLineContext(lineText, character);
      /* Cursor on/just after ASK → complete function names, not the keyword. */
      {
        auto ident = toUpperCopy(wordAt(lineText, character));
        if (ident == "ASK") {
          ctx = LineCtx::AfterAsk;
          prefix.clear();
          pU.clear();
        } else if (ident == "AS") {
          ctx = LineCtx::AfterAs;
          prefix.clear();
          pU.clear();
        } else if (pU == "ASK" || pU == "AS" || pU == "WITH" || pU == "SPEAK" || pU == "SET" ||
                   pU == "TO" || pU == "IS" || pU == "THE" || pU == "MY" || pU == "AND") {
          prefix.clear();
          pU.clear();
        }
      }
      std::ostringstream items;
      items << "[";
      bool first = true;
      auto addItem = [&](const std::string &label, const std::string &detail, int kind,
                         const std::string &insert = {}) {
        if (!prefix.empty()) {
          auto lU = toUpperCopy(label);
          if (lU.rfind(pU, 0) != 0) return;
        }
        if (!first) items << ",";
        first = false;
        items << "{\"label\":\"" << jsonEscape(label) << "\",\"kind\":" << kind
              << ",\"detail\":\"" << jsonEscape(detail) << "\"";
        if (!insert.empty())
          items << ",\"insertText\":\"" << jsonEscape(insert) << "\"";
        items << "}";
      };

      auto syms = scanSymbols(src);
      if (ctx == LineCtx::AfterAs) {
        for (int i = 0; kTypeKeywords[i]; ++i) addItem(kTypeKeywords[i], "type", 25);
      } else if (ctx == LineCtx::AfterAsk) {
        for (auto &s : syms)
          if (s.kind == "function") {
            std::string ins = s.name;
            if (s.arity > 0) ins += " WITH ";
            addItem(s.name, s.signature.empty() ? "function" : s.signature, 3, ins);
          }
      } else if (ctx == LineCtx::AfterWith || ctx == LineCtx::InExpr) {
        for (auto &s : syms) {
          if (s.kind == "parameter" || s.kind == "variable" || s.kind == "cell" ||
              s.kind == "derived")
            addItem(s.name,
                    (s.typeName.empty() ? s.kind : s.kind + " · " + s.typeName),
                    completionKind(s.kind));
          if (s.kind == "function")
            addItem(s.name, s.signature.empty() ? "function" : s.signature + " · arity " +
                                                                   std::to_string(std::max(0, s.arity)),
                    3, "ASK " + s.name + (s.arity > 0 ? " WITH " : ""));
        }
        for (int i = 0; kExprKeywords[i]; ++i) addItem(kExprKeywords[i], "operator", 24);
      } else {
        /* Statement-start / other: keywords by position + in-scope symbols */
        for (int i = 0; kStmtKeywords[i]; ++i) addItem(kStmtKeywords[i], "keyword", 14);
        for (int i = 0; kTypeKeywords[i]; ++i) addItem(kTypeKeywords[i], "type", 25);
        for (auto &s : syms) {
          if (s.kind == "parameter") continue;
          std::string detail = s.kind;
          if (!s.typeName.empty()) detail += " · " + s.typeName;
          if (s.kind == "function" && s.arity >= 0)
            detail += " · arity " + std::to_string(s.arity);
          addItem(s.name, detail, completionKind(s.kind));
        }
      }
      items << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":{\"isIncomplete\":false,\"items\":"
           << items.str() << "}}";
      writeMessage(body.str());
    } else if (method == "textDocument/formatting") {
      std::string uri = extractUri(msg);
      std::string src = docs.count(uri) ? docs[uri] : "";
      std::string formatted = formatLukeSource(src);
      auto lines = splitLines(src);
      int lastLine = lines.empty() ? 0 : (int)lines.size() - 1;
      int lastChar = lines.empty() ? 0 : (int)lines.back().size();
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":[{\"range\":"
           << rangeJson(0, 0, lastLine, lastChar) << ",\"newText\":\"" << jsonEscape(formatted)
           << "\"}]}";
      writeMessage(body.str());
    } else if (method == "textDocument/codeAction") {
      std::string uri = extractUri(msg);
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto diags = collectDiags(src, opts);
      std::ostringstream arr;
      arr << "[";
      bool first = true;
      auto addAction = [&](const std::string &title, const std::string &kind,
                           const std::string &editJson) {
        if (!first) arr << ",";
        first = false;
        arr << "{\"title\":\"" << jsonEscape(title) << "\",\"kind\":\"" << kind << "\","
            << "\"edit\":{\"changes\":{\"" << jsonEscape(uri) << "\":" << editJson << "}}}";
      };
      /* Always offer format-all as a source action. */
      {
        std::string formatted = formatLukeSource(src);
        auto lines = splitLines(src);
        int lastLine = lines.empty() ? 0 : (int)lines.size() - 1;
        int lastChar = lines.empty() ? 0 : (int)lines.back().size();
        std::ostringstream edits;
        edits << "[{\"range\":" << rangeJson(0, 0, lastLine, lastChar) << ",\"newText\":\""
              << jsonEscape(formatted) << "\"}]";
        addAction("Format document", "source.fixAll", edits.str());
      }
      for (auto &d : diags) {
        auto U = toUpperCopy(d.message);
        if (U.find("UNKNOWN TYPE") != std::string::npos) {
          auto lines = splitLines(src);
          if (d.line >= 0 && d.line < (int)lines.size()) {
            auto L = lines[d.line];
            auto asPos = toUpperCopy(L).find(" AS ");
            if (asPos != std::string::npos) {
              size_t typeStart = asPos + 4;
              size_t typeEnd = typeStart;
              while (typeEnd < L.size() && !isspace((unsigned char)L[typeEnd]) && L[typeEnd] != ',')
                ++typeEnd;
              std::ostringstream edits;
              edits << "[{\"range\":" << rangeJson(d.line, (int)typeStart, d.line, (int)typeEnd)
                    << ",\"newText\":\"NUMBER\"}]";
              addAction("Replace type with NUMBER", "quickfix", edits.str());
            }
          }
        }
      }
      arr << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << arr.str() << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/semanticTokens/full") {
      std::string uri = extractUri(msg);
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      auto syms = scanSymbols(src);
      /* tokenTypes: 0 keyword, 1 function, 2 variable, 3 class, 4 string, 5 number, 6 type, 7 parameter */
      std::vector<int> data;
      int prevLine = 0, prevStart = 0;
      auto pushTok = [&](int line, int start, int len, int type, int mod) {
        if (line < prevLine || (line == prevLine && start < prevStart)) return;
        data.push_back(line - prevLine);
        data.push_back(line == prevLine ? start - prevStart : start);
        data.push_back(len);
        data.push_back(type);
        data.push_back(mod);
        prevLine = line;
        prevStart = start;
      };
      struct Tok {
        int line, start, len, type, mod;
      };
      std::vector<Tok> toks;
      for (int i = 0; i < (int)lines.size(); ++i) {
        const auto &L = lines[i];
        for (size_t p = 0; p < L.size();) {
          if (L[p] == '"') {
            size_t e = p + 1;
            while (e < L.size() && L[e] != '"') {
              if (L[e] == '\\' && e + 1 < L.size()) e += 2;
              else ++e;
            }
            if (e < L.size()) ++e;
            toks.push_back({i, (int)p, (int)(e - p), 4, 0});
            p = e;
            continue;
          }
          if (isdigit((unsigned char)L[p]) ||
              (L[p] == '-' && p + 1 < L.size() && isdigit((unsigned char)L[p + 1]))) {
            size_t e = p + (L[p] == '-' ? 1 : 0);
            while (e < L.size() && (isdigit((unsigned char)L[e]) || L[e] == '.')) ++e;
            toks.push_back({i, (int)p, (int)(e - p), 5, 0});
            p = e;
            continue;
          }
          if (isalnum((unsigned char)L[p]) || L[p] == '_') {
            size_t e = p;
            while (e < L.size() && (isalnum((unsigned char)L[e]) || L[e] == '_')) ++e;
            auto tok = L.substr(p, e - p);
            auto u = toUpperCopy(tok);
            int type = -1, mod = 0;
            for (int k = 0; kTypeKeywords[k]; ++k)
              if (u == kTypeKeywords[k]) type = 6;
            for (int k = 0; type < 0 && kStmtKeywords[k]; ++k)
              if (u == kStmtKeywords[k]) type = 0;
            for (int k = 0; type < 0 && kExprKeywords[k]; ++k)
              if (u == kExprKeywords[k]) type = 0;
            for (auto &s : syms) {
              if (s.name != tok) continue;
              if (s.kind == "function") {
                type = 1;
                if (s.line == i) mod = 3; /* declaration|definition bits */
              } else if (s.kind == "class")
                type = 3;
              else if (s.kind == "parameter")
                type = 7;
              else
                type = 2;
              break;
            }
            if (type >= 0) toks.push_back({i, (int)p, (int)(e - p), type, mod});
            p = e;
            continue;
          }
          ++p;
        }
      }
      std::sort(toks.begin(), toks.end(), [](const Tok &a, const Tok &b) {
        if (a.line != b.line) return a.line < b.line;
        return a.start < b.start;
      });
      for (auto &t : toks) pushTok(t.line, t.start, t.len, t.type, t.mod);
      std::ostringstream arr;
      arr << "[";
      for (size_t i = 0; i < data.size(); ++i) {
        if (i) arr << ",";
        arr << data[i];
      }
      arr << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":{\"data\":" << arr.str() << "}}";
      writeMessage(body.str());
    } else if (!id.empty() && id != "null" && method.find('/') != std::string::npos) {
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":null}";
      writeMessage(body.str());
    }
  }
  return 0;
}

}  // namespace luke
