/* Syntax v2 lexer — real tokenizer (v1's statement layer was prefix-matched strings). */

#include "luke2.hpp"

#include <cctype>

namespace luke2 {
namespace {

bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool identPart(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/* Multi-character punctuation, longest first so "->" beats "-". */
const char *kPunct[] = {
    "...", "->", "..", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=",
    "(", ")", "{", "}", "[", "]", ",", ":", ";", ".", "=", "+", "-", "*", "/", "%",
    "<", ">", "!", nullptr,
};

}  // namespace

std::vector<Tok> lex(const std::string &src, std::string *err, size_t *errLine) {
  std::vector<Tok> out;
  size_t i = 0, line = 1;
  auto fail = [&](const std::string &m) {
    if (err) *err = m;
    if (errLine) *errLine = line;
  };

  while (i < src.size()) {
    char c = src[i];

    if (c == '\n') {
      Tok t;
      t.kind = Tk::Newline;
      t.line = line;
      out.push_back(t);
      ++line;
      ++i;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }

    /* comments */
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      while (i < src.size() && src[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
        if (src[i] == '\n') ++line;
        ++i;
      }
      if (i + 1 >= src.size()) {
        fail("unterminated block comment");
        return out;
      }
      i += 2;
      continue;
    }

    /* triple-quoted block: opaque, preserved byte for byte (spec §8) */
    if (c == '"' && i + 2 < src.size() && src[i + 1] == '"' && src[i + 2] == '"') {
      size_t start = i;
      size_t startLine = line;
      i += 3;
      while (i + 2 < src.size() && !(src[i] == '"' && src[i + 1] == '"' && src[i + 2] == '"')) {
        if (src[i] == '\n') ++line;
        ++i;
      }
      if (i + 2 >= src.size()) {
        fail("unterminated \"\"\" block");
        return out;
      }
      i += 3;
      Tok t;
      t.kind = Tk::Str;
      t.line = startLine;
      t.raw = src.substr(start, i - start); /* includes the triple quotes */
      t.text = t.raw;
      out.push_back(t);
      continue;
    }

    /* string literal — kept verbatim; data is never translated (spec, corpus rule 2) */
    if (c == '"') {
      size_t start = i;
      ++i;
      while (i < src.size() && src[i] != '"') {
        if (src[i] == '\\' && i + 1 < src.size()) {
          i += 2;
          continue;
        }
        if (src[i] == '\n') {
          fail("unterminated string");
          return out;
        }
        ++i;
      }
      if (i >= src.size()) {
        fail("unterminated string");
        return out;
      }
      ++i; /* closing quote */
      Tok t;
      t.kind = Tk::Str;
      t.line = line;
      t.raw = src.substr(start, i - start); /* with quotes */
      t.text = t.raw;
      out.push_back(t);
      continue;
    }

    /* number */
    if (std::isdigit((unsigned char)c)) {
      size_t start = i;
      bool isFloat = false;
      while (i < src.size() && std::isdigit((unsigned char)src[i])) ++i;
      /* ".." is a range operator, not a decimal point */
      if (i < src.size() && src[i] == '.' && !(i + 1 < src.size() && src[i + 1] == '.')) {
        if (i + 1 < src.size() && std::isdigit((unsigned char)src[i + 1])) {
          isFloat = true;
          ++i;
          while (i < src.size() && std::isdigit((unsigned char)src[i])) ++i;
        }
      }
      Tok t;
      t.kind = isFloat ? Tk::Float : Tk::Int;
      t.text = src.substr(start, i - start);
      t.line = line;
      out.push_back(t);
      continue;
    }

    /* identifier / keyword */
    if (identStart(c)) {
      size_t start = i;
      while (i < src.size() && identPart(src[i])) ++i;
      Tok t;
      t.kind = Tk::Ident;
      t.text = src.substr(start, i - start);
      t.line = line;
      out.push_back(t);
      continue;
    }

    /* punctuation */
    bool matched = false;
    for (int p = 0; kPunct[p]; ++p) {
      size_t n = std::string(kPunct[p]).size();
      if (src.compare(i, n, kPunct[p]) == 0) {
        Tok t;
        t.kind = Tk::Punct;
        t.text = kPunct[p];
        t.line = line;
        out.push_back(t);
        i += n;
        matched = true;
        break;
      }
    }
    if (matched) continue;

    fail(std::string("unexpected character '") + c + "'");
    return out;
  }

  Tok end;
  end.kind = Tk::End;
  end.line = line;
  out.push_back(end);
  return out;
}

bool isV2Path(const std::string &path) {
  if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".lk") == 0) return true;
  /* Phase 5: also true for `.luke` once conversational content has been rewritten. */
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".luke") == 0) return true;
  return false;
}

bool wantsV2(const std::string &path, SyntaxMode mode) {
  if (mode == SyntaxMode::ForceV1) return false;
  if (mode == SyntaxMode::ForceV2) return true;
  return isV2Path(path);
}

}  // namespace luke2
