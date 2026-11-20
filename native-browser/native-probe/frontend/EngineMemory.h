/* Task-scoped PSRAM allocator for selected NetSurf C translation units. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct engine_memory_stats {
  size_t limit_bytes;
  size_t live_bytes; /* Full charged blocks, including header/alignment. */
  size_t peak_bytes;
  size_t live_allocations;
  size_t failed_allocations;
  bool active;
};

/* A new limit may be installed only when no blocks are live. No implicit free. */
bool engine_memory_begin(size_t limit_bytes);
struct engine_memory_stats engine_memory_snapshot(void);

void* engine_memory_malloc(size_t size);
void* engine_memory_calloc(size_t count, size_t size);
void* engine_memory_realloc(void* ptr, size_t size);
void engine_memory_free(void* ptr);
char* engine_memory_strdup(const char* text);
char* engine_memory_strndup(const char* text, size_t max_bytes);

#ifdef __cplusplus
}
#endif
