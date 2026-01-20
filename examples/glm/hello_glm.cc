#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>

int main() {
  glm::vec3 position(1.0f, 2.0f, 3.0f);
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
  std::cout << "Hello glm, tx=" << transform[3].x << "\n";
  return 0;
}
