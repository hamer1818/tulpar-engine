#include "platform/time.hpp"

#include <time.h>

namespace tulpar::engine::platform {

uint64_t now_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Howard Hinnant, days_from_civil (proleptik Gregoryen; 1970-01-01 = 0).
static int32_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int32_t)(era * 146097 + (int)doe - 719468);
}

int32_t local_day_index(int64_t epoch_s) {
  time_t t = epoch_s < 0 ? time(nullptr) : (time_t)epoch_s;
  struct tm tmv{};
#if defined(_WIN32)
  if (localtime_s(&tmv, &t) != 0) return (int32_t)(t / 86400);
#else
  if (localtime_r(&t, &tmv) == nullptr) return (int32_t)(t / 86400);
#endif
  return days_from_civil(tmv.tm_year + 1900, (unsigned)(tmv.tm_mon + 1), (unsigned)tmv.tm_mday);
}

} // namespace tulpar::engine::platform
