#version 120
varying vec2 vUV;
uniform sampler2D uTex0;
void main() {
    gl_FragColor = texture2D(uTex0, vUV);
}
