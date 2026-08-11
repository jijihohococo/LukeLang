#include "luke/build.hpp"
#include "luke_expr.hpp"

#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <regex>
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
  auto pos = msg.find("\"method\"");
  if (pos == std::string::npos) return {};
  pos = msg.find('"', pos + 8);
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

struct Symbol {
  std::string name;
  std::string kind; /* variable | function | cell */
  int line = 0;     /* 0-based */
  int character = 0;
};

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

std::vector<Symbol> scanSymbols(const std::string &source) {
  std::vector<Symbol> out;
  auto lines = splitLines(source);
  for (size_t i = 0; i < lines.size(); ++i) {
    auto t = lines[i];
    while (!t.empty() && isspace((unsigned char)t[0])) t.erase(t.begin());
    auto U = toUpperCopy(t);
    auto takeName = [&](size_t start) -> std::string {
      while (start < t.size() && isspace((unsigned char)t[start])) ++start;
      size_t end = start;
      while (end < t.size() && (isalnum((unsigned char)t[end]) || t[end] == '_')) ++end;
      return t.substr(start, end - start);
    };
    if (U.rfind("MY NAME IS ", 0) == 0) {
      auto n = takeName(11);
      if (!n.empty()) out.push_back({n, "variable", (int)i, 11});
    } else if (U.rfind("REMEMBER ", 0) == 0) {
      auto n = takeName(9);
      if (!n.empty()) out.push_back({n, "cell", (int)i, 9});
    } else if (U.rfind("THIS IS FUNCTION ", 0) == 0) {
      auto n = takeName(17);
      if (!n.empty()) out.push_back({n, "function", (int)i, 17});
    } else if (U.rfind("WATCH ", 0) == 0) {
      auto n = takeName(6);
      if (!n.empty()) out.push_back({n, "cell", (int)i, 6});
    }
  }
  return out;
}

std::string wordAt(const std::string &line, int character, int *startOut = nullptr) {
  if (character < 0) character = 0;
  if (character > (int)line.size()) character = (int)line.size();
  int s = character;
  while (s > 0 && (isalnum((unsigned char)line[s - 1]) || line[s - 1] == '_')) --s;
  int e = character;
  while (e < (int)line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
  if (startOut) *startOut = s;
  return line.substr(s, e - s);
}

void publishDiagnostics(const std::string &uri, const std::string &source,
                        const BuildOptions &opts) {
  BuildResult r = analyzeLukeBuild(source, opts);
  std::ostringstream diags;
  diags << "[";
  if (!r.ok && !r.error.empty()) {
    int line = 0;
    std::smatch m;
    std::regex re("line ([0-9]+)");
    if (std::regex_search(r.error, m, re)) line = std::stoi(m[1].str());
    if (line > 0) line -= 1;
    if (line < 0) line = 0;
    diags << "{\"range\":{\"start\":{\"line\":" << line
          << ",\"character\":0},\"end\":{\"line\":" << line
          << ",\"character\":200}},\"severity\":1,\"source\":\"lukelang\",\"message\":\""
          << jsonEscape(r.error) << "\"}";
  }
  diags << "]";
  std::ostringstream note;
  note << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
       << "\"uri\":\"" << jsonEscape(uri) << "\",\"diagnostics\":" << diags.str() << "}}";
  writeMessage(note.str());
}

const char *kKeywords[] = {
    "MY", "NAME", "IS", "SET", "TO", "AS", "NUMBER", "INTEGER", "TEXT", "FLAG", "LIST", "MAP",
    "SPEAK", "ASK", "WITH", "ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "AND", "EQUALS",
    "IS LESS THAN", "IS GREATER THAN", "REMEMBER", "WATCH", "PUSH", "WHEN", "IF", "DO", "END",
    "WHILE", "FOR", "IMPORT", "GIVE", "BACK", "TRUE", "FALSE", "MAKE", "SURE", nullptr};

}  // namespace

