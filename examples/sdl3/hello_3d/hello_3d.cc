#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <nlohmann/json.hpp>
#include <tiny_gltf.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr const char *kWorkspaceName = "bando";
constexpr const char *kDefaultModelPath =
    "examples/sdl3/hello_3d/assets/Box.glb";
constexpr const char *kVertexShaderPath =
    "examples/sdl3/hello_3d/shaders/hello_3d.vert.spv";
constexpr const char *kFragmentShaderPath =
    "examples/sdl3/hello_3d/shaders/hello_3d.frag.spv";

struct Options {
  std::string model_path = kDefaultModelPath;
  double timeout_seconds = 0.0;
};

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
};

struct GltfMesh {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  glm::vec3 center = glm::vec3(0.0f);
  float radius = 1.0f;
  glm::vec4 base_color = glm::vec4(1.0f);
};

struct alignas(16) VertexUniforms {
  glm::mat4 mvp;
  glm::mat4 model;
};

struct alignas(16) FragmentUniforms {
  glm::vec4 light_dir;
  glm::vec4 base_color;
};

bool StartsWith(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool ParseDouble(const std::string &value, double *out) {
  if (!out) {
    return false;
  }
  char *end = nullptr;
  double result = std::strtod(value.c_str(), &end);
  if (!end || end == value.c_str() || *end != '\0') {
    return false;
  }
  *out = result;
  return true;
}

void PrintUsage(const char *argv0) {
  SDL_Log("Usage: %s [--model=PATH] [--timeout=SECONDS]", argv0);
}

Options ParseOptions(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    }
    if (StartsWith(arg, "--timeout=")) {
      double value = 0.0;
      if (!ParseDouble(arg.substr(std::strlen("--timeout=")), &value)) {
        SDL_Log("Invalid --timeout value: %s", arg.c_str());
      } else {
        options.timeout_seconds = value;
      }
      continue;
    }
    if (arg == "--timeout" && i + 1 < argc) {
      double value = 0.0;
      if (!ParseDouble(argv[++i], &value)) {
        SDL_Log("Invalid --timeout value: %s", argv[i]);
      } else {
        options.timeout_seconds = value;
      }
      continue;
    }
    if (StartsWith(arg, "--model=")) {
      options.model_path = arg.substr(std::strlen("--model="));
      continue;
    }
    if (arg == "--model" && i + 1 < argc) {
      options.model_path = argv[++i];
      continue;
    }
    SDL_Log("Unknown argument: %s", arg.c_str());
  }
  return options;
}

bool FileExists(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  return file.good();
}

std::string JoinPath(const std::string &left, const std::string &right) {
  if (left.empty()) {
    return right;
  }
  if (left.back() == '/') {
    return left + right;
  }
  return left + "/" + right;
}

std::string RunfilesPathFromManifest(const std::string &manifest_path,
                                     const std::string &key) {
  std::ifstream manifest(manifest_path);
  if (!manifest) {
    return std::string();
  }
  std::string line;
  while (std::getline(manifest, line)) {
    if (StartsWith(line, key) && line.size() > key.size() &&
        line[key.size()] == ' ') {
      return line.substr(key.size() + 1);
    }
  }
  return std::string();
}

std::string ResolveRunfile(const std::string &relative, const char *argv0) {
  if (relative.empty() || relative[0] == '/') {
    return relative;
  }
  if (FileExists(relative)) {
    return relative;
  }
  std::string runfiles_key =
      std::string(kWorkspaceName) + "/" + relative;
  if (const char *runfiles_dir = std::getenv("RUNFILES_DIR")) {
    std::string candidate = JoinPath(runfiles_dir, runfiles_key);
    if (FileExists(candidate)) {
      return candidate;
    }
  }
  if (const char *manifest_path = std::getenv("RUNFILES_MANIFEST_FILE")) {
    std::string mapped = RunfilesPathFromManifest(manifest_path, runfiles_key);
    if (!mapped.empty()) {
      return mapped;
    }
  }
  std::string argv0_path = argv0 ? argv0 : "";
  std::string exe_dir;
  std::string exe_name;
  std::size_t slash = argv0_path.find_last_of('/');
  if (slash != std::string::npos) {
    exe_dir = argv0_path.substr(0, slash + 1);
    exe_name = argv0_path.substr(slash + 1);
  } else {
    exe_name = argv0_path;
  }
  if (!exe_name.empty()) {
    std::string runfiles_dir = JoinPath(exe_dir, exe_name + ".runfiles");
    std::string candidate = JoinPath(runfiles_dir, runfiles_key);
    if (FileExists(candidate)) {
      return candidate;
    }
  }
  return relative;
}

