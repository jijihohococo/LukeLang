#include "luke/build.hpp"
#include "luke/compiler.hpp"
#include "luke/function.hpp"
#include "luke/object.hpp"
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

// Split "A AND B, C" style lists into identifiers.
std::vector<std::string> splitNameList(const std::string &s) {
  std::string normalized;
  normalized.reserve(s.size());
  std::string upper = toUpper(s);
  for (std::size_t i = 0; i < s.size();) {
    if (i + 5 <= s.size() && upper.compare(i, 5, " AND ") == 0) {
      normalized.push_back(',');
      i += 5;
      continue;
    }
    normalized.push_back(s[i]);
    ++i;
  }
  return splitArgs(normalized);
}

bool stripTrailingDo(std::string &s) {
  auto U = toUpper(s);
  auto doPos = U.rfind(" DO");
  if (doPos != std::string::npos && doPos + 3 == U.size()) {
    s = trim(s.substr(0, doPos));
    return true;
  }
  return false;
}

struct Local {
  std::string name;
  bool isCaptured = false;
};

struct Upvalue {
  uint8_t index = 0;
  bool isLocal = true;
};

struct Compiler {
  Compiler *enclosing = nullptr;
  Heap &heap;
  Chunk *chunk = nullptr;
  Chunk scriptChunk;
  std::string error;
  bool hadError = false;

  bool inFunction = false;
  bool inMethod = false;
  bool inClass = false;
  std::string className;
  std::vector<std::string> implementsContracts;

  ObjContract *currentContract = nullptr;
  std::string contractName;

  std::vector<Local> locals;
  std::vector<Upvalue> upvalues;

  struct Block {
    std::string type;
    int jump = 0;
    int loopStart = 0;
  };
  std::vector<Block> blockStack;

  ObjFunction *function = nullptr;
  std::string functionGlobalName;
  bool functionIsMethod = false;
  std::string methodBindName;
  uint8_t methodFlags = 0;
  bool functionIsNested = false;

  explicit Compiler(Heap &h, Compiler *enc = nullptr) : enclosing(enc), heap(h) {
    if (!enc) chunk = &scriptChunk;
  }

  void fail(std::size_t line, const std::string &msg) {
    if (hadError) return;
    hadError = true;
    std::ostringstream oss;
    oss << "Compile error on line " << line << ": " << msg;
    error = oss.str();
    // Propagate to root so compileLuke sees it.
    for (Compiler *c = enclosing; c; c = c->enclosing) {
      if (!c->hadError) {
        c->hadError = true;
        c->error = error;
      }
    }
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
    // Include slot 0 (SELF in methods / function cell for recursion).
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
      if (locals[static_cast<std::size_t>(i)].name == name) return i;
    }
    return -1;
  }

  int addLocal(const std::string &name, std::size_t line) {
    if (locals.size() >= 256) {
      fail(line, "Too many local variables in function.");
      return -1;
    }
    locals.push_back({name, false});
    return static_cast<int>(locals.size()) - 1;
  }

  int addUpvalue(uint8_t index, bool isLocal, std::size_t line) {
    for (std::size_t i = 0; i < upvalues.size(); ++i) {
      if (upvalues[i].index == index && upvalues[i].isLocal == isLocal) {
        return static_cast<int>(i);
      }
    }
    if (upvalues.size() >= 256) {
      fail(line, "Too many closure variables in function.");
      return -1;
    }
    upvalues.push_back({index, isLocal});
    return static_cast<int>(upvalues.size()) - 1;
  }

  int resolveUpvalue(const std::string &name, std::size_t line) {
    if (!enclosing) return -1;

    int local = enclosing->resolveLocal(name);
    if (local != -1) {
      enclosing->locals[static_cast<std::size_t>(local)].isCaptured = true;
      return addUpvalue(static_cast<uint8_t>(local), true, line);
    }

    int upvalue = enclosing->resolveUpvalue(name, line);
    if (upvalue != -1) {
      return addUpvalue(static_cast<uint8_t>(upvalue), false, line);
    }
    return -1;
  }

  void emitGetVariable(const std::string &name, std::size_t line) {
    if (inFunction) {
      int slot = resolveLocal(name);
      if (slot >= 0) {
        emit(Op::GetLocal, line);
        emitByte(static_cast<uint8_t>(slot), line);
        return;
      }
      int up = resolveUpvalue(name, line);
      if (up >= 0) {
        emit(Op::GetUpvalue, line);
        emitByte(static_cast<uint8_t>(up), line);
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
      int up = resolveUpvalue(name, line);
      if (up >= 0) {
        emit(Op::SetUpvalue, line);
        emitByte(static_cast<uint8_t>(up), line);
        return;
      }
    }
    emit(Op::SetGlobal, line);
    emitByte(identifierConstant(name, line), line);
  }

  void compileExpression(std::string expr, std::size_t line);
  void compilePrimary(std::string expr, std::size_t line);
  void compileAskCall(const std::string &expr, std::size_t line);
  void compileNewExpr(const std::string &expr, std::size_t line);
  bool compileBinaryPhrase(const std::string &expr, std::size_t line, const std::string &opWord,
                           Op op);
  void compileLine(const std::string &raw, std::size_t line, Compiler *&current);
  Compiler *startFunctionLike(const std::string &name, const std::vector<std::string> &params,
                              bool method, const std::string &bindName, uint8_t flags,
                              std::size_t line);
  Compiler *endFunctionLike(std::size_t line);
};

