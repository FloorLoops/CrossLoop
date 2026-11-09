#pragma once

#include <HalStorage.h>

#include <string>

// FAT has no atomic replace. Keep one previous complete version under .bak.
// Readers must validate the primary and fall back to .bak after interruption.
// A caller must validate any existing primary before setting primaryValid.
inline bool publishRecoverableFile(const std::string& temporary, const std::string& destination, bool primaryValid) {
  const std::string backup = destination + ".bak";
  for (const auto* candidate : {&temporary, &destination, &backup}) {
    auto file = Storage.open(candidate->c_str());
    if (file && file.isDirectory()) return false;
  }
  if (temporary == destination || temporary == backup) return false;
  if (Storage.exists(destination.c_str())) {
    if (primaryValid) {
      if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
      if (!Storage.rename(destination.c_str(), backup.c_str())) return false;
    } else if (!Storage.remove(destination.c_str()))
      return false;
  }
  if (Storage.rename(temporary.c_str(), destination.c_str())) return true;
  // Best effort restores the normal path; if it also fails, .bak is retained
  // for the reader's recovery path. Never delete that remaining good copy.
  if (!Storage.exists(destination.c_str()) && Storage.exists(backup.c_str())) {
    Storage.rename(backup.c_str(), destination.c_str());
  }
  return false;
}
