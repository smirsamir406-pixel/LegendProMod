// libclient.so v7 - client-side mod for Minecraft PE 0.15.10 (armeabi-v7a).
// Created by LegendPro.
// A "LegendPro" button on the HUD opens a panel of black toggle buttons.
// Hooks are installed with Substrate (libsubstrate.so loaded at runtime).
#include <jni.h>
#include <dlfcn.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "MCClient", __VA_ARGS__)

// ============================ types ============================
struct Color { float r, g, b, a; };
struct Vec3 { float x, y, z; };

// MCPE 0.15.10 uses the old libstdc++ COW std::string: the object is one
// pointer to the chars, with {length, capacity, refcount} right before them.
// The game keeps (a COW copy of) the string and draws it later, so every
// string must stay valid and UNCHANGED while the game may use it.
// Each on-screen text has its own slot; a slot gets a brand-new permanent
// string whenever its text changes (old ones are never modified or freed).
static const void* makeStr(const char* s) {
    size_t n = strlen(s);
    size_t dataLen = (n + 1 + 3) & ~(size_t)3;
    char* m = (char*)malloc(12 + dataLen + sizeof(char*));
    if (!m) return nullptr;
    ((size_t*)m)[0] = n;                 // length
    ((size_t*)m)[1] = n;                 // capacity
    ((int*)m)[2] = 0x3fffffff;           // refcount: never reaches zero
    char* data = m + 12;
    memcpy(data, s, n);
    data[n] = 0;
    char** holder = (char**)(m + 12 + dataLen);
    *holder = data;
    return holder;                       // acts as `const std::string&`
}

enum { SL_MOD = 0, SL_BTN = 1 /* + 23 buttons */, SL_FOOT2 = 24, SL_FOOT = 25,
       SL_FPS = 26, SL_POS = 27, SL_CAM = 28, SL_ARMOR = 29 /* 4 */,
       SL_BANNER = 33 /* 6 */, SL_MAPN = 39, SL_COUNT = 48 };
struct TextSlot { char text[64]; const void* ref; long long changedMs; };
static TextSlot g_slots[SL_COUNT];

// ---- resolved game functions (required ones abort loading, optional ones don't)
static void* (*client_getOptions)(void*);
static void* (*client_getLocalPlayer)(void*);
static void* (*client_getFont)(void*);
static void  (*client_toggleThirdPerson)(void*);
static void  (*opt_setGamma)(void*, float);
static float (*opt_getGamma)(void*);
static void  (*opt_setFov)(void*, float);
static float (*opt_getFov)(void*);
static void  (*opt_setLeftHanded)(void*, bool);
static void  (*opt_setBob)(void*, bool);
static bool  (*opt_getBob)(void*);
static void  (*opt_setSmooth)(void*, bool);
static void  (*opt_setHideGui)(void*, bool);
static bool  (*opt_getHideGui)(void*);
static void  (*opt_setParticles)(void*, float);
static float (*opt_getParticles)(void*);
static void  (*opt_setPersp)(void*, int);
static int   (*opt_getPersp)(void*);
static void  (*player_setSprinting)(void*, bool);
static void  (*mob_dropHeldItem)(void*);
static void* (*mob_getArmor)(void*, int);
static bool  (*item_isNull)(void*);
static int   (*item_getMaxDamage)(void*);
static int   (*item_getDamageValue)(void*);
static const float* (*entity_getPos)(void*);      // returns a pointer to x,y,z
static void* (*entity_getRegion)(void*);           // BlockSource of the entity
static int   (*region_getHeightmap)(void*, int, int);
static int   (*region_getBlockID)(void*, int, int, int);
static void* (*sr_singleton)();
static void  (*sr_fill)(void*, int, int, int, int, const Color*);
static void  (*sr_drawString)(void*, void*, const void*, int, int, const Color*);
static float* invGuiScale;

