#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "HalStorage.h"

inline bool allocationFails(size_t bytes) {
  if (fake::failAllocBytes == bytes) {
    fake::failAllocBytes = 0;
    fake::failureTriggered = true;
    return true;
  }
  return fake::fail(fake::failAlloc);
}

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (allocationFails(sizeof(T))) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (allocationFails(count * sizeof(std::remove_extent_t<T>))) return nullptr;
  return std::make_unique<T>(count);
}
