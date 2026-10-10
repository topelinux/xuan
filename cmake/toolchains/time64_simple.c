/* 简化版 time64 stub - 只调用现有函数 */
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <dlfcn.h>

time_t __time64(time_t *t) { return time(t); }
int __clock_gettime64(clockid_t c, struct timespec *t) { return clock_gettime(c, t); }
int __clock_getres_time64(clockid_t c, struct timespec *t) { return clock_getres(c, t); }
int __clock_settime64(clockid_t c, const struct timespec *t) { return clock_settime(c, t); }
int __clock_nanosleep_time64(clockid_t c, int f, const struct timespec *r, struct timespec *rm) { return clock_nanosleep(c, f, r, rm); }
int __nanosleep_time64(const struct timespec *r, struct timespec *rm) { return nanosleep(r, rm); }
int __gettimeofday_time64(struct timeval *tv, struct timezone *tz) { return gettimeofday(tv, tz); }
time_t __mktime64(struct tm *t) { return mktime(t); }
time_t __timegm_time64(struct tm *t) { return timegm(t); }
struct tm *__gmtime64_r(const time_t *t, struct tm *r) { return gmtime_r(t, r); }
struct tm *__localtime64_r(const time_t *t, struct tm *r) { return localtime_r(t, r); }
int __fstat_time64(int fd, struct stat *s) { return fstat(fd, s); }
int __fstatat_time64(int fd, const char *p, struct stat *s, int f) { return fstatat(fd, p, s, f); }
int __lstat_time64(const char *p, struct stat *s) { return lstat(p, s); }
int __stat_time64(const char *p, struct stat *s) { return stat(p, s); }
int __futimens_time64(int fd, const struct timespec *t) { return futimens(fd, t); }
int __utimensat_time64(int fd, const char *p, const struct timespec *t, int f) { return utimensat(fd, p, t, f); }
int __select_time64(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { return select(n, r, w, e, t); }
int __getrusage_time64(int w, struct rusage *u) { return getrusage(w, u); }
int __getitimer_time64(int w, struct itimerval *v) { return getitimer(w, v); }
int __setitimer_time64(int w, const struct itimerval *v, struct itimerval *o) { return setitimer(w, v, o); }
int __timespec_get_time64(struct timespec *t, int b) { return timespec_get(t, b); }
int __pthread_cond_timedwait_time64(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { return pthread_cond_timedwait(c, m, t); }
int __pthread_mutex_timedlock_time64(pthread_mutex_t *m, const struct timespec *t) { return pthread_mutex_timedlock(m, t); }
void *__dlsym_time64(void *h, const char *n) { return dlsym(h, n); }
void *__dlsym_redir_time64(void *h, const char *n) { return dlsym(h, n); }
int pthread_getname_np(pthread_t t, char *n, size_t l) { if (n && l > 0) n[0] = 0; return 0; }
