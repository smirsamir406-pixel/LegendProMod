#include <android/log.h>
#include <dlfcn.h>
#include "mcpe_fps.h"

#define TAG "MCPE01510FPS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Loader-facing entry point. It verifies that the target game library is
// already loaded; it intentionally does not guess private renderer symbols.
extern "C" __attribute__((visibility("default")))
int mcpe_fps_extension_init(void) {
    void* h = dlopen("libminecraftpe.so", RTLD_NOW);
    if (!h) {
        const char* err = dlerror();
        LOGE("Could not open libminecraftpe.so: %s", err ? err : "unknown error");
        return -1;
    }
    LOGI("Found libminecraftpe.so; FPS module initialized. Connect mcpe_fps_on_frame() to the present-frame callback.");
    // Do not dlclose: retain the handle while the extension is active.
    return 0;
}

extern "C" __attribute__((visibility("default")))
int mcpe_fps_extension_get_fps(void) { return mcpe_fps_get_current(); }
