#include "Texture.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <GL/gl.h>

Texture createTextureRGBA(const unsigned char* rgba, int width, int height, bool nearest) {
    Texture t;
    t.width = width; t.height = height;
    glGenTextures(1, &t.glId);
    glBindTexture(GL_TEXTURE_2D, t.glId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

Texture createTextureRGBA(const std::vector<unsigned char>& rgba, int width, int height, bool nearest) {
    return createTextureRGBA(rgba.data(), width, height, nearest);
}
