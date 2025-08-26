#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GLchar
typedef char GLchar;
#endif

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER   0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS  0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS     0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif

typedef GLuint(APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void    (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count,
    const GLchar* const* string,
    const GLint* length);
typedef void    (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void    (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void    (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength,
    GLsizei* length, GLchar* infoLog);
typedef void    (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);

typedef GLuint(APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void    (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void    (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void    (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void    (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength,
    GLsizei* length, GLchar* infoLog);
typedef void    (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);


#include "Shader.h"
// #include <GL/gl.h>        // only to define GLuint / constants
// #include <GLFW/glfw3.h>   // we’ll use glfwGetProcAddress to fetch GL shader entry points
#include <cstdlib>        // std::getenv
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// Forward: we reuse the smarter loader from Shader_Load.cpp
std::string loadTextFile(const std::string& path);

// Everything inside this TU is careful to compile even if real shaders aren’t used.
// If anything fails, we just fall back to the fixed-function pipeline (ShaderProg.id = 0).

namespace {

// Glue helpers to fetch GL 2.0 shader funcs at runtime (no GLEW/GLAD needed).
template<typename T>
static T glLoad(const char* name) {
    // GLFW gives us a cross-platform proc loader.
    return reinterpret_cast<T>(glfwGetProcAddress(name));
}

// A tiny holder for the pointers we care about.
struct GLSL {
    // Shader objects
    PFNGLCREATESHADERPROC            CreateShader   = nullptr;
    PFNGLSHADERSOURCEPROC            ShaderSource   = nullptr;
    PFNGLCOMPILESHADERPROC           CompileShader  = nullptr;
    PFNGLGETSHADERIVPROC             GetShaderiv    = nullptr;
    PFNGLGETSHADERINFOLOGPROC        GetShaderInfo  = nullptr;
    PFNGLDELETESHADERPROC            DeleteShader   = nullptr;

    // Program objects
    PFNGLCREATEPROGRAMPROC           CreateProgram  = nullptr;
    PFNGLATTACHSHADERPROC            AttachShader   = nullptr;
    PFNGLLINKPROGRAMPROC             LinkProgram    = nullptr;
    PFNGLGETPROGRAMIVPROC            GetProgramiv   = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC       GetProgramInfo = nullptr;
    PFNGLDELETEPROGRAMPROC           DeleteProgram  = nullptr;
};

static bool loadGLSL(GLSL& g) {
    g.CreateShader   = glLoad<PFNGLCREATESHADERPROC>          ("glCreateShader");
    g.ShaderSource   = glLoad<PFNGLSHADERSOURCEPROC>          ("glShaderSource");
    g.CompileShader  = glLoad<PFNGLCOMPILESHADERPROC>         ("glCompileShader");
    g.GetShaderiv    = glLoad<PFNGLGETSHADERIVPROC>           ("glGetShaderiv");
    g.GetShaderInfo  = glLoad<PFNGLGETSHADERINFOLOGPROC>      ("glGetShaderInfoLog");
    g.DeleteShader   = glLoad<PFNGLDELETESHADERPROC>          ("glDeleteShader");

    g.CreateProgram  = glLoad<PFNGLCREATEPROGRAMPROC>         ("glCreateProgram");
    g.AttachShader   = glLoad<PFNGLATTACHSHADERPROC>          ("glAttachShader");
    g.LinkProgram    = glLoad<PFNGLLINKPROGRAMPROC>           ("glLinkProgram");
    g.GetProgramiv   = glLoad<PFNGLGETPROGRAMIVPROC>          ("glGetProgramiv");
    g.GetProgramInfo = glLoad<PFNGLGETPROGRAMINFOLOGPROC>     ("glGetProgramInfoLog");
    g.DeleteProgram  = glLoad<PFNGLDELETEPROGRAMPROC>         ("glDeleteProgram");

    // If any essential pointer is null, we consider GLSL unavailable.
    return g.CreateShader && g.ShaderSource && g.CompileShader && g.GetShaderiv &&
           g.GetShaderInfo && g.DeleteShader && g.CreateProgram && g.AttachShader &&
           g.LinkProgram && g.GetProgramiv && g.GetProgramInfo && g.DeleteProgram;
}

// If the source doesn’t declare a version, prepend a conservative one.
// 120 is a safe bet for fixed-function era contexts (GLSL 1.20 ~ OpenGL 2.1).
static void injectVersionIfMissing(std::string& src, const char* versionLine = "#version 120\n") {
    // Leading comments/blank lines happen; we only add if there’s no "#version" anywhere.
    if (src.find("#version") == std::string::npos) {
        src = std::string(versionLine) + src;
    }
}

// Helper to print shader or program logs nicely.
static void printShaderLog(GLuint obj, bool isProgram,
                           PFNGLGETSHADERIVPROC getShaderiv,
                           PFNGLGETSHADERINFOLOGPROC getShaderInfo,
                           PFNGLGETPROGRAMIVPROC getProgramiv,
                           PFNGLGETPROGRAMINFOLOGPROC getProgramInfo) {
    GLint len = 0;
    if (isProgram) {
        getProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
    } else {
        getShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
    }
    if (len <= 1) return;
    std::vector<GLchar> buf(static_cast<size_t>(len), 0);
    if (isProgram) {
        getProgramInfo(obj, len, nullptr, buf.data());
        std::cerr << "[Shader] Program log:\n" << buf.data() << "\n";
    } else {
        getShaderInfo(obj, len, nullptr, buf.data());
        std::cerr << "[Shader] Shader log:\n" << buf.data() << "\n";
    }
}

// Compile utility.
static GLuint compileOne(const GLSL& g, GLenum type, const std::string& src) {
    GLuint s = g.CreateShader(type);
    if (!s) return 0;
    const char* csrc = src.c_str();
    GLint  clen = static_cast<GLint>(src.size());
    g.ShaderSource(s, 1, &csrc, &clen);
    g.CompileShader(s);
    GLint ok = GL_FALSE;
    g.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    printShaderLog(s, /*isProgram*/false, g.GetShaderiv, g.GetShaderInfo, nullptr, nullptr);
    if (!ok) {
        g.DeleteShader(s);
        return 0;
    }
    return s;
}

} // anonymous namespace

// If anything fails, we cleanly fall back to { id = 0 } so the app still runs.
ShaderProg buildShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    ShaderProg p;
    p.id = 0; // default/fallback: fixed-function

    // Only try real shaders if explicitly requested (keeps “no loader” builds happy).
    const char* want = std::getenv("NEOQUAKE_USE_SHADERS");
    if (!want || *want == '0') {
        return p;
    }

    // Load sources (with includes/BOM/newlines handled by our loader).
    std::string vsrc = loadTextFile(vertPath);
    std::string fsrc = loadTextFile(fragPath);
    if (vsrc.empty() || fsrc.empty()) {
        std::cerr << "[Shader] Source load failed: "
                  << (vsrc.empty() ? vertPath : "") << " "
                  << (fsrc.empty() ? fragPath : "") << "\n";
        return p;
    }

    // Make sure they have *some* GLSL version unless the file already defines one.
    injectVersionIfMissing(vsrc, "#version 120\n");
    injectVersionIfMissing(fsrc, "#version 120\n");

    // Fetch GL shader entry points through GLFW.
    GLSL g{};
    if (!loadGLSL(g)) {
        std::cerr << "[Shader] GLSL entry points unavailable (need OpenGL 2.0+). Falling back.\n";
        return p;
    }

    // Compile both stages.
    GLuint vs = compileOne(g, GL_VERTEX_SHADER,   vsrc);
    GLuint fs = compileOne(g, GL_FRAGMENT_SHADER, fsrc);
    if (!vs || !fs) {
        if (vs) g.DeleteShader(vs);
        if (fs) g.DeleteShader(fs);
        return p;
    }

    // Link program.
    GLuint prog = g.CreateProgram();
    g.AttachShader(prog, vs);
    g.AttachShader(prog, fs);
    g.LinkProgram(prog);

    GLint linked = GL_FALSE;
    g.GetProgramiv(prog, GL_LINK_STATUS, &linked);
    printShaderLog(prog, /*isProgram*/true, nullptr, nullptr, g.GetProgramiv, g.GetProgramInfo);

    // We can delete shaders once linked (program keeps the compiled code).
    g.DeleteShader(vs);
    g.DeleteShader(fs);

    if (!linked) {
        g.DeleteProgram(prog);
        return p; // fallback
    }

    p.id = prog;
    return p;
}
