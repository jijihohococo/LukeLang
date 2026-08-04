#include "luke/compiler.hpp"
#include "luke/function.hpp"
#include "luke/value.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
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

struct Local {
  std::string name;
};

struct Compiler {
  Heap &heap;
  Chunk *chunk = nullptr;
  Chunk scriptChunk;
  std::string error;
  bool hadError = false;
  bool inFunction = false;
  std::vector<Local> locals;  // slot 0 = function itself; params/locals follow
  struct Block {
    std::string type;
    int jump = 0;
    int loopStart = 0;
  };
  std::vector<Block> blockStack;

  explicit Compiler(Heap &h) : heap(h) { chunk = &scriptChunk; }

  void fail(std::size_t line, const std::string &msg) {
    if (hadError) return;
    hadError = true;
    std::ostringstream oss;
    oss << "Compile error on line " << line << ": " << msg;
    error = oss.str();
  }

  uint8_t makeConstant(Value v, std::size_t line) {
    std::size_t idx = chunk->addConstant(v);
    if (idx > 255) {
      fail(line, "Too many constants.");
      return 0;
    }
    return static_cast<uint8_t>(idx);
  }

  uint8_t identifierConstant(const std::string &name, std::size_t line) {
    return makeConstant(Value::object(heap.allocateString(name)), line);
  }

  void emit(Op op, std::size_t line) { chunk->write(op, line); }
  void emitByte(uint8_t b, std::size_t line) { chunk->writeByte(b, line); }

  void emitConstant(Value v, std::size_t line) {
    emit(Op::Constant, line);
    emitByte(makeConstant(v, line), line);
  }

  int resolveLocal(const std::string &name) {
    for (int i = static_cast<int>(locals.size()) - 1; i >= 1; --i) {
      if (locals[static_cast<std::size_t>(i)].name == name) return i;
    }
    return -1;
  }

  int addLocal(const std::string &name, std::size_t line) {
    if (locals.size() >= 256) {
      fail(line, "Too many local variables in function.");
      return -1;
    }
    locals.push_back({name});
    return static_cast<int>(locals.size()) - 1;
  }

  void emitGetVariable(const std::string &name, std::size_t line) {
    if (inFunction) {
      int slot = resolveLocal(name);
      if (slot >= 0) {
        emit(Op::GetLocal, line);
        emitByte(static_cast<uint8_t>(slot), line);
        return;
      }
    }
    emit(Op::GetGlobal, line);
    emitByte(identifierConstant(name, line), line);
  }

  void emitSetVariable(const std::string &name, std::size_t line, bool declare) {
    if (inFunction) {
      int slot = resolveLocal(name);
      if (slot < 0 && declare) slot = addLocal(name, line);
      if (slot >= 0) {
        emit(Op::SetLocal, line);
        emitByte(static_cast<uint8_t>(slot), line);
        return;
      }
    }
    emit(Op::SetGlobal, line);
    emitByte(identifierConstant(name, line), line);
  }

