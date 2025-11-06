#include "compiler.hpp"
#include "diagnostics.hpp"
#include "util.hpp"
#include <sstream>
#include <vector>
#include <unordered_set>
#include <cctype>

namespace mimo {

struct Block { std::string type; std::string name; std::string parent; std::vector<std::string> mixins; };

// Forward declaration for helper used in emitSpeak
static std::string replaceSelfDotAll(std::string s);

static std::string stripTrailingDo(const std::string &s) {
    std::string t = trim(s);
    if (t.empty()) return t;
    std::string U = toUpper(t);
    // If ends with " DO" remove it
    if (U.size() >= 3) {
        auto pos = U.rfind(" DO");
        if (pos != std::string::npos && pos + 3 == U.size()) {
            return trim(t.substr(0, pos));
        }
    }
    return t;
}

static std::string emitSpeak(const std::string &expr) {
    // Replace AND with + for concatenation if needed
    std::string e = trim(expr);
    e = replaceSelfDotAll(e);

    auto isQuoted = [](const std::string &s) -> bool {
        return s.find('"') != std::string::npos || s.find('\'') != std::string::npos || s.find('`') != std::string::npos;
    };
    auto needsQuoting = [&isQuoted](const std::string &s) -> bool {
        // Heuristic: multi-word alphabetic phrase should be treated as a string
        // Single word likely a variable; leave unquoted
        std::string t = trim(s);
        if (t.empty()) return false;
        if (isQuoted(t)) return false;
        // Contains space?
        if (t.find(' ') == std::string::npos) return false;
        // All chars letters or spaces?
        for (char c : t) {
            if (!(std::isalpha(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c)))) return false;
        }
        return true;
    };

    std::string U = toUpper(e);
    size_t pos = 0; std::string out;
    if (U.find(" AND ") != std::string::npos) {
        // Split by AND and quote multi-word phrases
        std::vector<std::string> parts; std::string cur = e;
        while ((pos = toUpper(cur).find(" AND ")) != std::string::npos) {
            std::string part = trim(cur.substr(0, pos));
            if (needsQuoting(part)) part = std::string("\"") + part + "\"";
            parts.push_back(part);
            cur = trim(cur.substr(pos + 5));
        }
        if (!cur.empty()) {
            std::string last = cur;
            if (needsQuoting(last)) last = std::string("\"") + last + "\"";
            parts.push_back(last);
        }
        out = "console.log(" + join(parts, " + ") + ")";
    } else {
        std::string v = e;
        if (needsQuoting(v)) v = std::string("\"") + v + "\"";
        out = "console.log(" + v + ")";
    }
    return out;
}

static std::string mapSelf(const std::string &s) {
    std::string U = toUpper(s);
    if (U == "SELF") return "this";
    return s;
}

// Replace SELF./self. with this. in arbitrary expressions
static std::string replaceSelfDotAll(std::string s) {
    // Replace case-insensitive occurrences of SELF. and self.
    for (;;) {
        auto U = toUpper(s);
        auto p = U.find("SELF.");
        if (p == std::string::npos) break;
        s.replace(p, 5, "this.");
    }
    for (;;) {
        auto p = s.find("self.");
        if (p == std::string::npos) break;
        s.replace(p, 5, "this.");
    }
    return s;
}

// Map NEW/CREATE/MAKE OBJECT Class WITH args => new Class(args)
static std::string replaceConstruction(const std::string &expr) {
    std::string s = expr;
    std::string U = toUpper(s);
    // Try patterns in order
    auto handleNewLike = [&](const std::string &keyword) {
        auto KU = toUpper(keyword);
        auto pos = U.find(KU + " ");
        if (pos == std::string::npos) return false;
        // Extract after keyword
        std::string after = trim(s.substr(pos + KU.size() + 1));
        // Class name up to space or WITH
        std::string afterU = toUpper(after);
        size_t withPos = afterU.find(" WITH ");
        std::string cls = withPos == std::string::npos ? trim(after) : trim(after.substr(0, withPos));
        std::string args;
        if (withPos != std::string::npos) {
            args = trim(after.substr(withPos + 6));
        }
        std::string js = "new " + cls + "(" + trim(join(splitArgs(args), ", ")) + ")";
        // Replace the whole NEW ... segment with js
        // We conservatively replace from pos to end of expr
        s = s.substr(0, pos) + js;
        return true;
    };
    if (handleNewLike("NEW")) return s;
    if (handleNewLike("CREATE")) return s;
    if (handleNewLike("MAKE OBJECT")) return s;
    return s;
}

