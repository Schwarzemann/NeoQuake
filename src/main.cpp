#include <cstdio>
#include <cstdlib>
#include <string>
#include <cfloat>
#include <iostream>
#include <vector>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "BSP.h"
#include "Renderer.h"
#include "Camera.h"
#include "Input.h"
#include "Explore.h"

using namespace neoquake;

// World-space info for the loaded map (used every frame)
static neoquake::Vec3 gMapCenter{ 0,0,0 };
static float          gMapRadius = 1.0f;

// Compute an axis-aligned bounds from all BSP vertices
static void computeBounds(const neoquake::BSPMap& map,
    neoquake::Vec3& bmin,
    neoquake::Vec3& bmax)
{
    if (map.vertices.empty()) { bmin = { 0,0,0 }; bmax = { 0,0,0 }; return; }
    bmin = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : map.vertices) {
        bmin.x = std::min(bmin.x, v.x); bmax.x = std::max(bmax.x, v.x);
        bmin.y = std::min(bmin.y, v.y); bmax.y = std::max(bmax.y, v.y);
        bmin.z = std::min(bmin.z, v.z); bmax.z = std::max(bmax.z, v.z);
    }
}

static void glfwErrCb(int code, const char* desc) {
    std::cerr << "[GLFW] (" << code << ") " << (desc ? desc : "") << "\n";
}

// Cross-platform env helpers
static void set_env(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    setenv(k, v ? v : "", 1);
#endif
}
static void unset_env(const char* k) {
#ifdef _WIN32
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}

// Initialize GLFW choosing the best platform available (Wayland if present, else X11).
static bool initGLFWSmart() {
    glfwSetErrorCallback(glfwErrCb);

#ifdef _WIN32
    // On Windows, GLFW uses the Win32 backend; no env juggling needed.
    return glfwInit();
#else
    auto has = [](const char* name) -> bool {
        const char* v = std::getenv(name);
        return v && *v;
        };
    if (!has("GLFW_PLATFORM")) {
        if (has("WAYLAND_DISPLAY")) set_env("GLFW_PLATFORM", "wayland");
        else if (has("DISPLAY"))     set_env("GLFW_PLATFORM", "x11");
    }
    if (glfwInit()) return true;

    glfwTerminate();
    set_env("GLFW_PLATFORM", "wayland");
    if (glfwInit()) return true;

    glfwTerminate();
    set_env("GLFW_PLATFORM", "x11");
    if (glfwInit()) return true;

    glfwTerminate();
    unset_env("GLFW_PLATFORM");
    return glfwInit();
#endif
}


