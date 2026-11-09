#pragma once

#include <map>
#include <optional>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  bool success = true;
  std::string genre, series;
  std::optional<float> seriesIndex;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  struct SyncMetadata {
    std::string title, author, genre, series;
    std::optional<float> seriesIndex;
  };
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    return true;
  }
  bool loadSyncMetadata(SyncMetadata& result) {
    if (!loadMetadata(result.title, result.author)) return false;
    const auto& data = bookMetadata[path];
    result.genre = data.genre;
    result.series = data.series;
    result.seriesIndex = data.seriesIndex;
    return true;
  }
};
