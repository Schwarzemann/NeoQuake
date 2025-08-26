#include <cstdio>
#include <string>
#include <iostream>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "BSP.h"
#include "Renderer.h"
#include "Camera.h"
#include "Input.h"
#include "Game.h"

using namespace neoquake;

static void setProjection(int w, int h) {
    if (h <= 0) h = 1;
    float proj[16];
    Camera::perspective(70.0f * 3.14159265f / 180.0f, (float)w / (float)h, 0.05f, 4096.0f, proj);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    std::string bspPath, palettePath;
    if (argc >= 2) bspPath = argv[1];
    if (argc >= 3) palettePath = argv[2];
    if (bspPath.empty()) {
        std::cout << "NeoQuakeGame — usage:\n  NeoQuakeGame <map.bsp> [palette.lmp]\n";
        return 0;
    }

    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "NeoQuake Game", nullptr, nullptr);
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
    Game game;
    Game_Init(game, map, cam); // spawns at info_player_start if present

    // Capture look by default in “game”
    InputContext ictx{};
    Input_Init(win, &ictx);
    Input_BeginLook(win, &ictx);

    std::cout << "Game controls:\n"
        "  Mouse = look, WASD = move, Space/Ctrl = up/down\n"
        "  Esc = release mouse, Left click = recapture, Q = quit\n";

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = float(now - last);
        last = now;

        glfwPollEvents();

        // Allow releasing and recapturing
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS && ictx.mlook.looking) {
            Input_EndLook(win, &ictx);
        }
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !ictx.mlook.looking) {
            Input_BeginLook(win, &ictx);
        }
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        Input_UpdateMouseLook(&ictx, cam, dt);
        Input_UpdateMovement(win, cam, dt);

        // Mirror camera back into the game/player (until we add physics)
        game.player.x = cam.x; game.player.y = cam.y; game.player.z = cam.z;
        game.player.yaw = cam.yaw; game.player.pitch = cam.pitch;
        Game_Update(game, map, cam, dt);

        glClearColor(0.08f, 0.1f, 0.12f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float view[16]; cam.viewMatrix(view);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(view);

        renderer.drawMap(map);

        glfwSwapBuffers(win);
    }

    glfwTerminate();
    return 0;
}
