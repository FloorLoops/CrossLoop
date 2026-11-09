#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_FAIL = 2, NVS_READONLY = 0, NVS_READWRITE = 1;
namespace fake_nvs {
using Values = std::map<std::string, std::vector<uint8_t>>;
inline std::map<std::string, Values> namespaces;
struct Handle {
  std::string name;
  Values pending;
};
inline std::map<unsigned, Handle> handles;
inline unsigned nextHandle = 0;
inline bool failOpen = false, failRead = false, failWrite = false, failCommit = false;
inline unsigned commits = 0;
}  // namespace fake_nvs
inline int nvs_open(const char* name, int mode, nvs_handle_t* handle) {
  if (fake_nvs::failOpen) return ESP_FAIL;
  if (!fake_nvs::namespaces.contains(name) && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
  *handle = ++fake_nvs::nextHandle;
  fake_nvs::handles[*handle] = {name, fake_nvs::namespaces[name]};
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) { fake_nvs::handles.erase(handle); }
inline int nvs_get_blob(nvs_handle_t handle, const char* key, void* output, size_t* size) {
  if (fake_nvs::failRead || !fake_nvs::handles.contains(handle)) return ESP_FAIL;
  auto& values = fake_nvs::handles.at(handle).pending;
  if (!values.contains(key)) return ESP_ERR_NVS_NOT_FOUND;
  const auto& value = values.at(key);
  if (!output) {
    *size = value.size();
    return ESP_OK;
  }
  if (*size < value.size()) {
    *size = value.size();
    return ESP_FAIL;
  }
  memcpy(output, value.data(), value.size());
  *size = value.size();
  return ESP_OK;
}
inline int nvs_get_str(nvs_handle_t handle, const char* key, char* output, size_t* size) {
  return nvs_get_blob(handle, key, output, size);
}
inline int nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t length) {
  if (fake_nvs::failWrite || !fake_nvs::handles.contains(handle)) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  fake_nvs::handles.at(handle).pending[key] = std::vector<uint8_t>(bytes, bytes + length);
  return ESP_OK;
}
inline int nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
  if (strlen(value) + 1 > 4000) return ESP_FAIL;
  return nvs_set_blob(handle, key, value, strlen(value) + 1);
}
inline int nvs_erase_key(nvs_handle_t handle, const char* key) {
  if (fake_nvs::failWrite || !fake_nvs::handles.contains(handle)) return ESP_FAIL;
  return fake_nvs::handles.at(handle).pending.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
inline int nvs_commit(nvs_handle_t handle) {
  if (fake_nvs::failCommit || !fake_nvs::handles.contains(handle)) return ESP_FAIL;
  const auto& value = fake_nvs::handles.at(handle);
  fake_nvs::namespaces[value.name] = value.pending;
  ++fake_nvs::commits;
  return ESP_OK;
}
