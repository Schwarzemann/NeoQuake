#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>   // std::strlen
#include <cstdlib>   // std::getenv

// All the helpers are private to this file.
// We keep the public surface tiny (loadTextFile) so the rest of NeoQuake stays simple.
namespace {

// Poor man’s path join: baseDir + "/" + name (skips duplicate slashes).
static std::string joinPath(const std::string& a, const std::string& b) {
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}

// Extract directory part of "a/b/c.glsl" → "a/b"
static std::string dirnameOf(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string() : p.substr(0, pos);
}

// Super small file slurp. Returns empty string on failure.
static std::string readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Remove UTF-8 BOM if someone saved the shader with it.
static void stripUTF8BOM(std::string& s) {
    if (s.size() >= 3 &&
        (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
}

// Normalize Windows CRLF → LF so line numbers and logs behave.
static void normalizeNewlines(std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') continue; // skip CR in CRLF
            else out.push_back('\n');
        } else {
            out.push_back(s[i]);
        }
    }
    s.swap(out);
}

// Split env search path:  "dir1:dir2:dir3" (Linux/macOS) or "dir1;dir2;dir3" (Windows)
static std::vector<std::string> splitSearchPath(const char* env) {
    std::vector<std::string> out;
    if (!env || !*env) return out;
#ifdef _WIN32
    const char delim = ';';
#else
    const char delim = ':';
#endif
    const char* p = env;
    const char* s = p;
    while (*p) {
        if (*p == delim) {
            if (p > s) out.emplace_back(s, p - s);
            s = p + 1;
        }
        ++p;
    }
    if (p > s) out.emplace_back(s, p - s);
    return out;
}

// Try to load a file from:
// given path
// each directory listed in NEOQUAKE_SHADER_PATH
static bool loadWithSearchPaths(const std::string& path, std::string& out) {
    // 1) direct
    out = readWholeFile(path);
    if (!out.empty()) return true;

    // 2) search paths
    const char* env = std::getenv("NEOQUAKE_SHADER_PATH");
    for (const auto& dir : splitSearchPath(env)) {
        std::string candidate = joinPath(dir, path);
        out = readWholeFile(candidate);
        if (!out.empty()) return true;
    }
    return false;
}

// A tiny recursive #include expander.
// Syntax:  #include "relative/or/searchpath/file.glsl"
// This keeps line directives out (KISS); we just paste files inline for readability.
// Depth is capped so a bad include loop won’t blow up the loader.
static bool expandIncludesRecursive(const std::string& absOrRelPath,
                                   std::string& out,
                                   int depth,
                                   int maxDepth) {
    if (depth > maxDepth) {
        std::cerr << "[Shader] Include depth exceeded (" << absOrRelPath << ")\n";
        return false;
    }

    std::string src;
    if (!loadWithSearchPaths(absOrRelPath, src)) {
        std::cerr << "[Shader] Could not open: " << absOrRelPath << "\n";
        return false;
    }

    stripUTF8BOM(src);
    normalizeNewlines(src);

    std::ostringstream assembled;
    std::string baseDir = dirnameOf(absOrRelPath);

    std::istringstream lines(src);
    std::string line;
    while (std::getline(lines, line)) {
        // Super loose parse: find #include "..."
        const char* key = "#include";
        auto pos = line.find(key);
        if (pos != std::string::npos) {
            // find first quote after #include
            auto q1 = line.find('"', pos + std::strlen(key));
            auto q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                std::string inc = line.substr(q1 + 1, q2 - (q1 + 1));
                // includes are resolved relative to the parent file’s folder first
                std::string childPath = inc;
                std::string relPath   = joinPath(baseDir, inc);

                std::string childExpanded;
                if (expandIncludesRecursive(relPath, childExpanded, depth + 1, maxDepth) ||
                    expandIncludesRecursive(childPath, childExpanded, depth + 1, maxDepth)) {
                    assembled << childExpanded << "\n";
                    continue; // done with this line
                } else {
                    std::cerr << "[Shader] Failed to include: " << inc
                              << " (referenced from " << absOrRelPath << ")\n";
                }
            }
        }
        assembled << line << "\n";
    }

    out = assembled.str();
    return true;
}

} // anonymous namespace

std::string loadTextFile(const std::string& path) {
    std::string expanded;
    if (!expandIncludesRecursive(path, expanded, /*depth*/0, /*maxDepth*/16)) {
        return {}; // caller will decide what to do next
    }
    return expanded;
}
