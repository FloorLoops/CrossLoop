/* Fixed-capacity NetSurf scheduler for the single browser task. GPL-2.0. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "utils/errors.h"

enum { PROBE_SCHEDULER_CAPACITY = 64, PROBE_SCHEDULER_PUMP_LIMIT = 16 };

typedef struct probe_scheduler_pump_result {
  nserror status;
  unsigned callbacks_executed;
  bool due_remaining;
  /* UINT32_MAX means no pending callback; zero means call pump again soon. */
  uint32_t next_delay_ms;
} probe_scheduler_pump_result;

/* Begin a fresh browser lifetime with the caller's current 32-bit ms clock.
 * Only one task may call these APIs. No timer task or SDK clock is read. */
void probe_scheduler_begin(uint32_t now_ms);
void probe_scheduler_reset(void);

/* Exact gui_misc_table.schedule signature. Additional calls with the same
 * callback/context replace its deadline. Negative delay cancels that pair. */
nserror probe_scheduler_schedule(int delay_ms, void (*callback)(void*), void* context);

/* Update clock and execute at most PROBE_SCHEDULER_PUMP_LIMIT due callbacks.
 * Call again while due_remaining is true; pending events are never dropped. */
probe_scheduler_pump_result probe_scheduler_pump(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