static bool loadSymbols(void* h) {
    #define REQ(var, name) var = (decltype(var))dlsym(h, name); \
        if (!(var)) { LOG("missing REQUIRED symbol: %s", name); return false; }
    #define OPT(var, name) var = (decltype(var))dlsym(h, name); \
        if (!(var)) LOG("missing optional symbol: %s", name);
    REQ(client_getOptions,        "_ZN15MinecraftClient10getOptionsEv")
    REQ(client_getFont,           "_ZNK15MinecraftClient7getFontEv")
    REQ(sr_singleton,             "_ZN14ScreenRenderer9singletonEv")
    REQ(sr_fill,                  "_ZNK14ScreenRenderer4fillEiiiiRK5Color")
    REQ(sr_drawString,            "_ZN14ScreenRenderer10drawStringEP4FontRKSsiiRK5Color")
    OPT(client_getLocalPlayer,    "_ZN15MinecraftClient14getLocalPlayerEv")
    OPT(client_toggleThirdPerson, "_ZN15MinecraftClient22_toggleThirdPersonViewEv")
    OPT(opt_setGamma,             "_ZN7Options8setGammaEf")
    OPT(opt_getGamma,             "_ZNK7Options8getGammaEv")
    OPT(opt_setFov,               "_ZN7Options14setFieldOfViewEf")
    OPT(opt_getFov,               "_ZNK7Options14getFieldOfViewEv")
    OPT(opt_setLeftHanded,        "_ZN7Options15setIsLeftHandedEb")
    OPT(opt_setBob,               "_ZN7Options10setBobViewEb")
    OPT(opt_getBob,               "_ZNK7Options10getBobViewEv")
    OPT(opt_setSmooth,            "_ZN7Options15setSmoothCameraEb")
    OPT(opt_setHideGui,           "_ZN7Options10setHideGuiEb")
    OPT(opt_getHideGui,           "_ZNK7Options10getHideGuiEv")
    OPT(opt_setParticles,         "_ZN7Options23setParticleViewDistanceEf")
    OPT(opt_getParticles,         "_ZNK7Options23getParticleViewDistanceEv")
    OPT(opt_setPersp,             "_ZN7Options24setPlayerViewPerspectiveEi")
    OPT(opt_getPersp,             "_ZNK7Options24getPlayerViewPerspectiveEv")
    OPT(player_setSprinting,      "_ZN11LocalPlayer12setSprintingEb")
    OPT(mob_dropHeldItem,         "_ZN3Mob12dropHeldItemEv")
    OPT(mob_getArmor,             "_ZNK3Mob8getArmorE9ArmorSlot")
    OPT(item_isNull,              "_ZNK12ItemInstance6isNullEv")
    OPT(item_getMaxDamage,        "_ZNK12ItemInstance12getMaxDamageEv")
    OPT(item_getDamageValue,      "_ZNK12ItemInstance14getDamageValueEv")
    OPT(entity_getPos,            "_ZNK6Entity6getPosEv")
    OPT(entity_getRegion,         "_ZNK6Entity9getRegionEv")
    OPT(region_getHeightmap,      "_ZN11BlockSource12getHeightmapEii")
    OPT(region_getBlockID,        "_ZN11BlockSource10getBlockIDEiii")
    OPT(invGuiScale,              "_ZN7GuiData11InvGuiScaleE")
    #undef REQ
    #undef OPT
    return true;
}

// ============================ state ============================
static void* g_client = nullptr;
static void* g_player = nullptr;
static volatile int g_frame = 0;
static volatile int g_lastHudFrame = -100;

enum { B_VIEW, B_FPS, B_ARMOR, B_LIGHT, B_FOV, B_LEFT, B_NOHAND,
       B_NOCLOUD, B_NOSTAR, B_CLEAR, B_OUTLINE, B_AUTOSPR, B_NOBOB, B_SMOOTH,
       B_Q, B_HUD, B_FREECAM, B_NOPART, B_POS, B_UP, B_DOWN, B_MAP, B_MAPZ, B_COUNT };
static const int B_MOD = B_COUNT;                       // the master button
static const char* LABELS[B_COUNT] = {
    "F5 VIEW", "FPS", "ARMOR", "LIGHT", "FOV", "LEFT", "NOHAND",
    "NOCLOUD", "NOSTAR", "CLEAR", "OUTLINE", "SPRINT", "NOBOB", "SMOOTH",
    "Q DROP", "F1 HUD", "FREECAM", "NOPART", "POS", "FC UP", "FC DOWN", "MAP", "MAPZOOM"};
static volatile bool g_on[B_COUNT] = {
    false, true, false, false, false, false, false,
    false, false, false, true, false, false, false,
    false, false, false, false, true, false, false, true, false};
static volatile bool g_hudHidden = false;
static volatile bool g_open = false;
static unsigned g_pending = 0;

// layout, GUI units. Minimap top-left; the "LegendPro" button sits under it
// (or at the very top when the map is off); the panel opens to the right.
static const int MAP_X = 2, MAP_Y = 2, MAP_CELLS = 26, MAP_CELL = 2, MAP_SIZE = 52;
static const int MOD_X = 2, MOD_W = 60, MOD_H = 12;
static const int P_X = 66, P_Y = 2, BW = 50, BH = 12, GAP = 2, ROWS = 8;
static int modY() { return g_on[B_MAP] ? MAP_Y + MAP_SIZE + 6 : 2; }