std::vector<uint8_t> LoadBinaryFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  std::streamsize size = file.tellg();
  if (size <= 0) {
    return {};
  }
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(data.data()), size);
  return data;
}

bool ReadAccessorVec3(const tinygltf::Model &model,
                      int accessor_index,
                      std::vector<glm::vec3> *out,
                      std::string *error) {
  if (!out) {
    return false;
  }
  if (accessor_index < 0 || accessor_index >=
                                static_cast<int>(model.accessors.size())) {
    if (error) {
      *error = "Missing accessor";
    }
    return false;
  }
  const tinygltf::Accessor &accessor = model.accessors[accessor_index];
  if (accessor.type != TINYGLTF_TYPE_VEC3 ||
      accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
    if (error) {
      *error = "Expected VEC3 float accessor";
    }
    return false;
  }
  const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
  const tinygltf::Buffer &buffer = model.buffers[view.buffer];
  size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 3u;
  const unsigned char *data = buffer.data.data() + view.byteOffset +
                              accessor.byteOffset;
  out->resize(accessor.count);
  for (size_t i = 0; i < accessor.count; ++i) {
    const float *src =
        reinterpret_cast<const float *>(data + i * stride);
    (*out)[i] = glm::vec3(src[0], src[1], src[2]);
  }
  return true;
}

bool ReadIndexAccessor(const tinygltf::Model &model,
                       int accessor_index,
                       std::vector<uint32_t> *out,
                       std::string *error) {
  if (!out) {
    return false;
  }
  if (accessor_index < 0 || accessor_index >=
                                static_cast<int>(model.accessors.size())) {
    if (error) {
      *error = "Missing index accessor";
    }
    return false;
  }
  const tinygltf::Accessor &accessor = model.accessors[accessor_index];
  if (accessor.type != TINYGLTF_TYPE_SCALAR) {
    if (error) {
      *error = "Expected scalar index accessor";
    }
    return false;
  }
  const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
  const tinygltf::Buffer &buffer = model.buffers[view.buffer];
  size_t component_size = 0;
  switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      component_size = sizeof(uint8_t);
      break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      component_size = sizeof(uint16_t);
      break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
      component_size = sizeof(uint32_t);
      break;
    default:
      if (error) {
        *error = "Unsupported index component type";
      }
      return false;
  }
  size_t stride = view.byteStride ? view.byteStride : component_size;
  const unsigned char *data = buffer.data.data() + view.byteOffset +
                              accessor.byteOffset;
  out->resize(accessor.count);
  for (size_t i = 0; i < accessor.count; ++i) {
    const unsigned char *element = data + i * stride;
    switch (accessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        (*out)[i] = *reinterpret_cast<const uint8_t *>(element);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        (*out)[i] = *reinterpret_cast<const uint16_t *>(element);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        (*out)[i] = *reinterpret_cast<const uint32_t *>(element);
        break;
      default:
        break;
    }
  }
  return true;
}

glm::vec4 ExtractBaseColor(const tinygltf::Model &model,
                           const tinygltf::Primitive &primitive) {
  if (primitive.material < 0 ||
      primitive.material >= static_cast<int>(model.materials.size())) {
    return glm::vec4(1.0f);
  }
  const tinygltf::Material &material = model.materials[primitive.material];
  const std::vector<double> &base_color =
      material.pbrMetallicRoughness.baseColorFactor;
  if (base_color.size() == 4) {
    return glm::vec4(static_cast<float>(base_color[0]),
                     static_cast<float>(base_color[1]),
                     static_cast<float>(base_color[2]),
                     static_cast<float>(base_color[3]));
  }
  return glm::vec4(1.0f);
}

