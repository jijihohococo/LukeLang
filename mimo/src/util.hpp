#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>

inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
    return s;
}

inline bool icomparePrefix(const std::string &s, const std::string &prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) != std::toupper(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out; std::string item; std::istringstream iss(s);
    while (std::getline(iss, item, delim)) out.push_back(item);
    return out;
}

inline std::vector<std::string> splitArgs(const std::string &s) {
    std::string t = trim(s);
    if (t.empty()) return {};
    // Normalize AND to comma for simple splitting
    std::string U = toUpper(t);
    std::string norm; norm.reserve(t.size());
    for (size_t i = 0; i < t.size();) {
        if (i + 5 <= t.size() && toUpper(t.substr(i, 5)) == " AND ") { norm.push_back(','); i += 5; }
        else { norm.push_back(t[i]); ++i; }
    }
    auto parts = split(norm, ',');
    std::vector<std::string> out;
    for (auto &p : parts) {
        auto q = trim(p);
        if (!q.empty()) out.push_back(q);
    }
    return out;
}

inline std::string join(const std::vector<std::string> &xs, const std::string &sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (i) oss << sep;
        oss << xs[i];
    }
    return oss.str();
}