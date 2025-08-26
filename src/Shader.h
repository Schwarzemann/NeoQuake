#pragma once
#include <string>
#include <vector>

struct ShaderProg {
    unsigned int id = 0;
};

// Compiles GLSL 1.20 shaders (for future upgrade), returns 0 if fixed pipeline used
ShaderProg buildShaderProgram(const std::string& vertPath, const std::string& fragPath);

// Load a whole file into string
std::string loadTextFile(const std::string& path);
