/*
 * time64_stub.c - 最简单的 time64 符号 stub
 * 只提供符号，不做实际转换 - 避免递归调用
 */
#define _GNU_SOURCE
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>

/* 这些 stub 直接返回错误，让程序自己处理 */
#define STUB_RET_0 do { return 0; } while(0)
#define STUB_RET_1 do { return -1; } while(0)
#define STUB_RET_NULL do { return NULL; } while(0)
#define STUB_RET_NEG do { errno = ENOSYS; return -1; } while(0)

time_t __time64(time_t *t) {
    time_t now = time(NULL);
    if (t) *t = now;
    return now;
}

int __clock_gettime64(clockid_t clock, struct timespec *ts) {
    if (!ts) return -1;
    return clock_gettime(clock, ts);
}

int __clock_getres_time64(clockid_t clock, struct timespec *ts) {
    if (!ts) return -1;
    return clock_getres(clock, ts);
}

int __clock_settime64(clockid_t clock, const struct timespec *ts) {
    if (!ts) return -1;
    return clock_settime(clock, ts);
}

int __clock_nanosleep_time64(clockid_t clock, int flags, const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    return clock_nanosleep(clock, flags, req, rem);
}

int __nanosleep_time64(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    return nanosleep(req, rem);
}

int __gettimeofday_time64(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -1;
    return gettimeofday(tv, tz);
}

int __adjtime64(const struct timeval *in, struct timeval *out) {
    STUB_RET_NEG;
}

int __getrusage_time64(int who, struct rusage *usage) {
    if (!usage) return -1;
    return getrusage(who, usage);
}

time_t __mktime64(struct tm *tm) {
    if (!tm) return -1;
    return mktime(tm);
}

time_t __timegm_time64(struct tm *tm) {
    if (!tm) return -1;
    time_t t = timegm(tm);
    return t;
}

struct tm *__gmtime64_r(const time_t *t, struct tm *tm) {
    if (!t || !tm) return NULL;
    return gmtime_r(t, tm);
}

struct tm *__localtime64_r(const time_t *t, struct tm *tm) {
    if (!t || !tm) return NULL;
    return localtime_r(t, tm);
}

char *__ctime64_r(const time_t *t, char *buf) {
    if (!t || !buf) return NULL;
    return ctime_r(t, buf);
}

double __difftime64(time_t t1, time_t t2) {
    return difftime(t1, t2);
}

int __fstat_time64(int fd, struct stat *st) {
    if (fd < 0 || !st) return -1;
    return fstat(fd, st);
}

int __fstatat_time64(int fd, const char *path, struct stat *st, int flags) {
    if (fd < 0 || !st) return -1;
    return fstatat(fd, path, st, flags);
}

int __lstat_time64(const char *path, struct stat *st) {
    if (!path || !st) return -1;
    return lstat(path, st);
}

int __stat_time64(const char *path, struct stat *st) {
    if (!path || !st) return -1;
    return stat(path, st);
}

int __futimens_time64(int fd, const struct timespec times[2]) {
    if (fd < 0) return -1;
    return futimens(fd, times);
}

int __utimensat_time64(int fd, const char *path, const struct timespec times[2], int flags) {
    if (fd < 0 && !path) return -1;
    return utimensat(fd, path, times, flags);
}

int __select_time64(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    return select(nfds, readfds, writefds, exceptfds, timeout);
}

int __recvmmsg_time64(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags, struct timespec *timeout) {
    if (sockfd < 0 || !msgvec || vlen == 0) return -1;
    // 简化实现
    return recvmmsg(sockfd, msgvec, vlen, flags, NULL);
}

int __timer_settime64(timer_t timerid, int flags, const struct itimerspec *value, struct itimerspec *ovalue) {
    if (!value) return -1;
    return timer_settime(timerid, flags, value, ovalue);
}

int __timer_gettime64(timer_t timerid, struct itimerspec *value) {
    if (!value) return -1;
    return timer_gettime(timerid, value);
}

int __getitimer_time64(int which, struct itimerval *value) {
    if (!value) return -1;
    return getitimer(which, value);
}

int __setitimer_time64(int which, const struct itimerval *value, struct itimerval *ovalue) {
    if (!value) return -1;
    return setitimer(which, value, ovalue);
}

int __timespec_get_time64(struct timespec *ts, int base) {
    if (!ts) return -1;
    return timespec_get(ts, base);
}

/* pthread time64 */
int __pthread_cond_timedwait_time64(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) {
    if (!cond || !mutex || !abstime) return -1;
    return pthread_cond_timedwait(cond, mutex, abstime);
}

int __pthread_mutex_timedlock_time64(pthread_mutex_t *mutex, const struct timespec *abstime) {
    if (!mutex || !abstime) return -1;
    return pthread_mutex_timedlock(mutex, abstime);
}

/* dlsym time64 */
void *__dlsym_time64(void *handle, const char *name) {
    return dlsym(handle, name);
}

void *__dlsym_redir_time64(void *handle, const char *name) {
    return dlsym(handle, name);
}

/* pthread getname_np - needed by libuv */
int pthread_getname_np(pthread_t thread, char *name, size_t len) {
    if (name && len > 0) name[0] = '\0';
    return 0;
}