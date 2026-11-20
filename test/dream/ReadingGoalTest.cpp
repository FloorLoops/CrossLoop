#include <nvs.h>

#include <cassert>
#include <cstdio>
#include <limits>

#include "util/ReadingGoal.h"
int main() {
  using namespace crossloop;
  uint16_t goal = 0;
  assert(loadReadingGoal(goal) && goal == DEFAULT_READING_GOAL);
  assert(!saveReadingGoal(0) && !saveReadingGoal(121));
  auto& ns = fake_nvs::namespaces["crossloop_stats"];
  ns["stats"] = {1, 2, 3, 4};
  const auto original = ns["stats"];
  assert(saveReadingGoal(35) && loadReadingGoal(goal) && goal == 35);
  const auto commits = fake_nvs::commits;
  assert(saveReadingGoal(35) && fake_nvs::commits == commits);
  for (int fault = 0; fault < 4; ++fault) {
    fake_nvs::failOpen = fault == 0;
    fake_nvs::failRead = fault == 1;
    fake_nvs::failWrite = fault == 2;
    fake_nvs::failCommit = fault == 3;
    assert(!saveReadingGoal(40));
    fake_nvs::failOpen = fake_nvs::failRead = fake_nvs::failWrite = fake_nvs::failCommit = false;
    assert(loadReadingGoal(goal) && goal == 35 && ns["stats"] == original);
    assert(fake_nvs::handles.empty());
  }
  ns["goal"][0] = 2;
  const auto unknown = ns["goal"];
  assert(!loadReadingGoal(goal) && !saveReadingGoal(40) && ns["goal"] == unknown);
  assert(readingGoalPercent(0, 20) == 0 && readingGoalPercent(600, 20) == 50);
  assert(readingGoalPercent(1200, 20) == 100 && readingGoalPercent(UINT32_MAX, 20) == 100);
  assert(readingGoalPercent(10, 0) == 0 && fake_nvs::handles.empty());
  std::puts("Reading goal: defaults, ranges, corruption, NVS faults and preservation checks passed");
}
