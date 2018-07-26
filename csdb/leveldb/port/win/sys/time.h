#include <time.h>

struct timeval;
struct timezone;

#ifdef __cplusplus
extern "C" {
#endif

int gettimeofday(struct timeval* tp, struct timezone* tzp);
struct tm* localtime_r(const time_t *clock, struct tm *result);

#ifdef __cplusplus
}
#endif

//#define strerror strerror_

