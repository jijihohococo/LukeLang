#include "luke/compiler.hpp"
#include "luke/value.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace luke {
namespace {

std::string trim(const std::string &s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string toUpper(std::string s) {
  for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool startsWithCI(const std::string &s, const std::string &prefix) {
  if (s.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(s[i])) !=
        std::toupper(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '\'';
}

struct Compiler {
  Heap &heap;
  Chunk chunk;
  std::string error;
  bool hadError = false;
  std::unordered_map<std::string, uint8_t> globalsNamed;

  explicit Compiler(Heap &h) : heap(h) {}

  void fail(std::size_t line, const std::string &msg) {
    if (hadError) return;
    hadError = true;
    std::ostringstream oss;
    oss << "Compile error on line " << line << ": " << msg;
    error = oss.str();
  }

  uint8_t makeConstant(Value v, std::size_t line) {
    std::size_t idx = chunk.addConstant(v);
    if (idx > 255) {
      fail(line, "Too many constants.");
      return 0;
    }
    return static_cast<uint8_t>(idx);
  }

  uint8_t identifierConstant(const std::string &name, std::size_t line) {
    return makeConstant(Value::object(heap.allocateString(name)), line);
  }

  void emit(Op op, std::size_t line) { chunk.write(op, line); }
  void emitByte(uint8_t b, std::size_t line) { chunk.writeByte(b, line); }

  void emitConstant(Value v, std::size_t line) {
    emit(Op::Constant, line);
    emitByte(makeConstant(v, line), line);
  }

  // Very small expression compiler for Luke conversational ops and literals.
  void compileExpression(std::string expr, std::size_t line) {
    expr = trim(expr);
    if (expr.empty()) {
      emit(Op::Nil, line);
      return;
    }

    // Comparisons (lowest precedence among binary we handle)
    if (compileBinaryPhrase(expr, line, " IS GREATER THAN ", Op::Greater)) return;
    if (compileBinaryPhrase(expr, line, " IS LESS THAN ", Op::Less)) return;
    if (compileBinaryPhrase(expr, line, " EQUALS ", Op::Equal)) return;
    if (compileBinaryPhrase(expr, line, " IS EQUAL TO ", Op::Equal)) return;

    // ADD / SUBTRACT / MULTIPLY / DIVIDE / AND(concat)
    if (compileBinaryPhrase(expr, line, " ADD ", Op::Add)) return;
    if (startsWithCI(expr, "ADD ")) {
      // ADD a AND b
      auto rest = trim(expr.substr(4));
      auto andPos = toUpper(rest).find(" AND ");
      if (andPos != std::string::npos) {
        compileExpression(trim(rest.substr(0, andPos)), line);
        compileExpression(trim(rest.substr(andPos + 5)), line);
        emit(Op::Add, line);
        return;
      }
    }
    if (compileBinaryPhrase(expr, line, " SUBTRACT ", Op::Subtract)) return;
    if (startsWithCI(expr, "SUBTRACT ")) {
      auto rest = trim(expr.substr(9));
      auto andPos = toUpper(rest).find(" AND ");
      if (andPos != std::string::npos) {
        compileExpression(trim(rest.substr(0, andPos)), line);
        compileExpression(trim(rest.substr(andPos + 5)), line);
        emit(Op::Subtract, line);
        return;
      }
    }
    if (compileBinaryPhrase(expr, line, " MULTIPLY ", Op::Multiply)) return;
    if (startsWithCI(expr, "MULTIPLY ")) {
      auto rest = trim(expr.substr(9));
      auto andPos = toUpper(rest).find(" AND ");
      if (andPos != std::string::npos) {
        compileExpression(trim(rest.substr(0, andPos)), line);
        compileExpression(trim(rest.substr(andPos + 5)), line);
        emit(Op::Multiply, line);
        return;
      }
    }
    if (compileBinaryPhrase(expr, line, " DIVIDE ", Op::Divide)) return;
    if (startsWithCI(expr, "DIVIDE ")) {
      auto rest = trim(expr.substr(7));
      auto andPos = toUpper(rest).find(" AND ");
      if (andPos != std::string::npos) {
        compileExpression(trim(rest.substr(0, andPos)), line);
        compileExpression(trim(rest.substr(andPos + 5)), line);
        emit(Op::Divide, line);
        return;
      }
    }

    // String concat via AND between terms: a AND " " AND b
    {
      auto U = toUpper(expr);
      auto pos = U.find(" AND ");
      if (pos != std::string::npos) {
        compileExpression(trim(expr.substr(0, pos)), line);
        compileExpression(trim(expr.substr(pos + 5)), line);
        emit(Op::Add, line);
        return;
      }
    }

    // NOT
    if (startsWithCI(expr, "NOT ")) {
      compileExpression(trim(expr.substr(4)), line);
      emit(Op::Not, line);
      return;
    }

    // MAKE LIST WITH ...
    if (startsWithCI(expr, "MAKE LIST WITH ")) {
      auto rest = trim(expr.substr(15));
      auto parts = splitArgs(rest);
      for (auto &p : parts) compileExpression(p, line);
      emit(Op::MakeArray, line);
      emitByte(static_cast<uint8_t>(parts.size()), line);
      return;
    }

    // ITEM AT i OF list
    if (startsWithCI(expr, "ITEM AT ")) {
      auto rest = trim(expr.substr(8));
      auto U = toUpper(rest);
      auto ofPos = U.find(" OF ");
      if (ofPos != std::string::npos) {
        auto idx = trim(rest.substr(0, ofPos));
        auto list = trim(rest.substr(ofPos + 4));
        compileExpression(list, line);
        compileExpression(idx, line);
        emit(Op::GetIndex, line);
        return;
      }
    }

    // Literals / variables
    compilePrimary(expr, line);
  }

  bool compileBinaryPhrase(const std::string &expr, std::size_t line, const std::string &opWord,
                           Op op) {
    auto U = toUpper(expr);
    auto needle = toUpper(opWord);
    auto pos = U.find(needle);
    if (pos == std::string::npos) return false;
    compileExpression(trim(expr.substr(0, pos)), line);
    compileExpression(trim(expr.substr(pos + needle.size())), line);
    emit(op, line);
    return true;
  }

  std::vector<std::string> splitArgs(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    char q = 0;
    for (char c : s) {
      if (inQuote) {
        cur.push_back(c);
        if (c == q) inQuote = false;
        continue;
      }
      if (c == '"' || c == '\'') {
        inQuote = true;
        q = c;
        cur.push_back(c);
        continue;
      }
      if (c == ',') {
        out.push_back(trim(cur));
        cur.clear();
        continue;
      }
      cur.push_back(c);
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
  }

  void compilePrimary(std::string expr, std::size_t line) {
    expr = trim(expr);

    if (expr.size() >= 2 &&
        ((expr.front() == '"' && expr.back() == '"') ||
         (expr.front() == '\'' && expr.back() == '\''))) {
      std::string raw = expr.substr(1, expr.size() - 2);
      emitConstant(Value::object(heap.allocateString(raw)), line);
      return;
    }

    // Bare multi-word phrase => string (Luke SPEAK style)
    bool allWord = !expr.empty();
    for (char c : expr) {
      if (!(std::isalpha(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c)))) {
        allWord = false;
        break;
      }
    }
    if (allWord && expr.find(' ') != std::string::npos) {
      emitConstant(Value::object(heap.allocateString(expr)), line);
      return;
    }

    std::string U = toUpper(expr);
    if (U == "TRUE" || U == "YES") {
      emit(Op::True, line);
      return;
    }
    if (U == "FALSE" || U == "NO") {
      emit(Op::False, line);
      return;
    }
    if (U == "NIL" || U == "NOTHING" || U == "NONE") {
      emit(Op::Nil, line);
      return;
    }

    // Number?
    char *end = nullptr;
    double num = std::strtod(expr.c_str(), &end);
    if (end && end != expr.c_str() && *end == '\0') {
      emitConstant(Value::number(num), line);
      return;
    }

    // Identifier / variable
    bool ident = !expr.empty();
    for (char c : expr) {
      if (!isIdentChar(c)) {
        ident = false;
        break;
      }
    }
    if (ident) {
      emit(Op::GetGlobal, line);
      emitByte(identifierConstant(expr, line), line);
      return;
    }

    fail(line, "Cannot parse expression: " + expr);
  }

  void compileLine(const std::string &raw, std::size_t line) {
    std::string text = trim(raw);
    if (text.empty()) return;
    if (startsWithCI(text, "//")) return;

    // Optional program markers
    if (startsWithCI(text, "LET'S START") || startsWithCI(text, "LETS START") ||
        startsWithCI(text, "GET OUTTA HERE")) {
      return;
    }

    if (startsWithCI(text, "SPEAK ") || startsWithCI(text, "SAY ") || startsWithCI(text, "YELL ") ||
        startsWithCI(text, "SHOUT ")) {
      auto space = text.find(' ');
      compileExpression(trim(text.substr(space + 1)), line);
      emit(Op::Print, line);
      return;
    }

    // MY NAME IS x SET TO expr   OR   MY NAME IS x
    if (startsWithCI(text, "MY NAME IS ")) {
      auto rest = trim(text.substr(11));
      auto U = toUpper(rest);
      auto setPos = U.find(" SET TO ");
      std::string name;
      std::string valueExpr;
      if (setPos == std::string::npos) {
        name = rest;
        emit(Op::Nil, line);
      } else {
        name = trim(rest.substr(0, setPos));
        valueExpr = trim(rest.substr(setPos + 8));
        compileExpression(valueExpr, line);
      }
      emit(Op::SetGlobal, line);
      emitByte(identifierConstant(name, line), line);
      emit(Op::Pop, line);
      return;
    }

    // SET name TO expr
    if (startsWithCI(text, "SET ")) {
      auto rest = trim(text.substr(4));
      auto U = toUpper(rest);
      auto toPos = U.find(" TO ");
      if (toPos == std::string::npos) {
        fail(line, "Expected SET name TO value");
        return;
      }
      std::string name = trim(rest.substr(0, toPos));
      std::string valueExpr = trim(rest.substr(toPos + 4));
      compileExpression(valueExpr, line);
      emit(Op::SetGlobal, line);
      emitByte(identifierConstant(name, line), line);
      emit(Op::Pop, line);
      return;
    }

    // IF condition DO ... END IF
    // Bytecode shape:
    //   condition; JumpIfFalse else; Pop; <body>; Jump end; else: Pop; end:
    if (startsWithCI(text, "IF ")) {
      auto rest = trim(text.substr(3));
      auto U = toUpper(rest);
      auto doPos = U.rfind(" DO");
      if (doPos != std::string::npos && doPos + 3 == U.size()) {
        rest = trim(rest.substr(0, doPos));
      }
      blockStack_.push_back({"IF", 0, 0});
      compileExpression(rest, line);
      int jump = chunk.writeJump(Op::JumpIfFalse, line);
      emit(Op::Pop, line);
      blockStack_.back().jump = jump;
      return;
    }

    if (toUpper(text) == "END IF" || toUpper(text) == "ENDIF") {
      if (blockStack_.empty() || blockStack_.back().type != "IF") {
        fail(line, "END IF without matching IF");
        return;
      }
      // Skip the falsy-path Pop when the body ran.
      int endJump = chunk.writeJump(Op::Jump, line);
      chunk.patchJump(blockStack_.back().jump);
      emit(Op::Pop, line);  // falsy condition still on stack
      chunk.patchJump(endJump);
      blockStack_.pop_back();
      return;
    }

    // WHILE condition DO ... END WHILE
    if (startsWithCI(text, "WHILE ")) {
      auto rest = trim(text.substr(6));
      auto U = toUpper(rest);
      auto doPos = U.rfind(" DO");
      if (doPos != std::string::npos && doPos + 3 == U.size()) {
        rest = trim(rest.substr(0, doPos));
      }
      int loopStart = static_cast<int>(chunk.code.size());
      blockStack_.push_back({"WHILE", 0, loopStart});
      compileExpression(rest, line);
      int jump = chunk.writeJump(Op::JumpIfFalse, line);
      emit(Op::Pop, line);
      blockStack_.back().jump = jump;
      return;
    }

    if (toUpper(text) == "END WHILE" || toUpper(text) == "ENDWHILE") {
      if (blockStack_.empty() || blockStack_.back().type != "WHILE") {
        fail(line, "END WHILE without matching WHILE");
        return;
      }
      auto block = blockStack_.back();
      blockStack_.pop_back();
      emit(Op::Loop, line);
      int back = static_cast<int>(chunk.code.size()) - block.loopStart + 2;
      emitByte(static_cast<uint8_t>((back >> 8) & 0xff), line);
      emitByte(static_cast<uint8_t>(back & 0xff), line);
      chunk.patchJump(block.jump);
      emit(Op::Pop, line);  // falsy condition still on stack
      return;
    }

    fail(line, "Unknown statement: " + text);
  }

  struct Block {
    std::string type;
    int jump = 0;
    int loopStart = 0;
  };
  std::vector<Block> blockStack_;
};

}  // namespace

CompileResult compileLuke(const std::string &source, Heap &heap) {
  Compiler c(heap);
  std::istringstream in(source);
  std::string line;
  std::size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    c.compileLine(line, lineNo);
    if (c.hadError) break;
  }
  CompileResult result;
  if (c.hadError) {
    result.ok = false;
    result.error = c.error;
    return result;
  }
  if (!c.blockStack_.empty()) {
    result.ok = false;
    result.error = "Compile error: unclosed " + c.blockStack_.back().type + " block";
    return result;
  }
  c.emit(Op::Halt, lineNo ? lineNo : 1);
  result.ok = true;
  result.chunk = std::move(c.chunk);
  return result;
}

}  // namespace luke