static bool btnRect(int i, int* x, int* y, int* w, int* h) {
    if (i == B_MOD) { *x = MOD_X; *y = modY(); *w = MOD_W; *h = MOD_H; return true; }
    if (i < 0 || i >= B_COUNT) return false;
    int col = i / ROWS, row = i % ROWS;
    *x = P_X + col * (BW + GAP);
    *y = P_Y + row * (BH + GAP);
    *w = BW; *h = BH;
    return true;
}

static float g_origGamma = -1.f, g_origFov = -1.f, g_origPart = -1.f;
static int g_origBob = -1;
static int g_fovStep = 0;
static const float FOVS[3] = {70.f, 90.f, 110.f};
static int g_fps = 0;

// ---- freecam state
static volatile bool g_freecam = false;
static bool g_freecamReady = false;          // both freecam hooks installed
static Vec3 g_camOff = {0.f, 0.f, 0.f};      // camera offset from the player
static int g_vert = 0;                       // -1 down, 0 none, +1 up
static int g_origPersp = -1;
static const float FC_MOVE_MUL = 3.0f;       // horizontal speed multiplier
static const float FC_VERT_SPEED = 8.0f;     // blocks per second
static uintptr_t g_rngMove = 0, g_rngComp = 0, g_rngFrom = 0;

static const Color C_ON   = {0.2f, 0.9f, 0.3f, 1.f};
static const Color C_OFF  = {1.f, 1.f, 1.f, 1.f};
static const Color C_BG   = {0.f, 0.f, 0.f, 0.85f};     // black buttons
static const Color C_CYAN = {0.3f, 0.9f, 1.0f, 1.f};
static const Color C_YELLOW = {1.0f, 0.9f, 0.2f, 1.f};
static const Color C_RED  = {1.0f, 0.3f, 0.3f, 1.f};

static long long nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static float invScale() {
    float s = invGuiScale ? *invGuiScale : 0.f;
    return s > 0.f ? s : 1.f;
}

// ============================ drawing ============================
// minIntervalMs > 0: a changed text is only swapped in at that rate (keeps the
// number of permanent strings small for fast-changing live data).
static void text(int slot, void* sr, void* font, const char* s, int x, int y,
                 const Color* c, int minIntervalMs = 0) {
    TextSlot& t = g_slots[slot];
    long long now = nowMs();
    bool changed = !t.ref || strcmp(t.text, s) != 0;
    if (changed && (!t.ref || minIntervalMs <= 0 || now - t.changedMs >= minIntervalMs)) {
        const void* r = makeStr(s);
        if (!r) return;
        t.ref = r;
        strncpy(t.text, s, sizeof t.text - 1);
        t.text[sizeof t.text - 1] = 0;
        t.changedMs = now;
    }
    sr_drawString(sr, font, t.ref, x, y, c);
}

// ---------------- minimap ----------------
// Top-down view of the surface around the player, north (-Z) up, east (+X) right.
// Colours come from the legacy block id; the grid is rebuilt a few times per
// second and drawn every frame (neighbouring equal cells are merged into one fill).
static const int ZOOMS[3] = {1, 2, 4};              // blocks per map cell
static int g_zoomStep = 1;
static unsigned short g_mapKey[MAP_CELLS * MAP_CELLS];   // id*3+shade, 0xFFFF = unknown
static long long g_mapMs = 0;
static int g_mapZoomDrawn = -1;
static Color g_blockCol[256];
static bool g_blockTransparent[256];
static bool g_blockInit = false;

static void initBlockColors() {
    for (int i = 0; i < 256; i++) g_blockCol[i] = {0.45f, 0.45f, 0.45f, 1.f};
    struct E { int id; float r, g, b; };
    static const E T[] = {
        {1, .50f, .50f, .50f}, {2, .36f, .65f, .25f}, {3, .50f, .35f, .20f}, {4, .45f, .45f, .45f},
        {5, .65f, .52f, .30f}, {7, .18f, .18f, .18f}, {8, .20f, .35f, .85f}, {9, .20f, .35f, .85f},
        {10, .95f, .45f, .10f}, {11, .95f, .45f, .10f}, {12, .87f, .82f, .55f}, {13, .55f, .50f, .48f},
        {14, .70f, .65f, .40f}, {15, .60f, .52f, .45f}, {16, .35f, .35f, .35f}, {17, .40f, .30f, .15f},
        {18, .20f, .50f, .15f}, {24, .80f, .75f, .50f}, {45, .60f, .30f, .25f}, {48, .30f, .45f, .30f},
        {49, .10f, .05f, .15f}, {60, .45f, .30f, .15f}, {78, .95f, .95f, 1.f}, {79, .60f, .80f, .95f},
        {80, .95f, .95f, 1.f}, {81, .10f, .50f, .15f}, {82, .60f, .62f, .70f}, {86, .85f, .50f, .10f},
        {87, .45f, .15f, .15f}, {88, .35f, .25f, .20f}, {89, .95f, .85f, .40f}, {98, .48f, .48f, .48f},
        {110, .45f, .38f, .45f}, {121, .85f, .85f, .60f}, {161, .18f, .48f, .15f}, {162, .38f, .30f, .20f},
        {172, .60f, .35f, .25f}, {174, .70f, .85f, .95f}, {155, .90f, .90f, .88f}, {43, .55f, .55f, .55f},
    };
    for (const E& e : T) g_blockCol[e.id] = {e.r, e.g, e.b, 1.f};
    // plants, torches, rails, doors ...: skipped so the ground below shows instead
    static const int P[] = {6, 27, 28, 30, 31, 32, 37, 38, 39, 40, 50, 51, 55, 59, 63, 64, 65, 66, 68, 69,
                            70, 71, 72, 75, 76, 77, 83, 106, 111, 141, 142, 157, 175, 176, 177};
    for (int id : P) g_blockTransparent[id] = true;
    g_blockInit = true;
}

