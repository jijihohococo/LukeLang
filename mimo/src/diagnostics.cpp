#include "diagnostics.hpp"
#include <iostream>

namespace mimo {

std::string friendlyError(const std::string &what, const std::string &why, const std::string &fix, SourcePos pos) {
    std::string msg;
    msg += "❌ Oops! Mimo stumbled over something.\n";
    msg += "   What: " + what + "\n";
    msg += "   Why: " + why + "\n";
    msg += "   Where: line " + std::to_string(pos.line) + ", column " + std::to_string(pos.column) + "\n";
    msg += "   How to fix: " + fix + "\n";
    msg += "   Tip: Keep it conversational — I’ve got you. ✨";
    return msg;
}

void printInfo(const std::string &msg) {
    std::cout << "ℹ️  " << msg << std::endl;
}

void printSuccess(const std::string &msg) {
    std::cout << "✅ " << msg << std::endl;
}

void printWarn(const std::string &msg) {
    std::cerr << "⚠️  " << msg << std::endl;
}

void printError(const std::string &msg) {
    std::cerr << "❌ " << msg << std::endl;
}

}