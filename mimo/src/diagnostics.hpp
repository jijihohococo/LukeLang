#pragma once
#include <string>

namespace mimo {

struct SourcePos {
    int line;
    int column;
};

std::string friendlyError(const std::string &what, const std::string &why, const std::string &fix, SourcePos pos);
void printInfo(const std::string &msg);
void printSuccess(const std::string &msg);
void printWarn(const std::string &msg);
void printError(const std::string &msg);

}