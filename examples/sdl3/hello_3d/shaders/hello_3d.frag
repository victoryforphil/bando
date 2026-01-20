#version 450

layout(location = 0) in vec3 vNormal;

layout(set = 3, binding = 0) uniform FragmentUniforms {
  vec4 uLightDir;
  vec4 uBaseColor;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 lightDir = normalize(-ubo.uLightDir.xyz);
  float ndotl = max(dot(normal, lightDir), 0.0);
  vec3 litColor = ubo.uBaseColor.rgb * (0.1 + ndotl);
  outColor = vec4(litColor, ubo.uBaseColor.a);
}
