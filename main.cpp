// libclient.so - client-side mod for Minecraft PE 0.15.10 (armeabi-v7a).
// Adds an on-screen button panel: VIEW, FPS, ARMOR, LIGHT, FOV.
//
// Threading: touch events arrive on another thread, so a tap only sets a bit
// in g_pending; the real work runs inside MinecraftClient::update (game thread).
#include <jni.h>
#include <time.h>
#include <stdio.h>
#include <android/log.h>
#include "dobby.h"
#include "game.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "MCClient", __VA_ARGS__)

static Game g;
static void* g_client = nullptr;
static volatile int g_frame = 0;
static volatile int g_lastHudFrame = -100;

enum { B_VIEW, B_FPS, B_ARMOR, B_LIGHT, B_FOV, B_COUNT };
static const char* LABELS[B_COUNT] = {"VIEW", "FPS", "ARMOR", "LIGHT", "FOV"};
static bool g_on[B_COUNT] = {false, true, true, false, false};
static unsigned g_pending = 0;

// layout in GUI units
static const int BX = 2, BY = 28, BW = 46, BH = 12, GAP = 2;

static float g_origGamma = -1.f, g_origFov = -1.f;
static int g_fovStep = 0;
static const float FOVS[3] = {70.f, 90.f, 110.f};

static int g_fps = 0;

static const Color C_ON   = {0.2f, 0.9f, 0.3f, 1.f};
static const Color C_OFF  = {1.f, 1.f, 1.f, 1.f};
static const Color C_BG   = {0.f, 0.f, 0.f, 0.55f};
static const Color C_BGON = {0.f, 0.3f, 0.1f, 0.65f};

static float invScale() {
    float s = g.invGuiScale ? *g.invGuiScale : 0.f;
    return s > 0.f ? s : 1.f;
}

// ---------- drawing ----------
static void text(void* sr, void* font, const char* s, int x, int y, const Color* c) {
    FakeStr fs(s);
    g.sr_drawString(sr, font, fs.ref_(), x, y, c);
}

static void drawOverlay(void* client) {
    void* sr = g.sr_singleton();
    void* font = g.client_getFont(client);
    if (!sr || !font) return;

    for (int i = 0; i < B_COUNT; i++) {
        int y = BY + i * (BH + GAP);
        g.sr_fill(sr, BX, y, BX + BW, y + BH, g_on[i] ? &C_BGON : &C_BG);
        text(sr, font, LABELS[i], BX + 4, y + 2, g_on[i] ? &C_ON : &C_OFF);
    }

    int tx = BX + BW + 6, ty = BY;
    char buf[48];
    if (g_on[B_FPS]) {
        snprintf(buf, sizeof buf, "FPS: %d", g_fps);
        text(sr, font, buf, tx, ty, &C_OFF);
        ty += 10;
    }
    if (g_on[B_ARMOR]) {
        void* player = g.client_getLocalPlayer(client);
        static const char* N[4] = {"H", "C", "L", "B"};   // head, chest, legs, boots
        for (int slot = 0; player && slot < 4; slot++) {
            void* it = g.mob_getArmor(player, slot);
            if (!it || g.item_isNull(it)) { snprintf(buf, sizeof buf, "%s: -", N[slot]); }
            else {
                int mx = g.item_getMaxDamage(it);
                int left = mx - g.item_getDamageValue(it);
                snprintf(buf, sizeof buf, "%s: %d/%d", N[slot], left, mx);
            }
            text(sr, font, buf, tx, ty, &C_OFF);
            ty += 10;
        }
    }
}

// ---------- actions (game thread) ----------
static void runAction(int i, void* client) {
    void* opts = g.client_getOptions(client);
    switch (i) {
    case B_VIEW:
        g.client_toggleThirdPerson(client);
        break;
    case B_FPS:
    case B_ARMOR:
        g_on[i] = !g_on[i];
        break;
    case B_LIGHT:
        if (g_origGamma < 0) g_origGamma = g.opt_getGamma(opts);
        g_on[i] = !g_on[i];
        g.opt_setGamma(opts, g_on[i] ? 15.f : g_origGamma);
        break;
    case B_FOV:
        if (g_origFov < 0) g_origFov = g.opt_getFov(opts);
        g_fovStep = (g_fovStep + 1) % 4;                 // 0 = restore original
        g_on[i] = g_fovStep != 0;
        g.opt_setFov(opts, g_fovStep ? FOVS[g_fovStep - 1] : g_origFov);
        break;
    }
}

// ---------- hooks ----------
static void (*orig_update)(void*) = nullptr;
static void my_update(void* client) {
    g_client = client;
    g_frame++;

    static long long t0 = 0; static int n = 0;
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    n++;
    if (now - t0 >= 1000) { g_fps = n; n = 0; t0 = now; }

    unsigned m = __atomic_exchange_n(&g_pending, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; m; i++, m >>= 1)
        if (m & 1) runAction(i, client);

    orig_update(client);
}

static void (*orig_screenRender)(void*, void*) = nullptr;
static void my_screenRender(void* self, void* ctx) {
    orig_screenRender(self, ctx);
    if (!g_client) return;
    g_lastHudFrame = g_frame;
    drawOverlay(g_client);
}

static void (*orig_feed)(char, char, short, short, int) = nullptr;
static bool g_cap[16];
static int g_capFrame[16];
static void my_feed(char b, char down, short x, short y, int id) {
    int s = id & 15;
    if (g_cap[s]) {                                        // swallow until release
        if (down == 0 || g_frame - g_capFrame[s] > 30) g_cap[s] = false;
        return;
    }
    if (g_frame - g_lastHudFrame <= 2 && b != 0 && down == 1) {
        float inv = invScale();
        float gx = x * inv, gy = y * inv;
        LOG("tap px=(%d,%d) gui=(%.1f,%.1f) inv=%.3f", x, y, gx, gy, inv);
        for (int i = 0; i < B_COUNT; i++) {
            int by = BY + i * (BH + GAP);
            if (gx >= BX && gx < BX + BW && gy >= by && gy < by + BH) {
                g_cap[s] = true; g_capFrame[s] = g_frame;
                __atomic_fetch_or(&g_pending, 1u << i, __ATOMIC_ACQ_REL);
                return;
            }
        }
    }
    orig_feed(b, down, x, y, id);
}

static bool hook(void* target, void* repl, void** orig) {
    return DobbyHook(target, (dobby_dummy_func_t)repl, (dobby_dummy_func_t*)orig) == 0;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    void* h = dlopen("libminecraftpe.so", RTLD_NOW);
    if (!h) { LOG("libminecraftpe.so not loaded"); return JNI_VERSION_1_6; }
    if (!g.load(h)) { LOG("symbol resolve failed"); return JNI_VERSION_1_6; }
    bool ok = hook(g.t_update, (void*)my_update, (void**)&orig_update)
           && hook(g.t_screenRender, (void*)my_screenRender, (void**)&orig_screenRender)
           && hook(g.t_multitouchFeed, (void*)my_feed, (void**)&orig_feed);
    LOG("hooks %s", ok ? "installed" : "FAILED");
    return JNI_VERSION_1_6;
}