static void updateMap(void* player) {
    if (!entity_getPos || !entity_getRegion || !region_getHeightmap || !region_getBlockID) return;
    const float* pos = entity_getPos(player);
    void* region = entity_getRegion(player);
    if (!pos || !region) return;
    if (!g_blockInit) initBlockColors();

    int zoom = ZOOMS[g_zoomStep];
    int px = (int)__builtin_floorf(pos[0]), pz = (int)__builtin_floorf(pos[2]);
    static int heights[MAP_CELLS * MAP_CELLS];
    int half = MAP_CELLS / 2;
    for (int j = 0; j < MAP_CELLS; j++) {
        for (int i = 0; i < MAP_CELLS; i++) {
            int wx = px + (i - half) * zoom, wz = pz + (j - half) * zoom;
            int h = region_getHeightmap(region, wx, wz);
            int idx = j * MAP_CELLS + i;
            if (h <= 0 || h > 200) { g_mapKey[idx] = 0xFFFF; heights[idx] = 0; continue; }
            int y = h > 127 ? 127 : h;
            int id = 0;
            for (int k = 0; k < 14 && y > 0; k++, y--) {
                id = region_getBlockID(region, wx, y, wz) & 0xFF;
                if (id != 0 && !g_blockTransparent[id]) break;
                id = 0;
            }
            if (id == 0) { g_mapKey[idx] = 0xFFFF; heights[idx] = 0; continue; }
            int shade = 1;                                // 0 darker, 1 normal, 2 lighter
            if (i > 0 && j > 0) {
                int hn = heights[(j - 1) * MAP_CELLS + (i - 1)];
                if (hn > 0) shade = y > hn ? 2 : (y < hn ? 0 : 1);
            }
            g_mapKey[idx] = (unsigned short)(id * 3 + shade);
            heights[idx] = y;
        }
    }
    g_mapZoomDrawn = g_zoomStep;
}

static void drawMap(void* sr, void* font, void* client) {
    long long now = nowMs();
    void* player = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
    if (player && (now - g_mapMs > 450 || g_mapZoomDrawn != g_zoomStep)) {
        updateMap(player);
        g_mapMs = now;
    }
    static const Color FRAME = {0.f, 0.f, 0.f, 0.9f};
    static const Color UNKNOWN = {0.10f, 0.10f, 0.12f, 1.f};
    static const Color ME = {1.f, 0.15f, 0.15f, 1.f};
    sr_fill(sr, MAP_X - 2, MAP_Y - 2, MAP_X + MAP_SIZE + 2, MAP_Y + MAP_SIZE + 2, &FRAME);
    for (int j = 0; j < MAP_CELLS; j++) {
        int y0 = MAP_Y + j * MAP_CELL;
        int i = 0;
        while (i < MAP_CELLS) {
            unsigned short key = g_mapKey[j * MAP_CELLS + i];
            int i1 = i + 1;
            while (i1 < MAP_CELLS && g_mapKey[j * MAP_CELLS + i1] == key) i1++;
            Color c = UNKNOWN;
            if (key != 0xFFFF) {
                c = g_blockCol[key / 3];
                int shade = key % 3;
                float m = shade == 2 ? 1.15f : (shade == 0 ? 0.80f : 1.0f);
                c.r = c.r * m > 1.f ? 1.f : c.r * m;
                c.g = c.g * m > 1.f ? 1.f : c.g * m;
                c.b = c.b * m > 1.f ? 1.f : c.b * m;
            }
            sr_fill(sr, MAP_X + i * MAP_CELL, y0, MAP_X + i1 * MAP_CELL, y0 + MAP_CELL, &c);
            i = i1;
        }
    }
    int cx = MAP_X + (MAP_CELLS / 2) * MAP_CELL;           // player marker
    sr_fill(sr, cx - 1, MAP_Y + (MAP_CELLS / 2) * MAP_CELL - 1,
                cx + 3, MAP_Y + (MAP_CELLS / 2) * MAP_CELL + 3, &ME);
    text(SL_MAPN, sr, font, "N", MAP_X + MAP_SIZE / 2 - 2, MAP_Y + 1, &C_YELLOW);
}

