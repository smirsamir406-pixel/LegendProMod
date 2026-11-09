#include <jni.h>
#include <dlfcn.h>
#include <string>
#include <stdio.h>
#include <stdarg.h>
#include <android/log.h>

extern "C" void MSHookFunction(void* symbol, void* replace, void** result);

struct Color { float r, g, b, a; };
struct Font;
typedef void  (*Render_t)(void* self, float partial);
typedef void  (*TickInput_t)(void* self);
typedef Font* (*GetFont_t)(void* client);
typedef void  (*DrawString_t)(void* sr, Font* f, const std::string& t, int x, int y, const Color& c);

static const char* LOG_PATHS[] = {
    "/sdcard/games/com.mojang/minecraftpe/legendpro_log.txt",
    "/sdcard/legendpro_log.txt",
    "/sdcard/Android/data/com.mojang.LegendPro/files/legendpro_log.txt",
    "/data/data/com.mojang.LegendPro/legendpro_log.txt"
};
static const char* STAGE_PATHS[] = {
    "/sdcard/games/com.mojang/minecraftpe/legendpro_stage.txt",
    "/sdcard/legendpro_stage.txt",
    "/sdcard/Android/data/com.mojang.LegendPro/files/legendpro_stage.txt"
};

static void L(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "LegendProMod", "%s", buf);
    for (int i = 0; i < 4; i++) {
        FILE* f = fopen(LOG_PATHS[i], "a");
        if (f) { fprintf(f, "%s\n", buf); fclose(f); }
    }
}

static int g_stage = 0;
static void* g_mc = 0;
static Render_t orig_render = 0;
static TickInput_t orig_tick = 0;
static GetFont_t getFont = 0;
static DrawString_t drawString = 0;
static void** g_sr = 0;
static int g_frames = 0, g_tickLogged = 0, g_renderLogged = 0;

static void hook_tick(void* self) {
    g_mc = self;
    if (!g_tickLogged) { g_tickLogged = 1; L("tickInput first call, mc=%p", self); }
    orig_tick(self);
}

static void hook_render(void* self, float partial) {
    orig_render(self, partial);
    if (!g_renderLogged) { g_renderLogged = 1; L("render first call"); }
    if (g_stage < 3) return;
    if (!g_mc || !getFont || !drawString || !g_sr || !*g_sr) return;
    Font* font = getFont(g_mc);
    if (!font) return;
    if (g_frames == 0) L("about to draw, font=%p sr=%p", font, *g_sr);
    g_frames++;
    char buf[64];
    snprintf(buf, sizeof(buf), "LegendPro HUD test %d", g_frames);
    Color white = {1.f, 1.f, 1.f, 1.f};
    drawString(*g_sr, font, std::string(buf), 4, 4, white);
    if (g_frames == 1) L("first draw returned ok");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    L("JNI_OnLoad start");
    for (int i = 0; i < 3; i++) {
        FILE* sf = fopen(STAGE_PATHS[i], "r");
        if (sf) { int c = fgetc(sf); if (c >= '0' && c <= '3') g_stage = c - '0'; fclose(sf); break; }
    }
    L("stage=%d", g_stage);
    void* mc = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mc) { L("dlopen minecraftpe failed"); return JNI_VERSION_1_4; }
    void* render = dlsym(mc, "_ZN12GameRenderer6renderEf");
    void* tick   = dlsym(mc, "_ZN15MinecraftClient9tickInputEv");
    getFont      = (GetFont_t)dlsym(mc, "_ZNK15MinecraftClient7getFontEv");
    drawString   = (DrawString_t)dlsym(mc, "_ZN14ScreenRenderer10drawStringEP4FontRKSsiiRK5Color");
    g_sr         = (void**)dlsym(mc, "_ZN14ScreenRenderer12singletonPtrE");
    L("render=%p tick=%p getFont=%p draw=%p sr=%p", render, tick, getFont, drawString, g_sr);
    if (g_stage >= 1 && tick)   { MSHookFunction(tick, (void*)hook_tick, (void**)&orig_tick); L("hooked tick"); }
    if (g_stage >= 2 && render) { MSHookFunction(render, (void*)hook_render, (void**)&orig_render); L("hooked render"); }
    L("JNI_OnLoad done");
    return JNI_VERSION_1_4;
}