static void setProjection(int w, int h) {
    float proj[16];
    Camera::perspective(60.0f * 3.14159f/180.0f, (float)w/(float)h, 0.1f, 2048.0f, proj);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    std::string bspPath;
    std::string palettePath;
    if(argc >= 2) bspPath = argv[1];
    if(argc >= 3) palettePath = argv[2];

    bool exploreFlag = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--explore" || a == "-e") exploreFlag = true;
    }

    if(bspPath.empty()) {
        std::cout << "NeoQuake " << NEOQUAKE_VERSION << " - Quake 1-like renderer\n";
        std::cout << "Usage:\n  NeoQuake <map.bsp> [path/to/palette.lmp]\n";
        return 0;
    }

    if(!initGLFWSmart()) { std::cerr << "Failed to init GLFW (no usable backend)\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "NeoQuake", nullptr, nullptr);
    if(!win) { std::cerr << "Failed to create window\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    // GL viewport/projection
    int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    setProjection(fbw, fbh);

    glfwSetFramebufferSizeCallback(win, [](GLFWwindow* w, int width, int height) {
        glViewport(0, 0, width, height);
        setProjection(width, height);
    });

    // --- Input init (capture-style) ---
    InputContext input{};
    Input_Init(win, &input); // sets user pointer + cursor-pos callback
    glfwSetWindowSizeCallback(win, [](GLFWwindow* w, int width, int height){
        Input_OnResize(w, width, height);
    });

    // Optional: auto-release capture when window loses focus
    glfwSetWindowFocusCallback(win, [](GLFWwindow* w, int focused){
        if (!focused) {
            auto* ictx = static_cast<InputContext*>(glfwGetWindowUserPointer(w));
            if (ictx && ictx->mlook.looking) {
                Input_EndLook(w, ictx);
                glfwSetWindowTitle(w, "NeoQuake (Click to capture | Esc to release | Q to quit)");
            }
        }
    });

    std::string err;
    auto mapOpt = LoadBSP(bspPath, palettePath, err);
    if(!mapOpt) { std::cerr << "LoadBSP failed: " << err << std::endl; return 1; }
    BSPMap map = std::move(*mapOpt);

    Renderer renderer;
    renderer.uploadTextures(map);

    Camera cam;
    neoquake::ExploreState explore;
    neoquake::Explore_Init(explore, map, cam, exploreFlag);

    // If you want to automatically capture mouse when exploring:
    if (exploreFlag) {
        // your existing code that calls Input_BeginLook(win, &ictx); goes here
    }
    if(!map.models.empty()) {
        cam.x = map.models[0].origin.x;
        cam.y = map.models[0].origin.y + 64.f;
        cam.z = map.models[0].origin.z;
    } else {
        cam.y = 64.f;
    }

    glfwSetWindowTitle(win, "NeoQuake (Click to capture | Esc to release | Q to quit)  [ / ] = sens, P = invert pitch");
    std::cout << "Controls:\n"
                 "  Click       -> capture mouse (no need to hold)\n"
                 "  Esc         -> release mouse\n"
                 "  Q           -> quit program\n"
                 "  WASD        -> move, Space/Ctrl up/down, Shift to sprint\n"
                 "  [ / ]       -> sensitivity down/up, P -> invert pitch\n";

    double lastTime = glfwGetTime();
    int prevLMB = GLFW_RELEASE;
    int prevP   = GLFW_RELEASE;
    int prevQ   = GLFW_RELEASE;
    int prevV   = GLFW_RELEASE;

    while(!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;

        glfwPollEvents();

        // Cycle view mode with V
        int vKey = glfwGetKey(win, GLFW_KEY_V);
        if (vKey == GLFW_PRESS && prevV == GLFW_RELEASE) {
            int m = renderer.cycleViewMode();
            const char* name = renderer.viewModeName();
            std::cout << "View mode -> " << name << " (" << m << ")\n";

            // (optional) reflect in window title
            std::string title = std::string("NeoQuake (") + name +
                                ")  Click to capture | Esc release | Q quit   [ / ] = sens, P = invert pitch";
            glfwSetWindowTitle(win, title.c_str());
        }
        prevV = vKey;

        // --- Quit with Q (edge-triggered) ---
        int qKey = glfwGetKey(win, GLFW_KEY_Q);
        if (qKey == GLFW_PRESS && prevQ == GLFW_RELEASE) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        prevQ = qKey;

        // --- Release capture with ESC (does NOT quit) ---
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            if (input.mlook.looking) {
                Input_EndLook(win, &input);
                glfwSetWindowTitle(win, "NeoQuake (Click to capture | Esc to release | Q to quit)");
            }
        }

        // --- Sensitivity + invert toggle ---
        if (glfwGetKey(win, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS) Input_AdjustSensitivity(&input, 0.98f);
        if (glfwGetKey(win, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) Input_AdjustSensitivity(&input, 1.02f);
        int pKey = glfwGetKey(win, GLFW_KEY_P);
        if (pKey == GLFW_PRESS && prevP == GLFW_RELEASE) {
            Input_ToggleInvertPitch(&input);
            std::cout << "Pitch: " << (input.cfg.invertPitch ? "INVERTED" : "NORMAL")
                      << " | sens=" << input.cfg.sensYaw << "\n";
        }
        prevP = pKey;

        // --- Capture on LMB (press to capture; no need to hold) ---
        int lmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        if (lmb == GLFW_PRESS && prevLMB == GLFW_RELEASE) {
            if (!input.mlook.looking) {
                Input_BeginLook(win, &input);
                glfwSetWindowTitle(win, "NeoQuake (CAPTURED — Esc to release | Q to quit)");
            }
            // If already looking, do nothing; left click won't release.
        }
        prevLMB = lmb;

        // --- Movement + mouse look ---
        // Input_UpdateMovement(win, cam, dt);
        if (explore.enabled) {
            neoquake::Explore_Update(win, map, explore, cam, dt);
        }
        else {
            Input_UpdateMovement(win, cam, dt);
        }
        Input_UpdateMouseLook(&input, cam, dt);

        // --- Render ---
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
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