static void drawOverlay(void* client) {
    void* sr = sr_singleton();
    void* font = client_getFont(client);
    if (!sr || !font) return;

    if (g_on[B_MAP]) drawMap(sr, font, client);

    int x, y, w, h;
    btnRect(B_MOD, &x, &y, &w, &h);
    sr_fill(sr, x, y, x + w, y + h, &C_BG);
    text(SL_MOD, sr, font, "LegendPro", x + 4, y + 2, &C_CYAN);

    if (g_open) {
        for (int i = 0; i < B_COUNT; i++) {
            btnRect(i, &x, &y, &w, &h);
            sr_fill(sr, x, y, x + w, y + h, &C_BG);
            const Color* tc = g_on[i] ? &C_ON : &C_OFF;
            if (i == B_MAPZ) tc = g_zoomStep != 1 ? &C_ON : &C_OFF;
            text(SL_BTN + i, sr, font, LABELS[i], x + 4, y + 2, tc);
        }
        int fy = P_Y + ROWS * (BH + GAP) + 2;
        text(SL_FOOT,  sr, font, "Created by LegendPro",     P_X, fy,      &C_ON);
        text(SL_FOOT2, sr, font, "YouTuber : LegendPro9956", P_X, fy + 10, &C_RED);
    }

    // info text: right of the map (panel closed) or right of the panel (open)
    int tx = g_open ? P_X + 3 * (BW + GAP) + 4 : (g_on[B_MAP] ? MAP_X + MAP_SIZE + 6 : MOD_X + MOD_W + 6);
    int ty = g_open ? 22 : 4;

    // welcome banner for the first 12 seconds after the HUD first appears
    static long long hudStart = 0;
    long long now = nowMs();
    if (!hudStart) hudStart = now;
    if (now - hudStart < 12000) {
        text(SL_BANNER + 0, sr, font, "MENU",                      tx, ty, &C_OFF);    ty += 10;
        text(SL_BANNER + 1, sr, font, "Welcome to LegendPro3.4",   tx, ty, &C_CYAN);   ty += 10;
        text(SL_BANNER + 2, sr, font, "Made by LegendPro",         tx, ty, &C_YELLOW); ty += 10;
        text(SL_BANNER + 3, sr, font, "YouTuber : LegendPro9956",  tx, ty, &C_RED);    ty += 12;
    }

    // live data, read fresh every frame
    char buf[64];
    if (g_on[B_FPS]) {
        snprintf(buf, sizeof buf, "FPS: %d", g_fps);
        text(SL_FPS, sr, font, buf, tx, ty, &C_OFF, 500);
        ty += 10;
    }
    if (g_on[B_POS] && entity_getPos && client_getLocalPlayer) {
        void* p = client_getLocalPlayer(client);
        const float* pos = p ? entity_getPos(p) : nullptr;
        if (pos) {
            snprintf(buf, sizeof buf, "XYZ: %d %d %d", (int)__builtin_floorf(pos[0]),
                     (int)__builtin_floorf(pos[1]), (int)__builtin_floorf(pos[2]));
            text(SL_POS, sr, font, buf, tx, ty, &C_YELLOW, 200);
            ty += 10;
            if (g_freecam) {
                snprintf(buf, sizeof buf, "CAM: %d %d %d",
                         (int)__builtin_floorf(pos[0] + g_camOff.x),
                         (int)__builtin_floorf(pos[1] + g_camOff.y),
                         (int)__builtin_floorf(pos[2] + g_camOff.z));
                text(SL_CAM, sr, font, buf, tx, ty, &C_CYAN, 200);
                ty += 10;
            }
        }
    }
    if (g_on[B_ARMOR] && client_getLocalPlayer && mob_getArmor && item_isNull &&
        item_getMaxDamage && item_getDamageValue) {
        void* player = client_getLocalPlayer(client);
        static const char* N[4] = {"H", "C", "L", "B"};   // head, chest, legs, boots
        for (int slot = 0; player && slot < 4; slot++) {
            void* it = mob_getArmor(player, slot);
            if (!it || item_isNull(it)) {
                snprintf(buf, sizeof buf, "%s: -", N[slot]);
            } else {
                int mx = item_getMaxDamage(it);
                int left = mx - item_getDamageValue(it);
                snprintf(buf, sizeof buf, "%s: %d/%d", N[slot], left, mx);
            }
            text(SL_ARMOR + slot, sr, font, buf, tx, ty, &C_OFF, 500);
            ty += 10;
        }
    }
}

