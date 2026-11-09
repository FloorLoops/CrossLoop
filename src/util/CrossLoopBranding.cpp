#include "CrossLoopBranding.h"

#include <HalClock.h>

#include "CrossLoopHoliday.h"
#include "CrossPointSettings.h"
#include "images/CrossLoopLogos.h"

namespace crossloop {

const uint8_t* currentLogo() {
  tm local{};
  if (!SETTINGS.holidayBranding || !halClock.localTime(local)) return CROSSLOOP_STANDARD_120;
  switch (holidayForDate(true, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_wday)) {
    case Holiday::NewYear:
      return CROSSLOOP_NEWYEAR_120;
    case Holiday::Valentine:
      return CROSSLOOP_VALENTINE_120;
    case Holiday::StPatrick:
      return CROSSLOOP_STPATRICK_120;
    case Holiday::Independence:
      return CROSSLOOP_INDEPENDENCE_120;
    case Holiday::Halloween:
      return CROSSLOOP_HALLOWEEN_120;
    case Holiday::Thanksgiving:
      return CROSSLOOP_THANKSGIVING_120;
    case Holiday::Winter:
      return CROSSLOOP_WINTER_120;
    default:
      return CROSSLOOP_STANDARD_120;
  }
}

}  // namespace crossloop
