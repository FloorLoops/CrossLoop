#pragma once
#include <ctime>
#ifdef _MSC_VER
inline tm* localtime_r(const time_t* time, tm* out) { return localtime_s(out, time) == 0 ? out : nullptr; }
#endif
inline void delay(unsigned) {}