// ============================ actions (game thread) ============================
static void setFreecam(bool on, void* client, void* opts) {
    if (on) {
        if (!g_freecamReady || !client_getLocalPlayer) return;
        g_player = client_getLocalPlayer(client);
        if (!g_player) return;
        g_camOff = {0.f, 0.f, 0.f};
        g_vert = 0;
        if (opt_getPersp && opt_setPersp) {              // third person so the player body is visible
            g_origPersp = opt_getPersp(opts);
            opt_setPersp(opts, 1);
        }
        g_freecam = true;
    } else {
        g_freecam = false;
        g_camOff = {0.f, 0.f, 0.f};
        g_vert = 0;
        if (g_origPersp >= 0 && opt_setPersp) opt_setPersp(opts, g_origPersp);
        g_origPersp = -1;
    }
    g_on[B_FREECAM] = g_freecam;
    g_on[B_UP] = false;
    g_on[B_DOWN] = false;
}

static void runAction(int i, void* client) {
    if (i == B_MOD) { g_open = !g_open; return; }
    void* opts = client_getOptions(client);
    switch (i) {
    case B_VIEW:
        if (client_toggleThirdPerson) client_toggleThirdPerson(client);
        break;
    case B_LIGHT:
        if (!opt_setGamma || !opt_getGamma) break;
        if (g_origGamma < 0) g_origGamma = opt_getGamma(opts);
        g_on[i] = !g_on[i];
        opt_setGamma(opts, g_on[i] ? 15.f : g_origGamma);
        break;
    case B_FOV:
        if (!opt_setFov || !opt_getFov) break;
        if (g_origFov < 0) g_origFov = opt_getFov(opts);
        g_fovStep = (g_fovStep + 1) % 4;                 // 0 = restore original
        g_on[i] = g_fovStep != 0;
        opt_setFov(opts, g_fovStep ? FOVS[g_fovStep - 1] : g_origFov);
        break;
    case B_LEFT:
        if (!opt_setLeftHanded) break;
        g_on[i] = !g_on[i];
        opt_setLeftHanded(opts, g_on[i]);
        break;
    case B_NOBOB:
        if (!opt_setBob || !opt_getBob) break;
        if (g_origBob < 0) g_origBob = opt_getBob(opts) ? 1 : 0;
        g_on[i] = !g_on[i];
        opt_setBob(opts, g_on[i] ? false : g_origBob != 0);
        break;
    case B_SMOOTH:
        if (!opt_setSmooth) break;
        g_on[i] = !g_on[i];
        opt_setSmooth(opts, g_on[i]);
        break;
    case B_Q:                                             // drop the held item
        if (client_getLocalPlayer && mob_dropHeldItem) {
            void* p = client_getLocalPlayer(client);
            if (p) mob_dropHeldItem(p);
        }
        break;
    case B_HUD:                                           // F1: hide / show the game HUD
        if (opt_setHideGui && opt_getHideGui) opt_setHideGui(opts, !opt_getHideGui(opts));
        break;
    case B_FREECAM:
        setFreecam(!g_freecam, client, opts);
        break;
    case B_UP:
        if (!g_freecam) break;
        g_vert = (g_vert == 1) ? 0 : 1;
        g_on[B_UP] = g_vert == 1;
        g_on[B_DOWN] = false;
        break;
    case B_DOWN:
        if (!g_freecam) break;
        g_vert = (g_vert == -1) ? 0 : -1;
        g_on[B_DOWN] = g_vert == -1;
        g_on[B_UP] = false;
        break;
    case B_MAPZ:                                          // map zoom: 1 / 2 / 4 blocks per cell
        g_zoomStep = (g_zoomStep + 1) % 3;
        break;
    case B_NOPART:
        if (!opt_setParticles || !opt_getParticles) break;
        if (g_origPart < 0) g_origPart = opt_getParticles(opts);
        g_on[i] = !g_on[i];
        opt_setParticles(opts, g_on[i] ? 0.f : g_origPart);
        break;
    default:                                              // plain flags read by hooks / overlay
        g_on[i] = !g_on[i];
        break;
    }
}

