#version 450
layout(location=1) in vec2 vertexUV;
layout(set=0, binding=1) uniform sampler2DArray colorTexture;
layout(push_constant) uniform Options { layout(offset=0) float lod; layout(offset=4) int layer; } options;
layout(location=0) out vec4 outColor;
void main() { outColor = texture(colorTexture, vec3(vertexUV, float(options.layer))); }
