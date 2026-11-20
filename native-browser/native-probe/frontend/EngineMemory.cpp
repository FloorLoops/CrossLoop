/* Selected NetSurf allocations only. No process-wide replacement or fallback. */
#include "EngineMemory.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "HalMemory.h"

namespace {
constexpr size_t kAlignment = alignof(std::max_align_t);
constexpr uint32_t kLiveMagic = 0x4e534d45;  // NSME
static_assert((kAlignment & (kAlignment - 1)) == 0);

struct alignas(std::max_align_t) Header {
  uint8_t* base;
  size_t charged;
  size_t requested;
  uint32_t magic;
};
static_assert(sizeof(Header) % kAlignment == 0);

engine_memory_stats stats = {};

bool add_overflows(size_t a, size_t b) { return a > std::numeric_limits<size_t>::max() - b; }

Header* header_from_payload(void* ptr) {
  return reinterpret_cast<Header*>(static_cast<uint8_t*>(ptr) - sizeof(Header));
}

void record_failure() {
  if (stats.failed_allocations < std::numeric_limits<size_t>::max()) ++stats.failed_allocations;
}

void* allocate(size_t requested) {
  if (requested == 0) return nullptr;
  if (!stats.active || add_overflows(requested, sizeof(Header)) ||
      add_overflows(requested + sizeof(Header), kAlignment - 1)) {
    record_failure();
    return nullptr;
  }
  const size_t charged = requested + sizeof(Header) + kAlignment - 1;
  if (charged > stats.limit_bytes - stats.live_bytes || stats.live_allocations == std::numeric_limits<size_t>::max()) {
    record_failure();
    return nullptr;
  }
  auto owner = HalMemory::allocatePsram(charged);
  if (!owner) {
    record_failure();
    return nullptr;
  }
  uint8_t* base = owner.release();
  const uintptr_t base_address = reinterpret_cast<uintptr_t>(base);
  if (base_address > std::numeric_limits<uintptr_t>::max() - (sizeof(Header) + kAlignment - 1)) {
    HalMemory::PsramDeleter{}(base);
    record_failure();
    return nullptr;
  }
  const uintptr_t start = base_address + sizeof(Header);
  const uintptr_t aligned = (start + kAlignment - 1) & ~(static_cast<uintptr_t>(kAlignment) - 1);
  auto* header = reinterpret_cast<Header*>(aligned - sizeof(Header));
  header->base = base;
  header->charged = charged;
  header->requested = requested;
  header->magic = kLiveMagic;
  stats.live_bytes += charged;
  ++stats.live_allocations;
  if (stats.live_bytes > stats.peak_bytes) stats.peak_bytes = stats.live_bytes;
  return reinterpret_cast<void*>(aligned);
}
}  // namespace

extern "C" {
bool engine_memory_begin(size_t limit_bytes) {
  if (stats.live_allocations != 0 || limit_bytes == 0 || HalMemory::getPsramHeap().totalBytes == 0) return false;
  stats = {.limit_bytes = limit_bytes,
           .live_bytes = 0,
           .peak_bytes = 0,
           .live_allocations = 0,
           .failed_allocations = 0,
           .active = true};
  return true;
}

struct engine_memory_stats engine_memory_snapshot(void) { return stats; }

void* engine_memory_malloc(size_t size) { return allocate(size); }

void* engine_memory_calloc(size_t count, size_t size) {
  if (count != 0 && size > std::numeric_limits<size_t>::max() / count) {
    record_failure();
    return nullptr;
  }
  const size_t bytes = count * size;
  void* ptr = allocate(bytes);
  if (ptr) std::memset(ptr, 0, bytes);
  return ptr;
}

void engine_memory_free(void* ptr) {
  if (!ptr) return;
  Header* header = header_from_payload(ptr);
  if (header->magic != kLiveMagic || !stats.active || header->charged > stats.live_bytes || stats.live_allocations == 0)
    return;
  uint8_t* base = header->base;
  const size_t charged = header->charged;
  header->magic = 0;
  stats.live_bytes -= charged;
  --stats.live_allocations;
  HalMemory::PsramDeleter{}(base);
}

void* engine_memory_realloc(void* ptr, size_t size) {
  if (!ptr) return allocate(size);
  if (size == 0) {
    engine_memory_free(ptr);
    return nullptr;
  }
  Header* old = header_from_payload(ptr);
  if (old->magic != kLiveMagic || !stats.active) {
    record_failure();
    return nullptr;
  }
  /* Old and new blocks coexist. An OOM leaves ptr and its charge intact. */
  void* replacement = allocate(size);
  if (!replacement) return nullptr;
  std::memcpy(replacement, ptr, size < old->requested ? size : old->requested);
  engine_memory_free(ptr);
  return replacement;
}

char* engine_memory_strdup(const char* text) {
  if (!text) {
    record_failure();
    return nullptr;
  }
  const size_t bytes = std::strlen(text);
  if (add_overflows(bytes, 1)) {
    record_failure();
    return nullptr;
  }
  char* copy = static_cast<char*>(allocate(bytes + 1));
  if (copy) std::memcpy(copy, text, bytes + 1);
  return copy;
}

char* engine_memory_strndup(const char* text, size_t max_bytes) {
  if (!text) {
    record_failure();
    return nullptr;
  }
  size_t bytes = 0;
  while (bytes < max_bytes && text[bytes] != '\0') ++bytes;
  if (add_overflows(bytes, 1)) {
    record_failure();
    return nullptr;
  }
  char* copy = static_cast<char*>(allocate(bytes + 1));
  if (copy) {
    std::memcpy(copy, text, bytes);
    copy[bytes] = '\0';
  }
  return copy;
}
}  // extern "C"
