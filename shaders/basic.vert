#version 120
attribute vec3 aPos;
attribute vec2 aUV;
varying vec2 vUV;
uniform mat4 uMVP;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