void Compiler::compileExpression(std::string expr, std::size_t line) {
  expr = trim(expr);
  if (expr.empty()) {
    emit(Op::Nil, line);
    return;
  }

  if (startsWithCI(expr, "ASK ")) {
    compileAskCall(expr, line);
    return;
  }

  if (startsWithCI(expr, "NEW ") || startsWithCI(expr, "CREATE ") ||
      startsWithCI(expr, "MAKE OBJECT ")) {
    compileNewExpr(expr, line);
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
  if (compileBinaryPhrase(expr, line, " MULTIPLY BY ", Op::Multiply)) return;
  if (compileBinaryPhrase(expr, line, " MULTIPLY ", Op::Multiply)) return;
  if (startsWithCI(expr, "MULTIPLY ")) {
    auto rest = trim(expr.substr(9));
    if (startsWithCI(rest, "BY ")) rest = trim(rest.substr(3));
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

  // field OF obj  → GetProp
  {
    auto U = toUpper(expr);
    auto ofPos = U.find(" OF ");
    if (ofPos != std::string::npos) {
      auto field = trim(expr.substr(0, ofPos));
      auto obj = trim(expr.substr(ofPos + 4));
      bool fieldIdent = !field.empty();
      for (char c : field) {
        if (!isIdentChar(c)) {
          fieldIdent = false;
          break;
        }
      }
      if (fieldIdent) {
        compileExpression(obj, line);
        emit(Op::GetProp, line);
        emitByte(identifierConstant(field, line), line);
        return;
      }
    }
  }

  compilePrimary(expr, line);
}

bool Compiler::compileBinaryPhrase(const std::string &expr, std::size_t line,
                                   const std::string &opWord, Op op) {
  auto U = toUpper(expr);
  auto needle = toUpper(opWord);
  auto pos = U.find(needle);
  if (pos == std::string::npos) return false;
  compileExpression(trim(expr.substr(0, pos)), line);
  compileExpression(trim(expr.substr(pos + needle.size())), line);
  emit(op, line);
  return true;
}

void Compiler::compileAskCall(const std::string &expr, std::size_t line) {
  auto rest = trim(expr.substr(4));
  auto U = toUpper(rest);

  // ASK obj TO method [WITH args]
  auto toPos = U.find(" TO ");
  if (toPos != std::string::npos) {
    std::string obj = trim(rest.substr(0, toPos));
    std::string after = trim(rest.substr(toPos + 4));
    auto afterU = toUpper(after);
    auto withPos = afterU.find(" WITH ");
    std::string method;
    std::vector<std::string> args;
    if (withPos == std::string::npos) {
      method = after;
    } else {
      method = trim(after.substr(0, withPos));
      args = splitArgs(trim(after.substr(withPos + 6)));
    }
    compileExpression(obj, line);
    for (auto &a : args) compileExpression(a, line);
    emit(Op::Invoke, line);
    emitByte(identifierConstant(method, line), line);
    emitByte(static_cast<uint8_t>(args.size()), line);
    return;
  }

  // ASK fn [WITH args]
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

void Compiler::compileNewExpr(const std::string &expr, std::size_t line) {
  std::string rest;
  if (startsWithCI(expr, "MAKE OBJECT ")) {
    rest = trim(expr.substr(12));
  } else if (startsWithCI(expr, "CREATE ")) {
    rest = trim(expr.substr(7));
  } else {
    rest = trim(expr.substr(4));  // NEW
  }
  auto U = toUpper(rest);
  auto withPos = U.find(" WITH ");
  std::string cls;
  std::vector<std::string> args;
  if (withPos == std::string::npos) {
    cls = rest;
  } else {
    cls = trim(rest.substr(0, withPos));
    args = splitArgs(trim(rest.substr(withPos + 6)));
  }
  emitGetVariable(cls, line);
  for (auto &a : args) compileExpression(a, line);
  emit(Op::Construct, line);
  emitByte(static_cast<uint8_t>(args.size()), line);
}

void Compiler::compilePrimary(std::string expr, std::size_t line) {
  expr = trim(expr);

  if (expr.size() >= 2 &&
      ((expr.front() == '"' && expr.back() == '"') ||
       (expr.front() == '\'' && expr.back() == '\''))) {
    emitConstant(Value::object(heap.allocateString(expr.substr(1, expr.size() - 2))), line);
    return;
  }

  // SELF.field
  if (startsWithCI(expr, "SELF.")) {
    if (!inMethod) {
      fail(line, "SELF is only valid inside methods / WHEN BORN.");
      return;
    }
    std::string field = trim(expr.substr(5));
    emit(Op::GetLocal, line);
    emitByte(0, line);
    emit(Op::GetProp, line);
    emitByte(identifierConstant(field, line), line);
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
  if (U == "SELF") {
    if (!inMethod) {
      fail(line, "SELF is only valid inside methods / WHEN BORN.");
      return;
    }
    emit(Op::GetLocal, line);
    emitByte(0, line);
    return;
  }

  char *end = nullptr;
  double num = std::strtod(expr.c_str(), &end);
  if (end && end != expr.c_str() && *end == '\0') {
    emitConstant(Value::number(num), line);
    return;
  }

  // obj.field dotted access (simple identifier.identifier)
  {
    auto dot = expr.find('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < expr.size()) {
      std::string obj = expr.substr(0, dot);
      std::string field = expr.substr(dot + 1);
      bool ok = true;
      for (char c : obj) {
        if (!isIdentChar(c)) ok = false;
      }
      for (char c : field) {
        if (!isIdentChar(c)) ok = false;
      }
      if (ok) {
        emitGetVariable(obj, line);
        emit(Op::GetProp, line);
        emitByte(identifierConstant(field, line), line);
        return;
      }
    }
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

Compiler *Compiler::startFunctionLike(const std::string &name,
                                      const std::vector<std::string> &params, bool method,
                                      const std::string &bindName, uint8_t flags,
                                      std::size_t /*line*/) {
  auto *fn = heap.allocateFunction(name, static_cast<int>(params.size()));
  fn->isMethod = method;
  fn->isStatic = (flags & kFlagStatic) != 0;
  fn->isPrivate = (flags & kFlagPrivate) != 0;

  auto *inner = new Compiler(heap, this);
  inner->function = fn;
  inner->chunk = &fn->chunk;
  inner->inFunction = true;
  inner->inMethod = method;
  inner->inClass = inClass;
  inner->className = className;
  inner->functionGlobalName = name;
  inner->functionIsMethod = method;
  inner->methodBindName = bindName.empty() ? name : bindName;
  inner->methodFlags = flags;
  inner->functionIsNested = (function != nullptr);
  // Share error state via fail() propagation; keep hadError linked.
  if (hadError) {
    inner->hadError = true;
    inner->error = error;
  }

  if (method) {
    inner->locals.push_back({"SELF", false});
  } else {
    inner->locals.push_back({name, false});
  }
  for (const auto &p : params) inner->locals.push_back({p, false});

  return inner;
}

Compiler *Compiler::endFunctionLike(std::size_t line) {
  if (!function) {
    fail(line, "END without matching function/method.");
    return this;
  }

  if (functionIsMethod && methodBindName == "born") {
    // Implicit return of SELF for constructors that fall through.
    emit(Op::GetLocal, line);
    emitByte(0, line);
    emit(Op::Return, line);
  } else {
    emit(Op::Nil, line);
    emit(Op::Return, line);
  }

  // Copy upvalue descriptors onto the function object.
  function->upvalueDescs.clear();
  function->upvalueDescs.reserve(upvalues.size());
  for (const auto &uv : upvalues) {
    UpvalueDesc desc;
    desc.isLocal = uv.isLocal;
    desc.index = uv.index;
    function->upvalueDescs.push_back(desc);
  }

  ObjFunction *fn = function;
  bool wasMethod = functionIsMethod;
  bool wasNested = functionIsNested;
  std::string bind = methodBindName;
  std::string globalName = functionGlobalName;
  uint8_t flags = methodFlags;
  bool useClosure = wasNested || !upvalues.empty();

  Compiler *parent = enclosing;
  if (!parent) {
    fail(line, "Internal: function with no enclosing compiler.");
    return this;
  }

  // Emit function value into enclosing chunk.
  if (useClosure) {
    parent->emit(Op::Closure, line);
    parent->emitByte(parent->makeConstant(Value::object(fn), line), line);
    for (const auto &uv : upvalues) {
      parent->emitByte(uv.isLocal ? 1 : 0, line);
      parent->emitByte(uv.index, line);
    }
  } else {
    parent->emitConstant(Value::object(fn), line);
  }

  if (wasMethod) {
    if (!parent->inClass) {
      parent->fail(line, "Method compiled outside a blueprint.");
      delete this;
      return parent;
    }
    parent->emit(Op::Method, line);
    parent->emitByte(parent->identifierConstant(bind, line), line);
    parent->emitByte(flags, line);
    // Class remains on stack (Method peeks class).
  } else if (wasNested) {
    // Bind nested function to a local (or overwrite existing).
    int slot = parent->resolveLocal(globalName);
    if (slot < 0) {
      parent->addLocal(globalName, line);
      // Value stays on stack as the new local slot — no Pop.
    } else {
      parent->emit(Op::SetLocal, line);
      parent->emitByte(static_cast<uint8_t>(slot), line);
      parent->emit(Op::Pop, line);
    }
  } else {
    parent->emit(Op::SetGlobal, line);
    parent->emitByte(parent->identifierConstant(globalName, line), line);
    parent->emit(Op::Pop, line);
  }

  if (hadError && !parent->hadError) {
    parent->hadError = true;
    parent->error = error;
  }

  delete this;
  return parent;
}

void Compiler::compileLine(const std::string &raw, std::size_t line, Compiler *&current) {
  std::string text = trim(raw);
  if (text.empty()) return;
  if (startsWithCI(text, "//")) return;

  // ---- CONTRACT --------------------------------------------------
  if (currentContract) {
    auto U = toUpper(text);
    if (U == "END CONTRACT" || U == "ENDCONTRACT" || U == "END PACT" ||
        U == "END AGREEMENT") {
      emitConstant(Value::object(currentContract), line);
      emit(Op::SetGlobal, line);
      emitByte(identifierConstant(contractName, line), line);
      emit(Op::Pop, line);
      currentContract = nullptr;
      contractName.clear();
      return;
    }

    // MUST METHOD / MUST ACTION / MUST TRICK / MUST ABILITY name [WITH args]
    std::string rest = text;
    bool isMust = false;
    if (startsWithCI(rest, "MUST ")) {
      rest = trim(rest.substr(5));
      isMust = true;
    } else if (startsWithCI(rest, "REQUIRES ")) {
      rest = trim(rest.substr(9));
      isMust = true;
    }
    if (isMust) {
      if (startsWithCI(rest, "METHOD ")) rest = trim(rest.substr(7));
      else if (startsWithCI(rest, "ACTION ")) rest = trim(rest.substr(7));
      else if (startsWithCI(rest, "TRICK ")) rest = trim(rest.substr(6));
      else if (startsWithCI(rest, "ABILITY ")) rest = trim(rest.substr(8));
      stripTrailingDo(rest);
      auto ru = toUpper(rest);
      auto withPos = ru.find(" WITH ");
      std::string reqName;
      int arity = -1;
      if (withPos == std::string::npos) {
        reqName = rest;
        arity = -1;
      } else {
        reqName = trim(rest.substr(0, withPos));
        auto args = splitArgs(trim(rest.substr(withPos + 6)));
        arity = static_cast<int>(args.size());
      }
      if (reqName.empty()) {
        fail(line, "MUST METHOD needs a name.");
        return;
      }
      currentContract->requirements[reqName] = arity;
      return;
    }
    // Ignore other lines inside CONTRACT.
    return;
  }

  if (startsWithCI(text, "CONTRACT ") || startsWithCI(text, "PACT ") ||
      startsWithCI(text, "AGREEMENT ")) {
    std::string rest;
    if (startsWithCI(text, "CONTRACT ")) rest = trim(text.substr(9));
    else if (startsWithCI(text, "PACT ")) rest = trim(text.substr(5));
    else rest = trim(text.substr(10));
    stripTrailingDo(rest);
    if (rest.empty()) {
      fail(line, "CONTRACT needs a name.");
      return;
    }
    contractName = rest;
    currentContract = heap.allocateContract(contractName);
    return;
  }

  if (startsWithCI(text, "LET'S START") || startsWithCI(text, "LETS START") ||
      startsWithCI(text, "GET OUTTA HERE")) {
    return;
  }

  // ---- Blueprint -------------------------------------------------
  if (startsWithCI(text, "ABSTRACT ") || startsWithCI(text, "BLUEPRINT ") ||
      startsWithCI(text, "THIS IS CLASS ") || startsWithCI(text, "MAKE CLASS ") ||
      startsWithCI(text, "CLASS ")) {
    if (inClass) {
      fail(line, "Nested blueprints are not supported.");
      return;
    }
    std::string rest = text;
    if (startsWithCI(rest, "ABSTRACT ")) rest = trim(rest.substr(9));
    if (startsWithCI(rest, "BLUEPRINT ")) rest = trim(rest.substr(10));
    else if (startsWithCI(rest, "THIS IS CLASS ")) rest = trim(rest.substr(14));
    else if (startsWithCI(rest, "MAKE CLASS ")) rest = trim(rest.substr(11));
    else if (startsWithCI(rest, "CLASS ")) rest = trim(rest.substr(6));
    else {
      fail(line, "Expected BLUEPRINT after ABSTRACT.");
      return;
    }
    stripTrailingDo(rest);

    // IMPLEMENTS / COMPLIES WITH contracts
    implementsContracts.clear();
    {
      auto U = toUpper(rest);
      auto implPos = U.find(" IMPLEMENTS ");
      if (implPos == std::string::npos) implPos = U.find(" COMPLIES WITH ");
      if (implPos != std::string::npos) {
        std::size_t skip = (U.compare(implPos, 12, " IMPLEMENTS ") == 0) ? 12 : 15;
        std::string contractsPart = trim(rest.substr(implPos + skip));
        rest = trim(rest.substr(0, implPos));
        implementsContracts = splitNameList(contractsPart);
      }
    }

    std::string parent;
    auto U = toUpper(rest);
    auto folPos = U.find(" FOLLOWS ");
    if (folPos == std::string::npos) folPos = U.find(" EXTENDS ");
    if (folPos == std::string::npos) folPos = U.find(" FROM ");
    std::string name;
    if (folPos == std::string::npos) {
      name = rest;
    } else {
      name = trim(rest.substr(0, folPos));
      parent = trim(rest.substr(folPos));
      if (startsWithCI(parent, "FOLLOWS ")) parent = trim(parent.substr(8));
      else if (startsWithCI(parent, "EXTENDS ")) parent = trim(parent.substr(8));
      else if (startsWithCI(parent, "FROM ")) parent = trim(parent.substr(5));
      // Single inheritance only — take first name if AND-list given.
      auto andPos = toUpper(parent).find(" AND ");
      if (andPos != std::string::npos) parent = trim(parent.substr(0, andPos));
      auto commaPos = parent.find(',');
      if (commaPos != std::string::npos) parent = trim(parent.substr(0, commaPos));
    }
    if (name.empty()) {
      fail(line, "Blueprint needs a name.");
      return;
    }
    inClass = true;
    className = name;
    emit(Op::Class, line);
    emitByte(identifierConstant(name, line), line);
    if (!parent.empty()) {
      emitGetVariable(parent, line);
      emit(Op::Inherit, line);
    }
    // Contract Implement is deferred until END CLASS so methods exist.
    return;
  }

  if (toUpper(text) == "END CLASS" || toUpper(text) == "ENDCLASS" ||
      toUpper(text) == "END BLUEPRINT") {
    if (!inClass) {
      fail(line, "END CLASS without matching BLUEPRINT.");
      return;
    }
    // Emit Implement for each contract now that methods/fields are bound.
    for (const auto &cname : implementsContracts) {
      emit(Op::GetGlobal, line);
      emitByte(identifierConstant(cname, line), line);
      emit(Op::Implement, line);
    }
    implementsContracts.clear();
    emit(Op::SetGlobal, line);
    emitByte(identifierConstant(className, line), line);
    emit(Op::Pop, line);
    inClass = false;
    className.clear();
    return;
  }

  if (toUpper(text) == "END BORN" || toUpper(text) == "ENDBORN") {
    current = endFunctionLike(line);
    return;
  }

  if (toUpper(text) == "END METHOD" || toUpper(text) == "ENDMETHOD" ||
      toUpper(text) == "END ACTION" || toUpper(text) == "END TRICK" ||
      toUpper(text) == "END ABILITY") {
    current = endFunctionLike(line);
    return;
  }

  if (inClass && !function) {
    // ALWAYS / ETERNAL / FOREVER statics
    if (startsWithCI(text, "ALWAYS ") || startsWithCI(text, "ETERNAL ") ||
        startsWithCI(text, "FOREVER ")) {
      std::string rest = text;
      if (startsWithCI(rest, "ALWAYS ")) rest = trim(rest.substr(7));
      else if (startsWithCI(rest, "ETERNAL ")) rest = trim(rest.substr(8));
      else rest = trim(rest.substr(8));  // FOREVER

      if (startsWithCI(rest, "HAS ")) {
        rest = trim(rest.substr(4));
        auto U = toUpper(rest);
        auto setPos = U.find(" SET TO ");
        std::string field;
        if (setPos == std::string::npos) {
          field = rest;
          emit(Op::Nil, line);
        } else {
          field = trim(rest.substr(0, setPos));
          compileExpression(trim(rest.substr(setPos + 8)), line);
        }
        emit(Op::StaticField, line);
        emitByte(identifierConstant(field, line), line);
        return;
      }

      // ALWAYS METHOD / ACTION / ...
      uint8_t flags = kFlagStatic;
      if (startsWithCI(rest, "METHOD ") || startsWithCI(rest, "ACTION ") ||
          startsWithCI(rest, "TRICK ") || startsWithCI(rest, "ABILITY ")) {
        std::string methodRest;
        if (startsWithCI(rest, "METHOD ")) methodRest = trim(rest.substr(7));
        else if (startsWithCI(rest, "ACTION ")) methodRest = trim(rest.substr(7));
        else if (startsWithCI(rest, "TRICK ")) methodRest = trim(rest.substr(6));
        else methodRest = trim(rest.substr(8));
        stripTrailingDo(methodRest);
        auto U = toUpper(methodRest);
        auto withPos = U.find(" WITH ");
        std::string mname;
        std::vector<std::string> params;
        if (withPos == std::string::npos) {
          mname = methodRest;
        } else {
          mname = trim(methodRest.substr(0, withPos));
          params = splitArgs(trim(methodRest.substr(withPos + 6)));
        }
        current = startFunctionLike(mname, params, true, mname, flags, line);
        return;
      }

      fail(line, "Expected ALWAYS HAS or ALWAYS METHOD.");
      return;
    }

    // HAS / PRIVATE / SECRET fields (and PRIVATE METHOD rewrite)
    if (startsWithCI(text, "HAS ") || startsWithCI(text, "PRIVATE ") ||
        startsWithCI(text, "SECRET ")) {
      uint8_t flags = 0;
      std::string rest = text;
      if (startsWithCI(rest, "PRIVATE ")) {
        flags = kFlagPrivate;
        rest = trim(rest.substr(8));
      } else if (startsWithCI(rest, "SECRET ")) {
        flags = kFlagPrivate;
        rest = trim(rest.substr(7));
      }
      if (startsWithCI(rest, "HAS ")) rest = trim(rest.substr(4));

      // Skip method forms: PRIVATE METHOD ...
      if (startsWithCI(rest, "METHOD ") || startsWithCI(rest, "ACTION ") ||
          startsWithCI(rest, "TRICK ") || startsWithCI(rest, "ABILITY ")) {
        // Fall through to method handling below with flags retained via rewrite.
        text = (flags & kFlagPrivate) ? ("PRIVATE " + rest) : rest;
      } else {
        auto U = toUpper(rest);
        auto setPos = U.find(" SET TO ");
        std::string field;
        if (setPos == std::string::npos) {
          field = rest;
          emit(Op::Nil, line);
        } else {
          field = trim(rest.substr(0, setPos));
          compileExpression(trim(rest.substr(setPos + 8)), line);
        }
        emit(Op::Field, line);
        emitByte(identifierConstant(field, line), line);
        emitByte(flags, line);
        return;
      }
    }

    // WHEN BORN / BORN / CONSTRUCTOR
    if (startsWithCI(text, "WHEN BORN") || startsWithCI(text, "BORN") ||
        startsWithCI(text, "CONSTRUCTOR") || startsWithCI(text, "MAKE ONE") ||
        startsWithCI(text, "INIT ")) {
      std::string rest = text;
      if (startsWithCI(rest, "WHEN BORN")) rest = trim(rest.substr(9));
      else if (startsWithCI(rest, "CONSTRUCTOR")) rest = trim(rest.substr(11));
      else if (startsWithCI(rest, "MAKE ONE")) rest = trim(rest.substr(8));
      else if (startsWithCI(rest, "INIT ")) rest = trim(rest.substr(5));
      else if (startsWithCI(rest, "BORN")) rest = trim(rest.substr(4));
      stripTrailingDo(rest);
      std::vector<std::string> params;
      if (startsWithCI(rest, "WITH ")) {
        params = splitArgs(trim(rest.substr(5)));
      } else if (!rest.empty()) {
        fail(line, "Expected WHEN BORN WITH args DO");
        return;
      }
      current = startFunctionLike("born", params, true, "born", 0, line);
      return;
    }

    // METHOD / ACTION / TRICK / ABILITY / PRIVATE|SECRET METHOD
    {
      uint8_t flags = 0;
      std::string methodLine = text;
      if (startsWithCI(methodLine, "PRIVATE ")) {
        flags |= kFlagPrivate;
        methodLine = trim(methodLine.substr(8));
      } else if (startsWithCI(methodLine, "SECRET ")) {
        flags |= kFlagPrivate;
        methodLine = trim(methodLine.substr(7));
      }
      if (startsWithCI(methodLine, "METHOD ") || startsWithCI(methodLine, "ACTION ") ||
          startsWithCI(methodLine, "TRICK ") || startsWithCI(methodLine, "ABILITY ")) {
        std::string rest;
        if (startsWithCI(methodLine, "METHOD ")) rest = trim(methodLine.substr(7));
        else if (startsWithCI(methodLine, "ACTION ")) rest = trim(methodLine.substr(7));
        else if (startsWithCI(methodLine, "TRICK ")) rest = trim(methodLine.substr(6));
        else rest = trim(methodLine.substr(8));
        stripTrailingDo(rest);
        auto U = toUpper(rest);
        auto withPos = U.find(" WITH ");
        std::string mname;
        std::vector<std::string> params;
        if (withPos == std::string::npos) {
          mname = rest;
        } else {
          mname = trim(rest.substr(0, withPos));
          params = splitArgs(trim(rest.substr(withPos + 6)));
        }
        current = startFunctionLike(mname, params, true, mname, flags, line);
        return;
      }
    }
  }

  // ---- Functions -------------------------------------------------
  {
    std::string rest;
    bool isFn = false;
    if (startsWithCI(text, "THIS IS FUNCTION ")) {
      rest = trim(text.substr(17));
      isFn = true;
    } else if (startsWithCI(text, "MAKE FUNCTION ")) {
      rest = trim(text.substr(14));
      isFn = true;
    } else if (startsWithCI(text, "RECIPE ")) {
      rest = trim(text.substr(7));
      isFn = true;
    }
    if (isFn) {
      stripTrailingDo(rest);
      auto U = toUpper(rest);
      auto withPos = U.find(" WITH ");
      std::string name;
      std::vector<std::string> params;
      if (withPos == std::string::npos) {
        name = rest;
      } else {
        name = trim(rest.substr(0, withPos));
        params = splitArgs(trim(rest.substr(withPos + 6)));
      }
      current = startFunctionLike(name, params, false, name, 0, line);
      return;
    }
  }

  if (toUpper(text) == "END FUNCTION" || toUpper(text) == "ENDFUNCTION") {
    current = endFunctionLike(line);
    return;
  }

  if (startsWithCI(text, "GIVE BACK ") || startsWithCI(text, "SEND BACK ") ||
      startsWithCI(text, "HAND BACK ")) {
    if (!inFunction) {
      fail(line, "GIVE BACK is only valid inside a function/method.");
      return;
    }
    auto U = toUpper(text);
    auto backPos = U.find(" BACK ");
    compileExpression(trim(text.substr(backPos + 6)), line);
    emit(Op::Return, line);
    return;
  }

  if (toUpper(text) == "GIVE BACK" || toUpper(text) == "SEND BACK" ||
      toUpper(text) == "HAND BACK") {
    if (!inFunction) {
      fail(line, "GIVE BACK is only valid inside a function/method.");
      return;
    }
    emit(Op::Nil, line);
    emit(Op::Return, line);
    return;
  }

  // CALL PARENT method [WITH args]
  if (startsWithCI(text, "CALL PARENT ") || startsWithCI(text, "CALL SUPER ")) {
    if (!inMethod) {
      fail(line, "CALL PARENT is only valid inside methods.");
      return;
    }
    std::string rest =
        startsWithCI(text, "CALL PARENT ") ? trim(text.substr(12)) : trim(text.substr(11));
    auto U = toUpper(rest);
    auto withPos = U.find(" WITH ");
    std::string method;
    std::vector<std::string> args;
    if (withPos == std::string::npos) {
      method = rest;
    } else {
      method = trim(rest.substr(0, withPos));
      args = splitArgs(trim(rest.substr(withPos + 6)));
    }
    emit(Op::GetLocal, line);
    emitByte(0, line);
    for (auto &a : args) compileExpression(a, line);
    emit(Op::SuperInvoke, line);
    emitByte(identifierConstant(method, line), line);
    emitByte(static_cast<uint8_t>(args.size()), line);
    emit(Op::Pop, line);
    return;
  }

  if (startsWithCI(text, "SPEAK ") || startsWithCI(text, "SAY ") || startsWithCI(text, "YELL ") ||
      startsWithCI(text, "SHOUT ")) {
    auto space = text.find(' ');
    compileExpression(trim(text.substr(space + 1)), line);
    emit(Op::Print, line);
    return;
  }

  if (startsWithCI(text, "ASK ")) {
    compileAskCall(text, line);
    emit(Op::Pop, line);
    return;
  }

  // SET SELF.field TO expr  /  SET field OF SELF TO expr
  if (startsWithCI(text, "SET ")) {
    auto rest = trim(text.substr(4));
    auto U = toUpper(rest);
    auto toPos = U.find(" TO ");
    if (toPos == std::string::npos) {
      fail(line, "Expected SET name TO value");
      return;
    }
    std::string target = trim(rest.substr(0, toPos));
    std::string valueExpr = trim(rest.substr(toPos + 4));

    // SET SELF.x TO ...
    if (startsWithCI(target, "SELF.")) {
      if (!inMethod) {
        fail(line, "SELF is only valid inside methods / WHEN BORN.");
        return;
      }
      std::string field = trim(target.substr(5));
      emit(Op::GetLocal, line);
      emitByte(0, line);
      compileExpression(valueExpr, line);
      emit(Op::SetProp, line);
      emitByte(identifierConstant(field, line), line);
      emit(Op::Pop, line);
      return;
    }

    // SET x OF SELF TO ...
    {
      auto tu = toUpper(target);
      auto ofPos = tu.find(" OF ");
      if (ofPos != std::string::npos) {
        std::string field = trim(target.substr(0, ofPos));
        std::string obj = trim(target.substr(ofPos + 4));
        compileExpression(obj, line);
        compileExpression(valueExpr, line);
        emit(Op::SetProp, line);
        emitByte(identifierConstant(field, line), line);
        emit(Op::Pop, line);
        return;
      }
    }

    // SET obj.field TO ...
    {
      auto dot = target.find('.');
      if (dot != std::string::npos) {
        std::string obj = target.substr(0, dot);
        std::string field = target.substr(dot + 1);
        emitGetVariable(obj, line);
        compileExpression(valueExpr, line);
        emit(Op::SetProp, line);
        emitByte(identifierConstant(field, line), line);
        emit(Op::Pop, line);
        return;
      }
    }

    compileExpression(valueExpr, line);
    if (inFunction) {
      int slot = resolveLocal(target);
      if (slot >= 0) {
        emit(Op::SetLocal, line);
        emitByte(static_cast<uint8_t>(slot), line);
        emit(Op::Pop, line);
      } else {
        int up = resolveUpvalue(target, line);
        if (up >= 0) {
          emit(Op::SetUpvalue, line);
          emitByte(static_cast<uint8_t>(up), line);
          emit(Op::Pop, line);
        } else {
          // New local: value already on stack becomes the slot.
          addLocal(target, line);
        }
      }
    } else {
      emitSetVariable(target, line, /*declare=*/false);
      emit(Op::Pop, line);
    }
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
      int slot = resolveLocal(name);
      if (slot >= 0) {
        emit(Op::SetLocal, line);
        emitByte(static_cast<uint8_t>(slot), line);
        emit(Op::Pop, line);
      } else {
        int up = resolveUpvalue(name, line);
        if (up >= 0) {
          emit(Op::SetUpvalue, line);
          emitByte(static_cast<uint8_t>(up), line);
          emit(Op::Pop, line);
        } else {
          addLocal(name, line);
        }
      }
    } else {
      emitSetVariable(name, line, /*declare=*/false);
      emit(Op::Pop, line);
    }
    return;
  }

  if (startsWithCI(text, "IF ")) {
    auto rest = trim(text.substr(3));
    stripTrailingDo(rest);
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
    stripTrailingDo(rest);
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

}  // namespace

CompileResult compileLuke(const std::string &source, Heap &heap) {
  // Shared Build frontend: expand luke/ + relative IMPORTS, then soften for Play.
  BuildOptions opt;
  opt.expandStd = false;      // std/* needs Build natives
  opt.expandCImports = false; // FOREIGN/c: is Build-only
  BuildResult meta;
  std::string src = source;
  if (source.find("IMPORT ") != std::string::npos || source.find("import ") != std::string::npos) {
    auto expanded = expandLukeImports(source, opt, &meta);
    if (!expanded.empty())
      src = softenBuildSurfaceForPlay(expanded);
    else if (!meta.error.empty()) {
      CompileResult fail;
      fail.ok = false;
      fail.error = meta.error;
      return fail;
    }
  }

  Compiler root(heap);
  Compiler *current = &root;
  std::istringstream in(src);
  std::string line;
  std::size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    current->compileLine(line, lineNo, current);
    if (root.hadError || current->hadError) break;
  }

  CompileResult result;
  if (root.hadError || (current && current->hadError)) {
    result.ok = false;
    result.error = root.hadError ? root.error : current->error;
    // Free any nested compilers still on the chain.
    while (current && current != &root) {
      Compiler *p = current->enclosing;
      delete current;
      current = p;
    }
    return result;
  }
  if (current != &root || current->function) {
    result.ok = false;
    result.error = "Compile error: unclosed function/method";
    while (current && current != &root) {
      Compiler *p = current->enclosing;
      delete current;
      current = p;
    }
    return result;
  }
  if (root.inClass) {
    result.ok = false;
    result.error = "Compile error: unclosed BLUEPRINT";
    return result;
  }
  if (root.currentContract) {
    result.ok = false;
    result.error = "Compile error: unclosed CONTRACT";
    return result;
  }
  if (!root.blockStack.empty()) {
    result.ok = false;
    result.error = "Compile error: unclosed " + root.blockStack.back().type + " block";
    return result;
  }
  root.emit(Op::Halt, lineNo ? lineNo : 1);
  result.ok = true;
  result.chunk = std::move(root.scriptChunk);
  return result;
}

}  // namespace luke
