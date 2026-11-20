#pragma once
#include <stddef.h>
#include <stdint.h>

#include "utils/errors.h"
#ifdef __cplusplus
extern "C" {
#endif
nserror probe_resource_get(const char* name, const uint8_t** data, size_t* length);
nserror probe_resource_release(const uint8_t* data);
#ifdef __cplusplus
}
#endif