void ComputeBounds(const std::vector<Vertex> &vertices,
                   glm::vec3 *center,
                   float *radius) {
  if (!center || !radius || vertices.empty()) {
    return;
  }
  glm::vec3 min_pos(std::numeric_limits<float>::max());
  glm::vec3 max_pos(std::numeric_limits<float>::lowest());
  for (const Vertex &vertex : vertices) {
    min_pos = glm::min(min_pos, vertex.position);
    max_pos = glm::max(max_pos, vertex.position);
  }
  *center = (min_pos + max_pos) * 0.5f;
  *radius = glm::length(max_pos - min_pos) * 0.5f;
  if (*radius <= 0.0f) {
    *radius = 1.0f;
  }
}

void ComputeNormalsFromIndices(std::vector<Vertex> *vertices,
                               const std::vector<uint32_t> &indices) {
  if (!vertices || indices.size() < 3) {
    return;
  }
  for (Vertex &vertex : *vertices) {
    vertex.normal = glm::vec3(0.0f);
  }
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    uint32_t i0 = indices[i];
    uint32_t i1 = indices[i + 1];
    uint32_t i2 = indices[i + 2];
    if (i0 >= vertices->size() || i1 >= vertices->size() ||
        i2 >= vertices->size()) {
      continue;
    }
    const glm::vec3 &p0 = (*vertices)[i0].position;
    const glm::vec3 &p1 = (*vertices)[i1].position;
    const glm::vec3 &p2 = (*vertices)[i2].position;
    glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    (*vertices)[i0].normal += normal;
    (*vertices)[i1].normal += normal;
    (*vertices)[i2].normal += normal;
  }
  for (Vertex &vertex : *vertices) {
    if (glm::length(vertex.normal) > 0.0f) {
      vertex.normal = glm::normalize(vertex.normal);
    } else {
      vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
  }
}

bool LoadGltfMesh(const std::string &path, GltfMesh *mesh) {
  if (!mesh) {
    return false;
  }
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string error;
  std::string warning;
  bool ok = false;
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb") {
    ok = loader.LoadBinaryFromFile(&model, &error, &warning, path);
  } else {
    ok = loader.LoadASCIIFromFile(&model, &error, &warning, path);
  }
  if (!warning.empty()) {
    SDL_Log("tinygltf warning: %s", warning.c_str());
  }
  if (!ok) {
    SDL_Log("Failed to load glTF: %s", error.c_str());
    return false;
  }
  if (model.meshes.empty() || model.meshes[0].primitives.empty()) {
    SDL_Log("glTF has no meshes to draw");
    return false;
  }
  const tinygltf::Primitive &primitive = model.meshes[0].primitives[0];
  auto position_it = primitive.attributes.find("POSITION");
  if (position_it == primitive.attributes.end()) {
    SDL_Log("glTF mesh missing POSITION attribute");
    return false;
  }
  auto normal_it = primitive.attributes.find("NORMAL");
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::string accessor_error;
  if (!ReadAccessorVec3(model, position_it->second, &positions,
                        &accessor_error)) {
    SDL_Log("%s", accessor_error.c_str());
    return false;
  }
  if (normal_it != primitive.attributes.end()) {
    if (!ReadAccessorVec3(model, normal_it->second, &normals,
                          &accessor_error)) {
      SDL_Log("%s", accessor_error.c_str());
      return false;
    }
  }
  mesh->vertices.resize(positions.size());
  for (size_t i = 0; i < positions.size(); ++i) {
    mesh->vertices[i].position = positions[i];
    if (i < normals.size()) {
      mesh->vertices[i].normal = normals[i];
    }
  }
  if (primitive.indices >= 0) {
    if (!ReadIndexAccessor(model, primitive.indices, &mesh->indices,
                           &accessor_error)) {
      SDL_Log("%s", accessor_error.c_str());
      return false;
    }
  } else {
    mesh->indices.resize(mesh->vertices.size());
    for (size_t i = 0; i < mesh->indices.size(); ++i) {
      mesh->indices[i] = static_cast<uint32_t>(i);
    }
  }
  if (normals.empty()) {
    ComputeNormalsFromIndices(&mesh->vertices, mesh->indices);
  }
  mesh->base_color = ExtractBaseColor(model, primitive);
  ComputeBounds(mesh->vertices, &mesh->center, &mesh->radius);
  return true;
}