  void compileExpression(std::string expr, std::size_t line) {
    expr = trim(expr);
    if (expr.empty()) {
      emit(Op::Nil, line);
      return;
    }

    // ASK fn WITH args  (expression call)
    if (startsWithCI(expr, "ASK ")) {
      compileAskCall(expr, line);
      return;
    }

    if (compileBinaryPhrase(expr, line, " IS GREATER THAN ", Op::Greater)) return;
    if (compileBinaryPhrase(expr, line, " IS LESS THAN ", Op::Less)) return;
    if (compileBinaryPhrase(expr, line, " EQUALS ", Op::Equal)) return;
    if (compileBinaryPhrase(expr, line, " IS EQUAL TO ", Op::Equal)) return;

    if (compileBinaryPhrase(expr, line, " ADD ", Op::Add)) return;
    if (startsWithCI(expr, "ADD ")) {
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

    if (startsWithCI(expr, "NOT ")) {
      compileExpression(trim(expr.substr(4)), line);
      emit(Op::Not, line);
      return;
    }

    if (startsWithCI(expr, "MAKE LIST WITH ")) {
      auto rest = trim(expr.substr(15));
      auto parts = splitArgs(rest);
      for (auto &p : parts) compileExpression(p, line);
      emit(Op::MakeArray, line);
      emitByte(static_cast<uint8_t>(parts.size()), line);
      return;
    }

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

    compilePrimary(expr, line);
  }

  void compileAskCall(const std::string &expr, std::size_t line) {
    // ASK name WITH a, b
    // ASK name   (zero args)
    auto rest = trim(expr.substr(4));
    // Reject method form ASK obj TO method for now
    if (toUpper(rest).find(" TO ") != std::string::npos) {
      fail(line, "ASK obj TO method is not on the native VM yet (blueprints next).");
      return;
    }
    auto U = toUpper(rest);
    auto withPos = U.find(" WITH ");
    std::string name;
    std::vector<std::string> args;
    if (withPos == std::string::npos) {
      name = rest;
    } else {
      name = trim(rest.substr(0, withPos));
      args = splitArgs(trim(rest.substr(withPos + 6)));
    }
    emitGetVariable(name, line);
    for (auto &a : args) compileExpression(a, line);
    emit(Op::Call, line);
    emitByte(static_cast<uint8_t>(args.size()), line);
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

  void compilePrimary(std::string expr, std::size_t line) {
    expr = trim(expr);

    if (expr.size() >= 2 &&
        ((expr.front() == '"' && expr.back() == '"') ||
         (expr.front() == '\'' && expr.back() == '\''))) {
      std::string raw = expr.substr(1, expr.size() - 2);
      emitConstant(Value::object(heap.allocateString(raw)), line);
      return;
    }

    bool allWord = !expr.empty();
    for (char c : expr) {
      if (!(std::isalpha(static_cast<unsigned char>(c)) ||
            std::isspace(static_cast<unsigned char>(c)))) {
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

    char *end = nullptr;
    double num = std::strtod(expr.c_str(), &end);
    if (end && end != expr.c_str() && *end == '\0') {
      emitConstant(Value::number(num), line);
      return;
    }

    bool ident = !expr.empty();
    for (char c : expr) {
      if (!isIdentChar(c)) {
        ident = false;
        break;
      }
    }
    if (ident) {
      emitGetVariable(expr, line);
      return;
    }

    fail(line, "Cannot parse expression: " + expr);
  }

  bool beginFunctionHeader(const std::string &text, std::size_t line, std::string &outName,
                           std::vector<std::string> &outParams) {
    std::string rest;
    if (startsWithCI(text, "THIS IS FUNCTION ")) {
      rest = trim(text.substr(17));
    } else if (startsWithCI(text, "MAKE FUNCTION ")) {
      rest = trim(text.substr(14));
    } else if (startsWithCI(text, "RECIPE ")) {
      rest = trim(text.substr(7));
    } else {
      return false;
    }

    auto U = toUpper(rest);
    auto doPos = U.rfind(" DO");
    if (doPos != std::string::npos && doPos + 3 == U.size()) {
      rest = trim(rest.substr(0, doPos));
      U = toUpper(rest);
    }

    auto withPos = U.find(" WITH ");
    if (withPos == std::string::npos) {
      outName = rest;
      outParams.clear();
    } else {
      outName = trim(rest.substr(0, withPos));
      outParams = splitArgs(trim(rest.substr(withPos + 6)));
    }
    if (outName.empty()) {
      fail(line, "Function needs a name.");
      return true;
    }
    return true;
  }

  // Pending function body compilation state
  ObjFunction *compilingFunction = nullptr;
  Chunk *enclosingChunk = nullptr;
  std::vector<Local> enclosingLocals;
  bool enclosingInFunction = false;
  std::string functionGlobalName;

  void startFunction(const std::string &name, const std::vector<std::string> &params,
                     std::size_t line) {
    if (compilingFunction) {
      fail(line, "Nested functions are not supported yet.");
      return;
    }
    auto *fn = heap.allocateFunction(name, static_cast<int>(params.size()));
    compilingFunction = fn;
    functionGlobalName = name;

    enclosingChunk = chunk;
    enclosingLocals = locals;
    enclosingInFunction = inFunction;

    chunk = &fn->chunk;
    inFunction = true;
    locals.clear();
    locals.push_back({name});  // slot 0 = function value
    for (const auto &p : params) locals.push_back({p});
  }

  void endFunction(std::size_t line) {
    if (!compilingFunction) {
      fail(line, "END FUNCTION without matching function.");
      return;
    }
    // Implicit return nil if body falls through.
    emit(Op::Nil, line);
    emit(Op::Return, line);

    ObjFunction *fn = compilingFunction;
    compilingFunction = nullptr;

    chunk = enclosingChunk;
    locals = enclosingLocals;
    inFunction = enclosingInFunction;
    enclosingChunk = nullptr;

    emitConstant(Value::object(fn), line);
    emit(Op::SetGlobal, line);
    emitByte(identifierConstant(functionGlobalName, line), line);
    emit(Op::Pop, line);
  }

  void compileLine(const std::string &raw, std::size_t line) {
    std::string text = trim(raw);
    if (text.empty()) return;
    if (startsWithCI(text, "//")) return;

    if (startsWithCI(text, "LET'S START") || startsWithCI(text, "LETS START") ||
        startsWithCI(text, "GET OUTTA HERE")) {
      return;
    }

    std::string fnName;
    std::vector<std::string> fnParams;
    if (beginFunctionHeader(text, line, fnName, fnParams)) {
      if (hadError) return;
      startFunction(fnName, fnParams, line);
      return;
    }

    if (toUpper(text) == "END FUNCTION" || toUpper(text) == "ENDFUNCTION") {
      endFunction(line);
      return;
    }

    if (startsWithCI(text, "GIVE BACK ") || startsWithCI(text, "SEND BACK ") ||
        startsWithCI(text, "HAND BACK ")) {
      if (!inFunction) {
        fail(line, "GIVE BACK is only valid inside a function.");
        return;
      }
      auto U = toUpper(text);
      auto backPos = U.find(" BACK ");
      if (backPos == std::string::npos) {
        fail(line, "Expected GIVE BACK <expr>");
        return;
      }
      compileExpression(trim(text.substr(backPos + 6)), line);
      emit(Op::Return, line);
      return;
    }

    if (toUpper(text) == "GIVE BACK" || toUpper(text) == "SEND BACK" ||
        toUpper(text) == "HAND BACK") {
      if (!inFunction) {
        fail(line, "GIVE BACK is only valid inside a function.");
        return;
      }
      emit(Op::Nil, line);
      emit(Op::Return, line);
      return;
    }

    if (startsWithCI(text, "SPEAK ") || startsWithCI(text, "SAY ") || startsWithCI(text, "YELL ") ||
        startsWithCI(text, "SHOUT ")) {
      auto space = text.find(' ');
      compileExpression(trim(text.substr(space + 1)), line);
      emit(Op::Print, line);
      return;
    }

    // Statement-level ASK (discard result unless assigned via SET)
    if (startsWithCI(text, "ASK ")) {
      compileAskCall(text, line);
      emit(Op::Pop, line);
      return;
    }

    if (startsWithCI(text, "MY NAME IS ")) {
      auto rest = trim(text.substr(11));
      auto U = toUpper(rest);
      auto setPos = U.find(" SET TO ");
      std::string name;
      if (setPos == std::string::npos) {
        name = rest;
        emit(Op::Nil, line);
      } else {
        name = trim(rest.substr(0, setPos));
        compileExpression(trim(rest.substr(setPos + 8)), line);
      }
      if (inFunction) {
        // Initializer value on the stack becomes the local slot — do not Pop.
        if (resolveLocal(name) < 0) addLocal(name, line);
        else {
          emitSetVariable(name, line, /*declare=*/false);
          emit(Op::Pop, line);
        }
      } else {
        emitSetVariable(name, line, /*declare=*/false);
        emit(Op::Pop, line);
      }
      return;
    }

    if (startsWithCI(text, "SET ")) {
      auto rest = trim(text.substr(4));
      auto U = toUpper(rest);
      auto toPos = U.find(" TO ");
      if (toPos == std::string::npos) {
        fail(line, "Expected SET name TO value");
        return;
      }
      std::string name = trim(rest.substr(0, toPos));
      compileExpression(trim(rest.substr(toPos + 4)), line);
      if (inFunction && resolveLocal(name) < 0) {
        // First assignment in a function declares a local; keep value on stack.
        addLocal(name, line);
      } else {
        emitSetVariable(name, line, /*declare=*/false);
        emit(Op::Pop, line);
      }
      return;
    }

    if (startsWithCI(text, "IF ")) {
      auto rest = trim(text.substr(3));
      auto U = toUpper(rest);
      auto doPos = U.rfind(" DO");
      if (doPos != std::string::npos && doPos + 3 == U.size()) {
        rest = trim(rest.substr(0, doPos));
      }
      blockStack.push_back({"IF", 0, 0});
      compileExpression(rest, line);
      int jump = chunk->writeJump(Op::JumpIfFalse, line);
      emit(Op::Pop, line);
      blockStack.back().jump = jump;
      return;
    }

    if (toUpper(text) == "END IF" || toUpper(text) == "ENDIF") {
      if (blockStack.empty() || blockStack.back().type != "IF") {
        fail(line, "END IF without matching IF");
        return;
      }
      int endJump = chunk->writeJump(Op::Jump, line);
      chunk->patchJump(blockStack.back().jump);
      emit(Op::Pop, line);
      chunk->patchJump(endJump);
      blockStack.pop_back();
      return;
    }

    if (startsWithCI(text, "WHILE ")) {
      auto rest = trim(text.substr(6));
      auto U = toUpper(rest);
      auto doPos = U.rfind(" DO");
      if (doPos != std::string::npos && doPos + 3 == U.size()) {
        rest = trim(rest.substr(0, doPos));
      }
      int loopStart = static_cast<int>(chunk->code.size());
      blockStack.push_back({"WHILE", 0, loopStart});
      compileExpression(rest, line);
      int jump = chunk->writeJump(Op::JumpIfFalse, line);
      emit(Op::Pop, line);
      blockStack.back().jump = jump;
      return;
    }

    if (toUpper(text) == "END WHILE" || toUpper(text) == "ENDWHILE") {
      if (blockStack.empty() || blockStack.back().type != "WHILE") {
        fail(line, "END WHILE without matching WHILE");
        return;
      }
      auto block = blockStack.back();
      blockStack.pop_back();
      emit(Op::Loop, line);
      int back = static_cast<int>(chunk->code.size()) - block.loopStart + 2;
      emitByte(static_cast<uint8_t>((back >> 8) & 0xff), line);
      emitByte(static_cast<uint8_t>(back & 0xff), line);
      chunk->patchJump(block.jump);
      emit(Op::Pop, line);
      return;
    }

    fail(line, "Unknown statement: " + text);
  }
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
  if (c.compilingFunction) {
    result.ok = false;
    result.error = "Compile error: unclosed FUNCTION";
    return result;
  }
  if (!c.blockStack.empty()) {
    result.ok = false;
    result.error = "Compile error: unclosed " + c.blockStack.back().type + " block";
    return result;
  }
  c.emit(Op::Halt, lineNo ? lineNo : 1);
  result.ok = true;
  result.chunk = std::move(c.scriptChunk);
  return result;
}

}  // namespace luke
