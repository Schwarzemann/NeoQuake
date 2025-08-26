#include <cstdio>
#include <string>
#include <iostream>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "BSP.h"
#include "Renderer.h"
#include "Camera.h"
#include "Input.h"

using namespace neoquake;

static void setProjection(int w, int h) {
    if (h <= 0) h = 1;
    float proj[16];
    Camera::perspective(60.0f * 3.14159265f / 180.0f, (float)w / (float)h, 0.1f, 4096.0f, proj);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    std::string bspPath, palettePath;
    if (argc >= 2) bspPath = argv[1];
    if (argc >= 3) palettePath = argv[2];
    if (bspPath.empty()) {
        std::cout << "NeoQuakeViewer — usage:\n  NeoQuakeViewer <map.bsp> [palette.lmp]\n";
        return 0;
    }

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "NeoQuake Viewer", nullptr, nullptr);
    if (!win) { std::cerr << "Window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    int ww, hh; glfwGetFramebufferSize(win, &ww, &hh);
    glViewport(0, 0, ww, hh);
    setProjection(ww, hh);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int w, int h) {
        glViewport(0, 0, w, h);
        setProjection(w, h);
        });

    std::string err;
    auto mapOpt = LoadBSP(bspPath, palettePath, err);
    if (!mapOpt) { std::cerr << "LoadBSP failed: " << err << "\n"; return 1; }
    BSPMap map = std::move(*mapOpt);

    Renderer renderer;
    renderer.uploadTextures(map);

    Camera cam;
    // Viewer starts near world origin (or model[0]) and looks forward
    if (!map.models.empty()) {
        cam.x = map.models[0].origin.x;
        cam.y = map.models[0].origin.y + 64.f;
        cam.z = map.models[0].origin.z - 128.f; // step back a little
        cam.yaw = 0.0f;                          // looking down -Z
        cam.pitch = 0.0f;
    }
    else {
        cam.y = 64.f; cam.z = -128.f;
    }

    InputContext ictx{};
    Input_Init(win, &ictx);        // don’t capture by default in the viewer

    std::cout << "Viewer controls:\n"
        "  Left click = capture mouse, Esc = release, Q = quit\n"
        "  WASD/Space/Ctrl = fly, Arrow keys = look (if you prefer)\n";

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = float(now - last);
        last = now;

        glfwPollEvents();

        // Click to capture / ESC to release / Q to quit
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !ictx.mlook.looking) {
            Input_BeginLook(win, &ictx);
        }
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS && ictx.mlook.looking) {
            Input_EndLook(win, &ictx);
        }
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        Input_UpdateMouseLook(&ictx, cam, dt);
        Input_UpdateMovement(win, cam, dt);

        glClearColor(0.1f, 0.1f, 0.12f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float view[16];
        cam.viewMatrix(view);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(view);

        renderer.drawMap(map);

        glfwSwapBuffers(win);
    }

    glfwTerminate();
    return 0;
}
