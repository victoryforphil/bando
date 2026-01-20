#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>

int main() {
  JPH::RegisterDefaultAllocator();

  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();

  JPH::TempAllocatorImpl temp_allocator(8 * 1024 * 1024);
  JPH::JobSystemThreadPool job_system(1024, 1024, 1);

  (void)temp_allocator;
  (void)job_system;

  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
  return 0;
}
