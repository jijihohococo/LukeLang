#pragma once
#include <string>

namespace mimo {

struct CompileResult {
    bool ok;
    std::string js;
    std::string error;
};

CompileResult compileLukeToJS(const std::string &source);

}