int runLspStdio(const BuildOptions &baseOpts) {
  BuildOptions opts = baseOpts;
  std::map<std::string, std::string> docs;
  for (;;) {
    std::string msg = readMessage();
    if (msg.empty()) break;
    std::string method = extractMethod(msg);
    std::string id = extractId(msg);
    if (method == "initialize") {
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id
           << ",\"result\":{\"capabilities\":{"
           << "\"textDocumentSync\":1,"
           << "\"hoverProvider\":true,"
           << "\"definitionProvider\":true,"
           << "\"completionProvider\":{\"triggerCharacters\":[\".\",\" \"]}"
           << "},\"serverInfo\":{\"name\":\"lukelang\",\"version\":\"0.2\"}}}";
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
        if (!text.empty() || method == "textDocument/didOpen") docs[uri] = text;
        publishDiagnostics(uri, docs[uri], opts);
      }
    } else if (method == "textDocument/hover") {
      std::string uri = extractUri(msg);
      int line = extractIntAfter(msg, "\"line\"");
      int character = extractIntAfter(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::string contents;
      if (line >= 0 && line < (int)lines.size()) {
        auto word = wordAt(lines[line], character);
        auto syms = scanSymbols(src);
        for (auto &s : syms) {
          if (s.name == word) {
            contents = "**" + s.name + "** (" + s.kind + ")";
            break;
          }
        }
        if (contents.empty() && !word.empty()) {
          auto toks = tokenizeExpr(word);
          if (!toks.empty() && toks[0].kind != TokKind::End) {
            ExprLower ctx;
            ctx.fail = nullptr;
            ctx.resolve = [](const std::string &atom, size_t) {
              return std::make_pair(atom, std::string("num"));
            };
            auto formatted = formatExpr(word);
            contents = "expr `" + formatted + "`";
          }
        }
        if (contents.empty()) {
          for (int i = 0; kKeywords[i]; ++i) {
            if (toUpperCopy(word) == toUpperCopy(kKeywords[i]) ||
                toUpperCopy(word) == "ADD" || toUpperCopy(word) == "WATCH") {
              contents = "Luke keyword `" + word + "`";
              break;
            }
          }
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
      int line = extractIntAfter(msg, "\"line\"");
      int character = extractIntAfter(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":";
      bool found = false;
      if (line >= 0 && line < (int)lines.size()) {
        auto word = wordAt(lines[line], character);
        for (auto &s : scanSymbols(src)) {
          if (s.name == word) {
            body << "{\"uri\":\"" << jsonEscape(uri) << "\",\"range\":{\"start\":{\"line\":"
                 << s.line << ",\"character\":" << s.character
                 << "},\"end\":{\"line\":" << s.line << ",\"character\":"
                 << (s.character + (int)s.name.size()) << "}}}";
            found = true;
            break;
          }
        }
      }
      if (!found) body << "null";
      body << "}";
      writeMessage(body.str());
    } else if (method == "textDocument/completion") {
      std::string uri = extractUri(msg);
      int line = extractIntAfter(msg, "\"line\"");
      int character = extractIntAfter(msg, "\"character\"");
      std::string src = docs.count(uri) ? docs[uri] : "";
      auto lines = splitLines(src);
      std::string prefix;
      if (line >= 0 && line < (int)lines.size()) prefix = wordAt(lines[line], character);
      auto pU = toUpperCopy(prefix);
      std::ostringstream items;
      items << "[";
      bool first = true;
      auto addItem = [&](const std::string &label, const std::string &detail, int kind) {
        if (!prefix.empty()) {
          auto lU = toUpperCopy(label);
          if (lU.rfind(pU, 0) != 0) return;
        }
        if (!first) items << ",";
        first = false;
        items << "{\"label\":\"" << jsonEscape(label) << "\",\"kind\":" << kind
              << ",\"detail\":\"" << jsonEscape(detail) << "\"}";
      };
      for (int i = 0; kKeywords[i]; ++i) addItem(kKeywords[i], "keyword", 14);
      addItem("ADD", "operator", 24);
      addItem("SUBTRACT", "operator", 24);
      addItem("MULTIPLY", "operator", 24);
      addItem("DIVIDE", "operator", 24);
      addItem("EQUALS", "operator", 24);
      for (auto &s : scanSymbols(src)) addItem(s.name, s.kind, s.kind == "function" ? 3 : 6);
      items << "]";
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << items.str() << "}";
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
