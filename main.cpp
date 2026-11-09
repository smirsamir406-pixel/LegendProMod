// LegendPro client mod - STEP 1: HUD overlay test (MCPE 0.15.10, armeabi-v7a)
// Goal: prove we can (a) hook the game, (b) capture MinecraftClient*, (c) draw text.
#include <jni.h>
#include <dlfcn.h>
#include <string>
#include <stdio.h>
#include <android/log.h>

#define TAG "LegendProMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

extern "C" void MSHookFunction(void* symbol, void* replace, void** result);

struct Color { float r, g, b, a; };
struct Font;

typedef void  (*GameRenderer_render_t)(void* self, float partial);
typedef Font* (*GetFont_t)(void* client);
typedef void  (*DrawString_t)(void* screenRenderer, Font* font, const std::string& text,
                              int x, int y, const Color& color);

static void* g_mc = nullptr;                 // MinecraftClient*
static GameRenderer_render_t  orig_render  = nullptr;
static GetFont_t              orig_getFont = nullptr;
static DrawString_t           drawString   = nullptr;
static void**                 g_screenRendererPtr = nullptr;
static int g_frames = 0;

// Hooking a frequently-called getter is a simple way to grab the MinecraftClient* ("this").
static Font* hook_getFont(void* self) {
    g_mc = self;
    return orig_getFont(self);
}

static void hook_render(void* self, float partial) {
    orig_render(self, partial);
    if (!g_mc || !drawString || !g_screenRendererPtr || !*g_screenRendererPtr || !orig_getFont) return;
    Font* font = orig_getFont(g_mc);
    if (!font) return;
    g_frames++;
    char buf[64];
    snprintf(buf, sizeof(buf), "LegendPro HUD test  frame %d", g_frames);
    Color white = {1.f, 1.f, 1.f, 1.f};
    drawString(*g_screenRendererPtr, font, std::string(buf), 4, 4, white);
}

static void* sym(void* h, const char* name) {
    void* p = dlsym(h, name);
    LOGI("%s -> %p", name, p);
    return p;
}

__attribute__((constructor)) static void init() {}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    void* mc = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mc) { LOGI("dlopen libminecraftpe failed"); return JNI_VERSION_1_4; }

    void* render  = sym(mc, "_ZN12GameRenderer6renderEf");
    void* getFont = sym(mc, "_ZNK15MinecraftClient7getFontEv");
    drawString    = (DrawString_t)sym(mc, "_ZN14ScreenRenderer10drawStringEP4FontRKSsiiRK5Color");
    g_screenRendererPtr = (void**)sym(mc, "_ZN14ScreenRenderer12singletonPtrE");

    if (getFont) MSHookFunction(getFont, (void*)hook_getFont, (void**)&orig_getFont);
    if (render)  MSHookFunction(render,  (void*)hook_render,  (void**)&orig_render);
    LOGI("LegendProMod step1 loaded");
    return JNI_VERSION_1_4;
}
