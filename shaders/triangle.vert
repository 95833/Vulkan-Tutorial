#version 450
// 正height viewport：负NDC y在图上方。
const vec2 positions[3] = vec2[](vec2(0,-0.75), vec2(0.75,0.65), vec2(-0.75,0.65));
const vec3 colors[3] = vec3[](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
layout(location=0) out vec3 vertexColor;
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0, 1);
    vertexColor = colors[gl_VertexIndex];
}
