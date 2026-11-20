#pragma once
#include <HalStorage.h>

#include <cstdlib>
#include <memory>
namespace dream_memory {
inline unsigned allocations = 0;
inline int failAfter = -1;
}  // namespace dream_memory
class HalMemory {
 public:
  struct PsramDeleter {
    void operator()(uint8_t* value) const {
      if (value) {
        --dream_memory::allocations;
        std::free(value);
      }
    }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramDeleter>;
  static PsramBuffer allocatePsram(size_t size) {
    if (fake::fail(dream_memory::failAfter)) return {};
    auto* memory = static_cast<uint8_t*>(std::malloc(size));
    if (memory) ++dream_memory::allocations;
    return PsramBuffer(memory);
  }
};
