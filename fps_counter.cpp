#include "mcpe_fps.h"
#include <time.h>
#include <stdint.h>
#include <pthread.h>

namespace {
struct FpsState {
    pthread_mutex_t mutex;
    uint64_t window_start_ns;
    uint64_t last_frame_ns;
    uint64_t total_frames;
    uint64_t window_frames;
    uint64_t completed_windows;
    int current;
    int average;
    int minimum;
    int maximum;
    FpsState() : window_start_ns(0), last_frame_ns(0), total_frames(0),
        window_frames(0), completed_windows(0), current(0), average(0), minimum(0), maximum(0) {
        pthread_mutex_init(&mutex, 0);
    }
};
FpsState g_state;

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
}

extern "C" void mcpe_fps_on_frame(void) {
    const uint64_t now = now_ns();
    pthread_mutex_lock(&g_state.mutex);
    if (g_state.window_start_ns == 0) {
        g_state.window_start_ns = now;
        g_state.last_frame_ns = now;
        g_state.window_frames = 1;
        g_state.total_frames = 1;
        pthread_mutex_unlock(&g_state.mutex);
        return;
    }
    ++g_state.window_frames;
    ++g_state.total_frames;
    const uint64_t elapsed = now - g_state.window_start_ns;
    if (elapsed >= 1000000000ULL) {
        int fps = (int)((g_state.window_frames * 1000000000ULL) / elapsed);
        g_state.current = fps;
        if (g_state.minimum == 0 || fps < g_state.minimum) g_state.minimum = fps;
        if (fps > g_state.maximum) g_state.maximum = fps;
        // Running average based on completed one-second measurement windows.
        ++g_state.completed_windows;
        g_state.average += (fps - g_state.average) / (int)g_state.completed_windows;
        g_state.window_start_ns = now;
        g_state.window_frames = 0;
    }
    g_state.last_frame_ns = now;
    pthread_mutex_unlock(&g_state.mutex);
}

extern "C" int mcpe_fps_get_current(void) {
    pthread_mutex_lock(&g_state.mutex); int v = g_state.current; pthread_mutex_unlock(&g_state.mutex); return v;
}
extern "C" int mcpe_fps_get_average(void) {
    pthread_mutex_lock(&g_state.mutex); int v = g_state.average; pthread_mutex_unlock(&g_state.mutex); return v;
}
extern "C" int mcpe_fps_get_minimum(void) {
    pthread_mutex_lock(&g_state.mutex); int v = g_state.minimum; pthread_mutex_unlock(&g_state.mutex); return v;
}
extern "C" int mcpe_fps_get_maximum(void) {
    pthread_mutex_lock(&g_state.mutex); int v = g_state.maximum; pthread_mutex_unlock(&g_state.mutex); return v;
}
extern "C" void mcpe_fps_reset_stats(void) {
    pthread_mutex_lock(&g_state.mutex);
    g_state.window_start_ns = 0; g_state.last_frame_ns = 0;
    g_state.total_frames = 0; g_state.window_frames = 0; g_state.completed_windows = 0;
    g_state.current = 0; g_state.average = 0; g_state.minimum = 0; g_state.maximum = 0;
    pthread_mutex_unlock(&g_state.mutex);
}