// ============================ hooks ============================
static void (*orig_update)(void*) = nullptr;
static void my_update(void* client) {
    g_client = client;
    g_frame++;

    static long long t0 = 0, tPrev = 0;
    static int n = 0;
    long long now = nowMs();
    n++;
    if (t0 == 0) t0 = now;
    if (now - t0 >= 500) { g_fps = (int)(n * 1000LL / (now - t0)); n = 0; t0 = now; }
    float dt = tPrev ? (now - tPrev) / 1000.f : 0.f;
    tPrev = now;
    if (dt > 0.2f) dt = 0.2f;

    unsigned m = __atomic_exchange_n(&g_pending, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; m; i++, m >>= 1)
        if (m & 1) runAction(i, client);

    if (opt_getHideGui) {
        void* o = client_getOptions(client);
        if (o) { bool h = opt_getHideGui(o); g_hudHidden = h; g_on[B_HUD] = h; }
    }

    if (g_freecam) {
        void* p = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
        if (!p) setFreecam(false, client, client_getOptions(client));   // left the world
        else {
            g_player = p;
            g_camOff.y += g_vert * FC_VERT_SPEED * dt;
        }
    }

    if (g_on[B_AUTOSPR] && client_getLocalPlayer && player_setSprinting) {
        void* p = client_getLocalPlayer(client);
        if (p) player_setSprinting(p, true);
    }

    orig_update(client);
}

// The hotbar is drawn by the HUD pass every frame, so drawing from here
// uses the right 2D state (the same one hearts/armor use).
static void (*orig_hotbar)(void*, void*, void*, int, void*) = nullptr;
static void my_hotbar(void* self, void* client, void* ctrl, int a, void* area) {
    orig_hotbar(self, client, ctrl, a, area);
    if (!client) return;
    g_client = client;
    g_lastHudFrame = g_frame;
    drawOverlay(client);
}

static void (*orig_feed)(char, char, short, short, int) = nullptr;
static bool g_cap[16];
static int g_capFrame[16];
static void my_feed(char b, char down, short x, short y, int id) {
    int s = id & 15;
    if (g_cap[s]) {                                   // swallow until the finger lifts
        if (down == 0 || g_frame - g_capFrame[s] > 30) g_cap[s] = false;
        return;
    }
    if (g_hudHidden && g_frame - g_lastHudFrame > 2 && b != 0 && down == 1) {
        float inv = invScale();
        float gx = x * inv, gy = y * inv;
        if (gx < 68 && gy < 80) {                                  // bring the HUD back
            g_cap[s] = true;
            g_capFrame[s] = g_frame;
            __atomic_fetch_or(&g_pending, 1u << B_HUD, __ATOMIC_ACQ_REL);
            return;
        }
    }
    if (g_frame - g_lastHudFrame <= 2 && b != 0 && down == 1) {
        float inv = invScale();
        float gx = x * inv, gy = y * inv;
        LOG("tap px=(%d,%d) gui=(%.1f,%.1f) inv=%.3f", x, y, gx, gy, inv);
        for (int i = 0; i <= B_COUNT; i++) {
            if (i != B_MOD && !g_open) continue;
            int bx, by, bw, bh;
            if (!btnRect(i, &bx, &by, &bw, &bh)) continue;
            if (gx >= bx && gx < bx + bw && gy >= by && gy < by + bh) {
                g_cap[s] = true;
                g_capFrame[s] = g_frame;
                __atomic_fetch_or(&g_pending, 1u << i, __ATOMIC_ACQ_REL);
                return;
            }
        }
    }
    orig_feed(b, down, x, y, id);
}

// ---- freecam: shift only the CAMERA's view of the player position ----
// Entity::getInterpolatedPosition is used both to place the camera and to draw
// entities. We only add the offset when the caller is one of the camera
// functions, so the player's body stays where it really is (it shows up like
// an NPC) while the camera flies around, also through blocks.
static bool inRange(uintptr_t a, uintptr_t start, uintptr_t size) {
    return start && a >= start && a < start + size;
}

static void (*orig_interp)(Vec3*, void*, float) = nullptr;
__attribute__((noinline)) static void my_interp(Vec3* ret, void* self, float a) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    orig_interp(ret, self, a);
    if (g_freecam && self && self == g_player) {
        if (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngComp, 480) || inRange(lr, g_rngFrom, 164)) {
            ret->x += g_camOff.x;
            ret->y += g_camOff.y;
            ret->z += g_camOff.z;
        }
    }
}

// While freecam is on the player does not walk: the movement the player would
// have made is applied to the camera instead (horizontal only; up/down use the
// FC UP / FC DOWN buttons).
static void (*orig_lpmove)(void*, const float*) = nullptr;
static void my_lpmove(void* self, const float* d) {
    if (g_freecam && self && self == g_player) {
        g_camOff.x += d[0] * FC_MOVE_MUL;
        g_camOff.z += d[2] * FC_MOVE_MUL;
        return;
    }
    orig_lpmove(self, d);
}

