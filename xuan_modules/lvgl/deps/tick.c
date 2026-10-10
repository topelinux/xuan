#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>

uint32_t custom_tick_get(void) {
    static uint64_t start_ms = 0;
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = ((uint64_t)tv_now.tv_sec * 1000000 + (uint64_t)tv_now.tv_usec) / 1000;
    if (start_ms == 0) {
        start_ms = now_ms;
    }
    return (uint32_t)(now_ms - start_ms);
}
