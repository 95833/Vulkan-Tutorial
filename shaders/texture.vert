#version 450
layout(location=0) in vec3 inPosition;
layout(location=2) in vec2 inUV;
layout(set=0, binding=0, std140) uniform Camera { mat4 viewProjection; } camera;
layout(location=1) out vec2 vertexUV;
void main() {
    gl_Position = camera.viewProjection * vec4(inPosition, 1);
    vertexUV = inUV;
}