SDL_GPUTexture *CreateDepthTexture(SDL_GPUDevice *device,
                                   Uint32 width,
                                   Uint32 height) {
  SDL_GPUTextureCreateInfo depth_info = {};
  depth_info.type = SDL_GPU_TEXTURETYPE_2D;
  depth_info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  depth_info.width = width;
  depth_info.height = height;
  depth_info.layer_count_or_depth = 1;
  depth_info.num_levels = 1;
  depth_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  return SDL_CreateGPUTexture(device, &depth_info);
}

}  // namespace

int main(int argc, char **argv) {
  Options options = ParseOptions(argc, argv);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("SDL3 Hello 3D", 1280, 720,
                                        SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  const std::string model_path = ResolveRunfile(options.model_path, argv[0]);
  const std::string vertex_shader_path =
      ResolveRunfile(kVertexShaderPath, argv[0]);
  const std::string fragment_shader_path =
      ResolveRunfile(kFragmentShaderPath, argv[0]);

  GltfMesh mesh;
  if (!LoadGltfMesh(model_path, &mesh)) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV, nullptr)) {
    SDL_Log("SDL GPU does not report SPIR-V support");
  }

  SDL_GPUDevice *device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
  if (!device) {
    SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!SDL_ClaimWindowForGPUDevice(device, window)) {
    SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  std::vector<uint8_t> vertex_shader_code = LoadBinaryFile(vertex_shader_path);
  std::vector<uint8_t> fragment_shader_code =
      LoadBinaryFile(fragment_shader_path);
  if (vertex_shader_code.empty() || fragment_shader_code.empty()) {
    SDL_Log("Failed to load shader binaries");
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GPUShaderCreateInfo vertex_shader_info = {};
  vertex_shader_info.code_size = vertex_shader_code.size();
  vertex_shader_info.code = vertex_shader_code.data();
  vertex_shader_info.entrypoint = "main";
  vertex_shader_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  vertex_shader_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vertex_shader_info.num_uniform_buffers = 1;
  SDL_GPUShader *vertex_shader =
      SDL_CreateGPUShader(device, &vertex_shader_info);
  if (!vertex_shader) {
    SDL_Log("SDL_CreateGPUShader vertex failed: %s", SDL_GetError());
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GPUShaderCreateInfo fragment_shader_info = {};
  fragment_shader_info.code_size = fragment_shader_code.size();
  fragment_shader_info.code = fragment_shader_code.data();
  fragment_shader_info.entrypoint = "main";
  fragment_shader_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  fragment_shader_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fragment_shader_info.num_uniform_buffers = 1;
  SDL_GPUShader *fragment_shader =
      SDL_CreateGPUShader(device, &fragment_shader_info);
  if (!fragment_shader) {
    SDL_Log("SDL_CreateGPUShader fragment failed: %s", SDL_GetError());
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GPUVertexBufferDescription vertex_buffer_description = {};
  vertex_buffer_description.slot = 0;
  vertex_buffer_description.pitch = sizeof(Vertex);
  vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vertex_buffer_description.instance_step_rate = 0;

  SDL_GPUVertexAttribute vertex_attributes[2] = {};
  vertex_attributes[0].location = 0;
  vertex_attributes[0].buffer_slot = 0;
  vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  vertex_attributes[0].offset = offsetof(Vertex, position);
  vertex_attributes[1].location = 1;
  vertex_attributes[1].buffer_slot = 0;
  vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  vertex_attributes[1].offset = offsetof(Vertex, normal);

  SDL_GPUVertexInputState vertex_input_state = {};
  vertex_input_state.vertex_buffer_descriptions =
      &vertex_buffer_description;
  vertex_input_state.num_vertex_buffers = 1;
  vertex_input_state.vertex_attributes = vertex_attributes;
  vertex_input_state.num_vertex_attributes = 2;

  SDL_GPURasterizerState rasterizer_state = {};
  rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  rasterizer_state.enable_depth_bias = false;
  rasterizer_state.enable_depth_clip = true;

  SDL_GPUMultisampleState multisample_state = {};
  multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  multisample_state.enable_mask = false;

  SDL_GPUDepthStencilState depth_stencil_state = {};
  depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  depth_stencil_state.enable_depth_test = true;
  depth_stencil_state.enable_depth_write = true;
  depth_stencil_state.enable_stencil_test = false;

  SDL_GPUColorTargetBlendState blend_state = {};
  blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
  blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
  blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                                 SDL_GPU_COLORCOMPONENT_G |
                                 SDL_GPU_COLORCOMPONENT_B |
                                 SDL_GPU_COLORCOMPONENT_A;
  blend_state.enable_blend = false;
  blend_state.enable_color_write_mask = true;

  SDL_GPUColorTargetDescription color_target_desc = {};
  color_target_desc.format = SDL_GetGPUSwapchainTextureFormat(device, window);
  color_target_desc.blend_state = blend_state;

  SDL_GPUGraphicsPipelineTargetInfo target_info = {};
  target_info.color_target_descriptions = &color_target_desc;
  target_info.num_color_targets = 1;
  target_info.has_depth_stencil_target = true;
  target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.vertex_shader = vertex_shader;
  pipeline_info.fragment_shader = fragment_shader;
  pipeline_info.vertex_input_state = vertex_input_state;
  pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pipeline_info.rasterizer_state = rasterizer_state;
  pipeline_info.multisample_state = multisample_state;
  pipeline_info.depth_stencil_state = depth_stencil_state;
  pipeline_info.target_info = target_info;

  SDL_GPUGraphicsPipeline *pipeline =
      SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
  if (!pipeline) {
    SDL_Log("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GPUBufferCreateInfo vertex_buffer_info = {};
  vertex_buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  vertex_buffer_info.size =
      static_cast<Uint32>(mesh.vertices.size() * sizeof(Vertex));
  SDL_GPUBuffer *vertex_buffer =
      SDL_CreateGPUBuffer(device, &vertex_buffer_info);
  if (!vertex_buffer) {
    SDL_Log("SDL_CreateGPUBuffer vertex failed: %s", SDL_GetError());
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GPUBufferCreateInfo index_buffer_info = {};
  index_buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  index_buffer_info.size =
      static_cast<Uint32>(mesh.indices.size() * sizeof(uint32_t));
  SDL_GPUBuffer *index_buffer =
      SDL_CreateGPUBuffer(device, &index_buffer_info);
  if (!index_buffer) {
    SDL_Log("SDL_CreateGPUBuffer index failed: %s", SDL_GetError());
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  Uint32 vertex_bytes =
      static_cast<Uint32>(mesh.vertices.size() * sizeof(Vertex));
  Uint32 index_bytes =
      static_cast<Uint32>(mesh.indices.size() * sizeof(uint32_t));
  SDL_GPUTransferBufferCreateInfo transfer_info = {};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = vertex_bytes + index_bytes;
  SDL_GPUTransferBuffer *transfer_buffer =
      SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (!transfer_buffer) {
    SDL_Log("SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUBuffer(device, index_buffer);
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  void *transfer_memory =
      SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
  if (!transfer_memory) {
    SDL_Log("SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    SDL_ReleaseGPUBuffer(device, index_buffer);
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUShader(device, fragment_shader);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::memcpy(transfer_memory, mesh.vertices.data(), vertex_bytes);
  std::memcpy(static_cast<uint8_t *>(transfer_memory) + vertex_bytes,
              mesh.indices.data(), index_bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

  SDL_GPUCommandBuffer *upload_command_buffer =
      SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(upload_command_buffer);
  SDL_GPUTransferBufferLocation vertex_source = {transfer_buffer, 0};
  SDL_GPUBufferRegion vertex_destination = {vertex_buffer, 0, vertex_bytes};
  SDL_UploadToGPUBuffer(copy_pass, &vertex_source, &vertex_destination,
                        false);
  SDL_GPUTransferBufferLocation index_source = {transfer_buffer, vertex_bytes};
  SDL_GPUBufferRegion index_destination = {index_buffer, 0, index_bytes};
  SDL_UploadToGPUBuffer(copy_pass, &index_source, &index_destination, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(upload_command_buffer);

  SDL_GPUTexture *depth_texture = nullptr;
  Uint32 depth_width = 0;
  Uint32 depth_height = 0;

  Uint64 start_ticks = SDL_GetTicks();
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }
    if (options.timeout_seconds > 0.0) {
      Uint64 elapsed = SDL_GetTicks() - start_ticks;
      if (elapsed >=
          static_cast<Uint64>(options.timeout_seconds * 1000.0)) {
        running = false;
      }
    }

    SDL_GPUCommandBuffer *command_buffer =
        SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUTexture *swapchain_texture = nullptr;
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window,
                                               &swapchain_texture,
                                               &swapchain_width,
                                               &swapchain_height)) {
      SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s",
              SDL_GetError());
      SDL_SubmitGPUCommandBuffer(command_buffer);
      continue;
    }
    if (!swapchain_texture) {
      SDL_SubmitGPUCommandBuffer(command_buffer);
      continue;
    }

    if (!depth_texture || depth_width != swapchain_width ||
        depth_height != swapchain_height) {
      if (depth_texture) {
        SDL_ReleaseGPUTexture(device, depth_texture);
      }
      depth_texture =
          CreateDepthTexture(device, swapchain_width, swapchain_height);
      depth_width = swapchain_width;
      depth_height = swapchain_height;
      if (!depth_texture) {
        SDL_Log("Failed to create depth texture: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(command_buffer);
        continue;
      }
    }

    float aspect = swapchain_height > 0
                       ? static_cast<float>(swapchain_width) /
                             static_cast<float>(swapchain_height)
                       : 1.0f;
    glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(60.0f), aspect, 0.1f, mesh.radius * 6.0f);
    projection[1][1] *= -1.0f;
    float distance = mesh.radius * 2.5f;
    glm::vec3 eye = mesh.center + glm::vec3(0.0f, mesh.radius, distance);
    glm::mat4 view = glm::lookAt(eye, mesh.center, glm::vec3(0.0f, 1.0f, 0.0f));
    float angle = static_cast<float>(SDL_GetTicks()) * 0.0004f;
    glm::mat4 base_model =
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / mesh.radius)) *
        glm::translate(glm::mat4(1.0f), -mesh.center);
    glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) *
        base_model;

    VertexUniforms vertex_uniforms = {};
    vertex_uniforms.mvp = projection * view * model;
    vertex_uniforms.model = model;
    FragmentUniforms fragment_uniforms = {};
    fragment_uniforms.light_dir =
        glm::vec4(glm::normalize(glm::vec3(0.3f, 1.0f, 0.4f)), 0.0f);
    fragment_uniforms.base_color = mesh.base_color;

    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms,
                                 sizeof(vertex_uniforms));
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms,
                                   sizeof(fragment_uniforms));

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = swapchain_texture;
    color_target.clear_color = SDL_FColor{0.05f, 0.07f, 0.1f, 1.0f};
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = depth_texture;
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_stencil = 0;

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(
        command_buffer, &color_target, 1, &depth_target);
    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_GPUViewport viewport = {0.0f, 0.0f,
                                static_cast<float>(swapchain_width),
                                static_cast<float>(swapchain_height), 0.0f,
                                1.0f};
    SDL_SetGPUViewport(render_pass, &viewport);

    SDL_GPUBufferBinding vertex_binding = {vertex_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_GPUBufferBinding index_binding = {index_buffer, 0};
    SDL_BindGPUIndexBuffer(render_pass, &index_binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass,
                                 static_cast<Uint32>(mesh.indices.size()), 1,
                                 0, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    SDL_SubmitGPUCommandBuffer(command_buffer);
  }

  if (depth_texture) {
    SDL_ReleaseGPUTexture(device, depth_texture);
  }
  SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
  SDL_ReleaseGPUBuffer(device, index_buffer);
  SDL_ReleaseGPUBuffer(device, vertex_buffer);
  SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
  SDL_ReleaseGPUShader(device, fragment_shader);
  SDL_ReleaseGPUShader(device, vertex_shader);
  SDL_ReleaseWindowFromGPUDevice(device, window);
  SDL_DestroyGPUDevice(device);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
