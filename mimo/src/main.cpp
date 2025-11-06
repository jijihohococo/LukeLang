#include <iostream>
#include <fstream>
#include <sstream>
#include "compiler.hpp"
#include "diagnostics.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        mimo::printError("No input provided. Usage: mimo <file.luke>");
        return 1;
    }
    std::string path = argv[1];
    std::ifstream f(path);
    if (!f) {
        mimo::printError("Could not open file: " + path);
        return 1;
    }
    std::stringstream buffer; buffer << f.rdbuf();
    auto result = mimo::compileLukeToJS(buffer.str());
    if (!result.ok) {
        std::cerr << result.error << std::endl;
        return 2;
    }
    std::cout << result.js;
    return 0;
}