CompileResult compileLukeToJS(const std::string &source) {
    std::istringstream in(source);
    std::string lineRaw;
    std::ostringstream out;
    std::vector<Block> stack;
    std::unordered_set<std::string> declaredVars; // Track declared variables (simple heuristic)
    int lineNo = 0;

    out << "// Compiled by Mimo v0.1\n";
    while (std::getline(in, lineRaw)) {
        ++lineNo;
        std::string line = trim(lineRaw);
        if (line.empty()) { out << lineRaw << "\n"; continue; }

        // CONTRACT (ignored for now)
        if (icomparePrefix(line, "CONTRACT ")) {
            // Begin a contract block; skip content for v0.1
            stack.push_back({"CONTRACT", "", "", {}});
            continue;
        }
        if (toUpper(line) == "END CONTRACT" || toUpper(line) == "END PACT" || toUpper(line) == "END AGREEMENT" || line == "}") {
            if (!stack.empty() && stack.back().type == "CONTRACT") { stack.pop_back(); }
            else {
                return {false, "", friendlyError("Unexpected contract end.", "I wasn’t inside a contract block.", "Remove this end or start with `CONTRACT Name DO`.", {lineNo, 1})};
            }
            continue;
        }

        // Class heads: ABSTRACT optional, FOLLOWS + mixins, IMPLEMENTS (ignored here)
        if (icomparePrefix(line, "BLUEPRINT ") || icomparePrefix(line, "CLASS ") || icomparePrefix(line, "MAKE CLASS ") || icomparePrefix(line, "THIS IS CLASS ") || icomparePrefix(line, "ABSTRACT CLASS ") || icomparePrefix(line, "ABSTRACT BLUEPRINT ")) {
            // Extract name and optional inheritance
            std::string rest = line.substr(line.find(' ') + 1);
            // Name is first token
            auto nameEnd = rest.find(' ');
            std::string name = nameEnd == std::string::npos ? rest : rest.substr(0, nameEnd);
            std::string parent;
            std::vector<std::string> mixins;
            std::string U = toUpper(line);
            auto fpos = U.find(" FOLLOWS ");
            if (fpos == std::string::npos) fpos = U.find(" EXTENDS ");
            if (fpos == std::string::npos) fpos = U.find(" FROM ");
            if (fpos != std::string::npos) {
                std::string inh = trim(line.substr(fpos + 9)); // after FOLLOWS
                // Split by AND
                auto parents = split(inh, ' ');
                // Simple split on AND tokens
                std::vector<std::string> tokens; for (auto &p : parents) { if (!p.empty()) tokens.push_back(p); }
                // First token is parent; collect after AND occurrences
                parent = tokens.empty() ? "" : tokens[0];
                // Find "AND" occurrences and next names
                for (size_t i = 1; i < tokens.size(); ++i) {
                    if (toUpper(tokens[i]) == "AND" && i + 1 < tokens.size()) mixins.push_back(tokens[i+1]);
                }
            }
            out << "class " << name;
            if (!parent.empty()) out << " extends " << parent;
            out << " {\n";
            stack.push_back({"CLASS", name, parent, mixins});
            continue;
        }
        if (toUpper(line) == "END CLASS" || toUpper(line) == "END CLS" || line == "}") {
            if (!stack.empty() && stack.back().type == "CLASS") {
                out << "}\n"; stack.pop_back();
            } else {
                return {false, "", friendlyError("Unexpected class end.", "I wasn’t inside a class block.", "Remove this end or start with `BLUEPRINT Name DO`.", {lineNo, 1})};
            }
            continue;
        }

        // Constructor synonyms
        if (icomparePrefix(line, "WHEN BORN") || icomparePrefix(line, "BORN") || icomparePrefix(line, "MAKE ONE") || icomparePrefix(line, "INIT") || icomparePrefix(line, "CONSTRUCTOR") || icomparePrefix(line, "BEGIN") || icomparePrefix(line, "CREATE") || icomparePrefix(line, "METHOD __init__")) {
            // Parse args after WITH or parentheses
            std::string args;
            auto W = toUpper(line);
            auto wp = W.find(" WITH ");
            if (wp != std::string::npos) args = stripTrailingDo(trim(line.substr(wp + 6)));
            else {
                auto lp = line.find('('); auto rp = line.find(')');
                if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
                    args = stripTrailingDo(trim(line.substr(lp + 1, rp - lp - 1)));
                }
            }
            out << "  constructor(" << trim(join(splitArgs(args), ", ")) << ") {\n";
            // Auto super for derived classes
            if (!stack.empty() && stack.back().type == "CLASS" && !stack.back().parent.empty()) {
                out << "    super(" << trim(join(splitArgs(args), ", ")) << ");\n";
            }
            // Mixins: copy methods from additional parents
            if (!stack.empty() && stack.back().type == "CLASS" && !stack.back().mixins.empty()) {
                for (auto &mx : stack.back().mixins) {
                    out << "    Object.assign(this, new " << mx << "());\n";
                }
            }
            stack.push_back({"CTOR", "", "", {}});
            continue;
        }
        if (toUpper(line) == "END BORN" || toUpper(line) == "END CONSTRUCTOR" || toUpper(line) == "END MAKE ONE" || toUpper(line) == "END INIT" || toUpper(line) == "END BEGIN" || toUpper(line) == "END CREATE" || toUpper(line) == "END METHOD" || line == "}") {
            if (!stack.empty() && stack.back().type == "CTOR") { out << "  }\n"; stack.pop_back(); continue; }
            // Method end
            if (!stack.empty() && stack.back().type == "METHOD") { out << "  }\n"; stack.pop_back(); continue; }
            // Otherwise ignore
            continue;
        }

        // Method heads
        if (icomparePrefix(line, "METHOD ") || icomparePrefix(line, "ACTION ") || icomparePrefix(line, "TRICK ") || icomparePrefix(line, "ABILITY ") || icomparePrefix(line, "FUNCTION ") || icomparePrefix(line, "FN ") || line.find('(') != std::string::npos) {
            // METHOD name WITH args DO
            std::string name; std::string args;
            auto W = toUpper(line);
            auto wit = W.find(" WITH ");
            if (icomparePrefix(line, "METHOD ") || icomparePrefix(line, "ACTION ") || icomparePrefix(line, "TRICK ") || icomparePrefix(line, "ABILITY ")) {
                auto after = line.substr(line.find(' ') + 1);
                if (wit != std::string::npos) { name = trim(line.substr(line.find(' ') + 1, wit - (line.find(' ') + 1))); args = stripTrailingDo(trim(line.substr(wit + 6))); }
                else { name = trim(after); }
                name = stripTrailingDo(name);
            } else {
                // name(args)
                auto lp = line.find('('); auto rp = line.find(')');
                name = stripTrailingDo(trim(line.substr(0, lp)));
                args = (lp != std::string::npos && rp != std::string::npos) ? stripTrailingDo(trim(line.substr(lp + 1, rp - lp - 1))) : "";
            }
            out << "  " << name << "(" << trim(join(splitArgs(args), ", ")) << ") {\n";
            stack.push_back({"METHOD", name, "", {}});
            continue;
        }

        // Statements inside blocks
        if (icomparePrefix(line, "SPEAK ") || icomparePrefix(line, "YELL ") || icomparePrefix(line, "SHOUT ") || icomparePrefix(line, "SAY ")) {
            std::string expr = trim(line.substr(line.find(' ') + 1));
            out << "    " << emitSpeak(expr) << ";\n"; continue;
        }

        if (icomparePrefix(line, "SET ")) {
            // General SET handling: property via OF, dot notation, or variable assignment
            auto after = trim(line.substr(4));
            auto U = toUpper(after);

            // Case 1: SET <prop> OF <obj> TO <expr>
            auto ofPos = U.find(" OF ");
            auto toPos = U.find(" TO ");
            if (ofPos != std::string::npos && toPos != std::string::npos && toPos > ofPos) {
                auto prop = trim(after.substr(0, ofPos));
                auto obj = trim(after.substr(ofPos + 4, toPos - (ofPos + 4)));
                auto expr = trim(after.substr(toPos + 4));
                expr = replaceSelfDotAll(expr);
                expr = replaceConstruction(expr);
                out << "    " << (mapSelf(obj) == "this" ? "this." + prop : obj + "." + prop) << " = " << expr << ";\n";
                continue;
            }

            // Case 2: SET <obj>.<prop> TO <expr>
            auto dotPos = after.find('.');
            auto toPos2 = U.find(" TO ");
            if (dotPos != std::string::npos && toPos2 != std::string::npos && dotPos < toPos2) {
                auto obj = trim(after.substr(0, dotPos));
                auto prop = trim(after.substr(dotPos + 1, toPos2 - (dotPos + 1)));
                auto expr = trim(after.substr(toPos2 + 4));
                expr = replaceSelfDotAll(expr);
                expr = replaceConstruction(expr);
                std::string jsObj = mapSelf(obj) == "this" ? "this" : obj;
                out << "    " << jsObj << "." << prop << " = " << expr << ";\n";
                continue;
            }

            // Case 3: SET <var> TO <expr>
            if (toPos != std::string::npos) {
                auto varName = trim(after.substr(0, toPos));
                auto expr = trim(after.substr(toPos + 4));
                expr = replaceSelfDotAll(expr);
                expr = replaceConstruction(expr);
                // First assignment declares, subsequent reassignments just assign
                bool already = declaredVars.find(varName) != declaredVars.end();
                if (!already) declaredVars.insert(varName);
                out << "    " << (already ? "" : "let ") << varName << " = " << expr << ";\n";
                continue;
            }
        }

        if (icomparePrefix(line, "ASK ")) {
            // ASK obj TO method WITH args
            auto after = trim(line.substr(4));
            auto U = toUpper(after);
            auto toPos = U.find(" TO ");
            auto withPos = U.find(" WITH ");
            if (toPos != std::string::npos) {
                auto obj = trim(after.substr(0, toPos));
                std::string meth, args;
                if (withPos != std::string::npos) {
                    meth = trim(after.substr(toPos + 4, withPos - (toPos + 4)));
                    args = stripTrailingDo(trim(after.substr(withPos + 6)));
                } else {
                    meth = trim(after.substr(toPos + 4));
                }
                std::string jsObj = mapSelf(obj) == "this" ? "this" : obj;
                out << "    " << jsObj << "." << meth << "(" << trim(join(splitArgs(args), ", ")) << ");\n";
                continue;
            }
        }

        if (icomparePrefix(line, "CALL PARENT ") || icomparePrefix(line, "CALL SUPER ")) {
            // CALL PARENT <method> [OF <Ancestor>] WITH args
            auto after = trim(line.substr(12));
            auto U = toUpper(after);
            std::string meth, anc, args;
            auto withPos = U.find(" WITH ");
            auto ofPos = U.find(" OF ");
            if (ofPos != std::string::npos) {
                meth = trim(after.substr(0, ofPos));
                if (withPos != std::string::npos) {
                    anc = trim(after.substr(ofPos + 4, withPos - (ofPos + 4)));
                    args = stripTrailingDo(trim(after.substr(withPos + 6)));
                } else { anc = trim(after.substr(ofPos + 4)); }
                out << "    (" << anc << ".prototype['" << meth << "']).apply(this";
                if (!trim(args).empty()) out << ", [" << trim(join(splitArgs(args), ", ")) << "]"; else out << ", []";
                out << ");\n";
                continue;
            } else {
                if (withPos != std::string::npos) { meth = trim(after.substr(0, withPos)); args = stripTrailingDo(trim(after.substr(withPos + 6))); }
                else { meth = trim(after); }
                out << "    super." << meth << "(" << trim(join(splitArgs(args), ", ")) << ");\n";
                continue;
            }
        }

        // HAS <field>
        if (icomparePrefix(line, "HAS ")) {
            if (!stack.empty() && stack.back().type == "CLASS") {
                auto field = trim(line.substr(4));
                out << "  " << field << ";\n";
                continue;
            }
        }

        // Fallback: emit raw line as JS comment to help debugging
        out << "    // [Mimo skipped] " << line << "\n";
    }

    return {true, out.str(), ""};
}

} // namespace mimo