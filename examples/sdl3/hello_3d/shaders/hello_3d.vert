#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 1, binding = 0) uniform VertexUniforms {
  mat4 uMvp;
  mat4 uModel;
} ubo;

layout(location = 0) out vec3 vNormal;

void main() {
  vNormal = mat3(ubo.uModel) * inNormal;
  gl_Position = ubo.uMvp * vec4(inPosition, 1.0);
}
