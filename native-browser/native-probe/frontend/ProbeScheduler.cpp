/* Fixed-capacity NetSurf scheduler for the single browser task. GPL-2.0. */
#include "ProbeScheduler.h"

#include <climits>
#include <cstddef>

namespace {
struct Slot {
  void (*callback)(void*) = nullptr;
  void* context = nullptr;
  uint32_t deadline = 0;
  uint64_t order = 0;
  bool active = false;
};

Slot slots[PROBE_SCHEDULER_CAPACITY]{};
uint32_t clock_ms = 0;
uint64_t next_order = 1;
uint32_t generation = 0;
bool initialized = false;
bool pumping = false;

/* Correct across uint32 wrap while the caller samples time at least once per
 * INT32_MAX milliseconds. Every scheduled delay is <= INT_MAX by API type. */
bool due(uint32_t now, uint32_t deadline) { return static_cast<uint32_t>(now - deadline) < 0x80000000u; }

void clear_slots() {
  for (Slot& slot : slots) slot = Slot{};
}

int find_pair(void (*callback)(void*), void* context) {
  for (int i = 0; i < PROBE_SCHEDULER_CAPACITY; ++i) {
    if (slots[i].active && slots[i].callback == callback && slots[i].context == context) return i;
  }
  return -1;
}

int find_empty() {
  for (int i = 0; i < PROBE_SCHEDULER_CAPACITY; ++i) {
    if (!slots[i].active) return i;
  }
  return -1;
}

int oldest_due(uint32_t now) {
  int selected = -1;
  uint32_t longest_late = 0;
  for (int i = 0; i < PROBE_SCHEDULER_CAPACITY; ++i) {
    const Slot& slot = slots[i];
    if (!slot.active || !due(now, slot.deadline)) continue;
    const uint32_t late = now - slot.deadline;
    if (selected < 0 || late > longest_late || (late == longest_late && slot.order < slots[selected].order)) {
      selected = i;
      longest_late = late;
    }
  }
  return selected;
}

probe_scheduler_pump_result pending_result(uint32_t now, unsigned executed) {
  probe_scheduler_pump_result result{NSERROR_OK, executed, false, UINT32_MAX};
  for (const Slot& slot : slots) {
    if (!slot.active) continue;
    if (due(now, slot.deadline)) {
      result.due_remaining = true;
      result.next_delay_ms = 0;
    } else if (!result.due_remaining) {
      const uint32_t wait = slot.deadline - now;
      if (wait < result.next_delay_ms) result.next_delay_ms = wait;
    }
  }
  return result;
}
}  // namespace

extern "C" void probe_scheduler_begin(uint32_t now_ms) {
  ++generation;
  clear_slots();
  clock_ms = now_ms;
  next_order = 1;
  initialized = true;
  /* Do not clear pumping while a callback is on the stack: its pump must
   * observe the changed generation and stop after that callback returns. */
}

extern "C" void probe_scheduler_reset(void) {
  ++generation;
  clear_slots();
  initialized = false;
}

extern "C" nserror probe_scheduler_schedule(int delay_ms, void (*callback)(void*), void* context) {
  if (!initialized || callback == nullptr) return NSERROR_BAD_PARAMETER;
  const int existing = find_pair(callback, context);
  if (delay_ms < 0) {
    if (existing < 0) return NSERROR_NOT_FOUND;
    slots[existing] = Slot{};
    return NSERROR_OK;
  }
  const int index = existing >= 0 ? existing : find_empty();
  if (index < 0) return NSERROR_NOMEM;
  slots[index] = Slot{callback, context, clock_ms + static_cast<uint32_t>(delay_ms), next_order++, true};
  return NSERROR_OK;
}

extern "C" probe_scheduler_pump_result probe_scheduler_pump(uint32_t now_ms) {
  if (!initialized || pumping) return {NSERROR_INVALID, 0, false, UINT32_MAX};
  if (static_cast<uint32_t>(now_ms - clock_ms) > INT32_MAX) return {NSERROR_INVALID, 0, false, UINT32_MAX};
  clock_ms = now_ms;
  pumping = true;
  const uint32_t start_generation = generation;
  unsigned executed = 0;
  while (executed < PROBE_SCHEDULER_PUMP_LIMIT) {
    const int index = oldest_due(now_ms);
    if (index < 0) break;
    void (*callback)(void*) = slots[index].callback;
    void* context = slots[index].context;
    slots[index] = Slot{};  // remove before callback; it may schedule this pair again
    ++executed;
    callback(context);
    if (generation != start_generation) break;
  }
  pumping = false;
  if (generation != start_generation) return {NSERROR_STOPPED, executed, false, UINT32_MAX};
  return pending_result(now_ms, executed);
}
