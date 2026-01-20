#include <nlohmann/json.hpp>
#include <tiny_gltf.h>

#include <cstring>
#include <iostream>

int main() {
  const char *gltf =
      "{\"asset\":{\"version\":\"2.0\"},"
      "\"scenes\":[{\"nodes\":[0]}],"
      "\"nodes\":[{\"name\":\"Root\"}],"
      "\"scene\":0}";

  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::string err;
  std::string warn;
  bool ok = loader.LoadASCIIFromString(
      &model,
      &err,
      &warn,
      gltf,
      std::strlen(gltf),
      "");

  if (!warn.empty()) {
    std::cout << "warn: " << warn << "\n";
  }
  if (!err.empty()) {
    std::cout << "err: " << err << "\n";
  }

  std::cout << (ok ? "Loaded" : "Failed")
            << " nodes=" << model.nodes.size() << "\n";
  return ok ? 0 : 1;
}
