#include <jni.h>
#include <dlfcn.h>
#include <string>
#include <stdio.h>
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "LegendProMod", __VA_ARGS__)

extern "C" void MSHookFunction(void* symbol, void* replace, void** result);

struct Color { float r, g, b, a; };
struct Font;

typedef void  (*Render_t)(void* self, float partial);
typedef void  (*TickInput_t)(void* self);
typedef Font* (*GetFont_t)(void* client);
typedef void  (*DrawString_t)(void* sr, Font* f, const std::string& t, int x, int y, const Color& c);

static void* g_mc = 0;
static Render_t orig_render = 0;
static TickInput_t orig_tick = 0;
static GetFont_t getFont = 0;
static DrawString_t drawString = 0;
static void** g_sr = 0;
static int g_frames = 0;

static void hook_tick(void* self) {
    g_mc = self;
    orig_tick(self);
}

static void hook_render(void* self, float partial) {
    orig_render(self, partial);
    if (!g_mc || !getFont || !drawString || !g_sr || !*g_sr) return;
    Font* font = getFont(g_mc);
    if (!font) return;
    g_frames++;
    char buf[64];
    snprintf(buf, sizeof(buf), "LegendPro HUD test %d", g_frames);
    Color white = {1.f, 1.f, 1.f, 1.f};
    drawString(*g_sr, font, std::string(buf), 4, 4, white);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    void* mc = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mc) return JNI_VERSION_1_4;
    void* render = dlsym(mc, "_ZN12GameRenderer6renderEf");
    void* tick   = dlsym(mc, "_ZN15MinecraftClient9tickInputEv");
    getFont      = (GetFont_t)dlsym(mc, "_ZNK15MinecraftClient7getFontEv");
    drawString   = (DrawString_t)dlsym(mc, "_ZN14ScreenRenderer10drawStringEP4FontRKSsiiRK5Color");
    g_sr         = (void**)dlsym(mc, "_ZN14ScreenRenderer12singletonPtrE");
    LOGI("render=%p tick=%p getFont=%p draw=%p sr=%p", render, tick, getFont, drawString, g_sr);
    if (tick)   MSHookFunction(tick,   (void*)hook_tick,   (void**)&orig_tick);
    if (render) MSHookFunction(render, (void*)hook_render, (void**)&orig_render);
    return JNI_VERSION_1_4;
}
