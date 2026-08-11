#include "luke/build.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <regex>

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
  /* Prefer textDocument.text from didOpen; fall back to empty. */
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
    if (line > 0) line -= 1; /* LSP is 0-based */
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

}  // namespace

int runLspStdio(const BuildOptions &baseOpts) {
  BuildOptions opts = baseOpts;
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
           << "\"hoverProvider\":false,"
           << "\"completionProvider\":false"
           << "},\"serverInfo\":{\"name\":\"lukelang\",\"version\":\"0.1\"}}}";
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
      if (!uri.empty()) publishDiagnostics(uri, text, opts);
    } else if (!id.empty() && id != "null" && method.find('/') != std::string::npos) {
      /* Method not implemented — empty result to keep clients calm. */
      std::ostringstream body;
      body << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":null}";
      writeMessage(body.str());
    }
  }
  return 0;
}

}  // namespace luke