// ---- feature hooks: skip the original function when the toggle is on ----
static void (*orig_hand)(void*, bool, float) = nullptr;
static void my_hand(void* self, bool a, float t) {
    if (g_on[B_NOHAND]) return;
    orig_hand(self, a, t);
}

static void (*orig_clouds)(void*, float) = nullptr;
static void my_clouds(void* self, float t) {
    if (g_on[B_NOCLOUD]) return;
    orig_clouds(self, t);
}

static void (*orig_stars)(void*, float) = nullptr;
static void my_stars(void* self, float t) {
    if (g_on[B_NOSTAR]) return;
    orig_stars(self, t);
}

static void (*orig_rain)(void*, float) = nullptr;
static void my_rain(void* self, float level) {
    orig_rain(self, g_on[B_CLEAR] ? 0.f : level);
}

static void (*orig_outline)(void*, void*, const void*, float, bool) = nullptr;
static void my_outline(void* self, void* bs, const void* pos, float t, bool b) {
    if (!g_on[B_OUTLINE]) return;
    orig_outline(self, bs, pos, t, b);
}

// ============================ entry ============================
typedef void (*MSHookFunction_t)(void* symbol, void* replace, void** result);
static MSHookFunction_t g_hook = nullptr;

static bool hookSym(void* game, const char* name, void* repl, void** orig, bool required) {
    void* target = dlsym(game, name);
    if (!target) { LOG("%s symbol not found: %s", required ? "REQUIRED" : "optional", name); return !required; }
    g_hook(target, repl, orig);
    return true;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    void* game = dlopen("libminecraftpe.so", RTLD_NOW);
    if (!game) { LOG("libminecraftpe.so not loaded"); return JNI_VERSION_1_6; }
    if (!loadSymbols(game)) { LOG("symbol resolve failed"); return JNI_VERSION_1_6; }

    void* sub = dlopen("libsubstrate.so", RTLD_NOW);
    g_hook = sub ? (MSHookFunction_t)dlsym(sub, "MSHookFunction") : nullptr;
    if (!g_hook) { LOG("libsubstrate.so / MSHookFunction not found"); return JNI_VERSION_1_6; }

    bool ok = true;
    ok &= hookSym(game, "_ZN15MinecraftClient6updateEv", (void*)my_update, (void**)&orig_update, true);
    ok &= hookSym(game, "_ZN14HotBarRenderer6renderER15MinecraftClientRSt10shared_ptrI9UIControlEiR13RectangleArea",
                  (void*)my_hotbar, (void**)&orig_hotbar, true);
    ok &= hookSym(game, "_ZN10Multitouch4feedEccssi", (void*)my_feed, (void**)&orig_feed, true);
    hookSym(game, "_ZN18ItemInHandRenderer6renderEbf",  (void*)my_hand,   (void**)&orig_hand,   false);
    hookSym(game, "_ZN13LevelRenderer12renderCloudsEf", (void*)my_clouds, (void**)&orig_clouds, false);
    hookSym(game, "_ZN13LevelRenderer12_renderStarsEf", (void*)my_stars,  (void**)&orig_stars,  false);
    hookSym(game, "_ZN7Weather12setRainLevelEf",        (void*)my_rain,   (void**)&orig_rain,   false);
    hookSym(game, "_ZN13LevelRenderer15renderHitSelectER11BlockSourceRK8BlockPosfb",
            (void*)my_outline, (void**)&orig_outline, false);

    // freecam: needs the camera function ranges plus both hooks
    g_rngMove = (uintptr_t)dlsym(game, "_ZN13LevelRenderer18moveCameraToPlayerER6Matrixf") & ~(uintptr_t)1;
    g_rngComp = (uintptr_t)dlsym(game, "_ZN13LevelRenderer16computeCameraPosEf") & ~(uintptr_t)1;
    g_rngFrom = (uintptr_t)dlsym(game, "_ZNK13LevelRenderer22getCameraPosFromEntityEfRK6Entity") & ~(uintptr_t)1;
    bool fc = g_rngMove && g_rngComp && g_rngFrom;
    fc &= hookSym(game, "_ZNK6Entity23getInterpolatedPositionEf", (void*)my_interp, (void**)&orig_interp, true);
    fc &= hookSym(game, "_ZN11LocalPlayer4moveERK4Vec3", (void*)my_lpmove, (void**)&orig_lpmove, true);
    g_freecamReady = fc && orig_interp && orig_lpmove;

    LOG("hooks %s, freecam %s", ok ? "installed" : "FAILED (required hook missing)",
        g_freecamReady ? "ready" : "unavailable");
    return JNI_VERSION_1_6;
}
