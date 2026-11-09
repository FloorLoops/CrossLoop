#pragma once
#include <cstddef>
class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes;
  };
  static inline HeapStats stats{1024 * 1024, 1024 * 1024, 1024 * 1024, 1024 * 1024};
  static HeapStats getInternalHeap() { return stats; }
};
