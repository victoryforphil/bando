#include <spdlog/spdlog.h>

int main() {
  spdlog::set_level(spdlog::level::info);
  spdlog::info("Hello from spdlog {}", SPDLOG_VER_MAJOR);
  return 0;
}
