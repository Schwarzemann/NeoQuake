#include <GLFW/glfw3.h>
#include <stdio.h>

int main() {
    glfwInit();
    int major, minor, revision;
    glfwGetVersion(&major, &minor, &revision);
    printf("GLFW Version: %d.%d.%d\n", major, minor, revision);
    glfwTerminate();
    return 0;
}
