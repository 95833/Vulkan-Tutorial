#version 450
layout(location=1) in vec2 vertexUV;
layout(set=0, binding=1) uniform sampler2D colorTexture;
layout(location=0) out vec4 outColor;
void main() { outColor = texture(colorTexture, vertexUV); }
