// libclient.so v13 - client-side mod for Minecraft PE 0.15.10 (armeabi-v7a).
// Created by LegendPro.
// All switches live in the game's Options screen (extra "LP" tab). Each feature can
// also get its own small button in the game ("BTN"), which can be moved and resized.
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

enum { SL_FPS = 0, SL_POS = 1, SL_CAM = 2, SL_ARMOR = 3 /* 4 */, SL_BANNER = 7 /* 6 */,
       SL_MAPN = 13, SL_MAPS = 14, SL_GBTN = 15 /* 21 */, SL_EDIT = 36 /* 5 */,
       SL_OPTTAB = 41, SL_OPTHDR = 42, SL_OPTHDR2 = 43, SL_OPTBTN = 44,
       SL_OPTLBL = 45 /* 23 */, SL_OPTST = 68 /* 23 */, SL_COUNT = 100 };
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
static void  (*region_getBlockID)(void*, void*, int, int, int);   // (result*, region, x, y, z): BlockID comes back via a hidden pointer
static bool  (*mob_isSneaking)(void*);
static bool  (*region_hasChunksAt)(void*, const int*, int);   // (center x,y,z), radius in blocks
static void* (*sr_singleton)();
static void  (*sr_fill)(void*, int, int, int, int, const Color*);
static void  (*sr_drawString)(void*, void*, const void*, int, int, const Color*);
static float* invGuiScale;
static void* (*client_getGuiData)(void*);
static float (*gui_getScreenW)(void*);
static float (*gui_getScreenH)(void*);

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
    OPT(mob_isSneaking,           "_ZNK3Mob10isSneakingEv")
    OPT(region_hasChunksAt,       "_ZN11BlockSource11hasChunksAtERK8BlockPosi")
    OPT(client_getGuiData,        "_ZN15MinecraftClient10getGuiDataEv")
    OPT(gui_getScreenW,           "_ZNK7GuiData14getScreenWidthEv")
    OPT(gui_getScreenH,           "_ZNK7GuiData15getScreenHeightEv")
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
static volatile int g_lastOptFrame = -100;      // last frame the game's Options screen was drawn
static volatile bool g_optPage = false;         // our "LP" page is selected in Options
static float g_optW = 515.f;                    // screen size in GUI units (read from the game)
static float g_guiW = 515.f, g_guiH = 240.f;

enum { B_VIEW, B_FPS, B_ARMOR, B_LIGHT, B_FOV, B_LEFT, B_NOHAND,
       B_NOCLOUD, B_NOSTAR, B_CLEAR, B_OUTLINE, B_AUTOSPR, B_NOBOB, B_SMOOTH,
       B_Q, B_HUD, B_FREECAM, B_NOPART, B_POS, B_MAP, B_MAPZ, B_COUNT };
static const int R_EDIT = B_COUNT, R_RESET = B_COUNT + 1;      // two extra rows on the Options page
static const int OPT_ROWS_TOTAL = B_COUNT + 2;

// long names (Options page) and short names (in-game buttons)
static const char* LABELS[B_COUNT] = {
    "F5 Button", "FPS Counter", "Armor Durability", "Full Bright", "FOV Zoom", "Left Hand", "Hide Hand",
    "No Clouds", "No Stars", "Clear Weather", "Outline Selection", "Auto Sprint", "No View Bobbing", "Smooth Camera",
    "Drop Button", "HUD Button", "Cam Button (Freecam)", "No Particles", "XYZ Position", "Minimap", "Map Zoom Button"};
static const char* SHORTL[B_COUNT] = {
    "F5", "FPS", "ARM", "LIGHT", "FOV", "LEFT", "HAND",
    "CLOUD", "STAR", "CLEAR", "OUTL", "SPRINT", "BOB", "SMOOTH",
    "Q", "HUD", "CAM", "PART", "XYZ", "MAP", "ZOOM"};
static volatile bool g_on[B_COUNT] = {
    false, true, false, false, false, false, false,
    false, false, false, true, false, false, false,
    false, false, false, false, true, true, false};
static volatile bool g_hudHidden = false;
static unsigned g_pending = 0;                  // feature actions waiting for the game thread
static unsigned g_pending2 = 0;                 // "BTN" switches waiting for the game thread

// layout, GUI units: minimap top-left, info text to its right
static const int MAP_X = 2, MAP_Y = 2, MAP_CELLS = 32, MAP_CELL = 2, MAP_SIZE = 64;

// ---- in-game buttons (enabled with the BTN switch on the Options page)
static bool g_btnOn[B_COUNT];
static int g_btnX[B_COUNT], g_btnY[B_COUNT];    // -1 = default position (stacked on the right)
static int g_btnW[B_COUNT], g_btnH[B_COUNT];    // 0 = default size
static bool g_editMode = false;                 // drag / resize the in-game buttons
static int g_editSel = -1;
static bool g_cfgDirty = false;

static void resetButtons() {
    for (int i = 0; i < B_COUNT; i++) { g_btnX[i] = -1; g_btnY[i] = -1; g_btnW[i] = 0; g_btnH[i] = 0; }
}

static int defBtnW(int i) {
    int w = (int)strlen(SHORTL[i]) * 6 + 10;
    return w < 26 ? 26 : w;
}

static bool inGameRect(int i, int* x, int* y, int* w, int* h) {
    if (i < 0 || i >= B_COUNT || !g_btnOn[i]) return false;
    *w = g_btnW[i] > 0 ? g_btnW[i] : defBtnW(i);
    *h = g_btnH[i] > 0 ? g_btnH[i] : 12;
    if (g_btnX[i] >= 0) { *x = g_btnX[i]; *y = g_btnY[i]; }
    else {
        int slot = 0;
        for (int j = 0; j < i; j++) if (g_btnOn[j] && g_btnX[j] < 0) slot++;
        *x = (int)g_guiW - *w - 8;
        *y = 24 + slot * 16;
    }
    return true;
}

// toolbar shown while editing the buttons: SIZE- SIZE+ RESET DONE
static const char* TB_LABEL[4] = {"SIZE-", "SIZE+", "RESET", "DONE"};
static bool tbRect(int k, int* x, int* y, int* w, int* h) {
    if (k < 0 || k > 3) return false;
    *x = 2 + k * 38; *y = 70; *w = 36; *h = 14;
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
static int g_origPersp = -1;
static const float FC_MOVE_MUL = 3.0f;       // horizontal speed multiplier
static const float FC_VERT_SPEED = 8.0f;     // blocks per second while sneaking (= down)
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
// Colours come from the legacy block id. The game draws fills a little later
// than we call it and keeps the colour pointer, so every colour lives in a
// permanent palette. To stay smooth the grid is built a few cells per frame
// (time budget), published only when complete, merged into big rectangles
// (few draw calls) and shifted every frame so it scrolls with the player.
static const int ZOOMS[3] = {1, 2, 4};              // blocks per map cell
static int g_zoomStep = 1;
struct Rect { unsigned char i0, i1, j0, j1; unsigned short key; };
static Rect g_rects[MAP_CELLS * MAP_CELLS];
static int g_rectCount = 0;
static unsigned short g_pubKey[MAP_CELLS * MAP_CELLS];     // grid being drawn
static unsigned short g_buildKey[MAP_CELLS * MAP_CELLS];   // grid under construction
static int g_buildHeights[MAP_CELLS * MAP_CELLS];
static int g_buildPos = 0;
static bool g_building = false;
static bool g_mapDirty = true;
static int g_bCx = 0, g_bCz = 0, g_bPy = 0, g_bZoom = 2, g_bKnown = 0;
static int g_pubCx = 0, g_pubCz = 0, g_pubZoom = 2;
static bool g_pubValid = false;
static long long g_mapMs = 0;
static void* g_mapPlayer = nullptr;
static int g_mapStatus = 0;     // 0 ok, 1 chunks not loaded, 2 no data, 3 missing symbol, 4 no region
static Color g_pal[256 * 3];                         // [id*3+shade]
static bool g_blockTransparent[256];
static bool g_blockInit = false;
static const Color MAP_FRAME = {0.f, 0.f, 0.f, 0.9f};
static const Color MAP_ME    = {1.f, 0.15f, 0.15f, 1.f};

static long long nowUs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void initBlockColors() {
    static Color base[256];
    for (int i = 0; i < 256; i++) base[i] = {0.45f, 0.45f, 0.45f, 1.f};
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
    for (const E& e : T) base[e.id] = {e.r, e.g, e.b, 1.f};
    for (int id = 0; id < 256; id++) {
        static const float M[3] = {0.80f, 1.0f, 1.15f};   // darker / normal / lighter
        for (int sh = 0; sh < 3; sh++) {
            Color c = base[id];
            c.r = c.r * M[sh] > 1.f ? 1.f : c.r * M[sh];
            c.g = c.g * M[sh] > 1.f ? 1.f : c.g * M[sh];
            c.b = c.b * M[sh] > 1.f ? 1.f : c.b * M[sh];
            g_pal[id * 3 + sh] = c;
        }
    }
    // plants, torches, rails, doors ...: skipped so the ground below shows instead
    static const int P[] = {6, 27, 28, 30, 31, 32, 37, 38, 39, 40, 50, 51, 55, 59, 63, 64, 65, 66, 68, 69,
                            70, 71, 72, 75, 76, 77, 83, 106, 111, 141, 142, 157, 175, 176, 177};
    for (int id : P) g_blockTransparent[id] = true;
    g_blockInit = true;
}

// Greedy merge of equal cells into rectangles: far fewer fills per frame.
static void buildRects() {
    static bool used[MAP_CELLS * MAP_CELLS];
    memset(used, 0, sizeof used);
    g_rectCount = 0;
    for (int j = 0; j < MAP_CELLS; j++) {
        for (int i = 0; i < MAP_CELLS; i++) {
            if (used[j * MAP_CELLS + i]) continue;
            unsigned short key = g_pubKey[j * MAP_CELLS + i];
            if (key == 0xFFFF) { used[j * MAP_CELLS + i] = true; continue; }   // unknown: not drawn
            int i1 = i + 1;
            while (i1 < MAP_CELLS && !used[j * MAP_CELLS + i1] && g_pubKey[j * MAP_CELLS + i1] == key) i1++;
            int j1 = j + 1;
            while (j1 < MAP_CELLS) {
                bool ok = true;
                for (int x = i; x < i1; x++)
                    if (used[j1 * MAP_CELLS + x] || g_pubKey[j1 * MAP_CELLS + x] != key) { ok = false; break; }
                if (!ok) break;
                j1++;
            }
            for (int y = j; y < j1; y++)
                for (int x = i; x < i1; x++) used[y * MAP_CELLS + x] = true;
            Rect& r = g_rects[g_rectCount++];
            r.i0 = (unsigned char)i; r.i1 = (unsigned char)i1;
            r.j0 = (unsigned char)j; r.j1 = (unsigned char)j1; r.key = key;
        }
    }
}

static void clearMap() {
    memset(g_pubKey, 0xFF, sizeof g_pubKey);
    g_rectCount = 0;
    g_pubValid = false;
    g_building = false;
    g_mapDirty = true;
}

static int blockIdAt(void* region, int x, int y, int z) {
    alignas(8) unsigned char buf[16];
    memset(buf, 0, sizeof buf);
    region_getBlockID(buf, region, x, y, z);          // BlockID comes back through a hidden pointer
    return buf[0];
}

static bool startBuild(void* player) {
    if (!entity_getPos || !entity_getRegion || !region_getHeightmap || !region_getBlockID ||
        !region_hasChunksAt) { g_mapStatus = 3; return false; }
    const float* pos = entity_getPos(player);
    void* region = entity_getRegion(player);
    if (!pos || !region) { g_mapStatus = 4; return false; }
    if (!g_blockInit) initBlockColors();

    g_bZoom = ZOOMS[g_zoomStep];
    g_bCx = (int)__builtin_floorf(pos[0]);
    g_bCz = (int)__builtin_floorf(pos[2]);
    g_bPy = (int)__builtin_floorf(pos[1]);

    // never touch blocks of chunks that are not loaded yet (e.g. while joining a server)
    int centre[3] = {g_bCx, 64, g_bCz};
    if (!region_hasChunksAt(region, centre, (MAP_CELLS / 2) * g_bZoom + 2)) { g_mapStatus = 1; return false; }

    g_buildPos = 0;
    g_bKnown = 0;
    g_building = true;
    g_mapDirty = false;
    return true;
}

// Continue building for at most budgetUs microseconds; publish when complete.
static void stepBuild(void* player, long long budgetUs) {
    void* region = entity_getRegion ? entity_getRegion(player) : nullptr;
    if (!region) { g_building = false; return; }
    const int half = MAP_CELLS / 2;
    const int total = MAP_CELLS * MAP_CELLS;
    long long t0 = nowUs();
    while (g_buildPos < total) {
        int idx = g_buildPos++;
        int j = idx / MAP_CELLS, i = idx % MAP_CELLS;
        int wx = g_bCx + (i - half) * g_bZoom, wz = g_bCz + (j - half) * g_bZoom;
        int h = region_getHeightmap(region, wx, wz);
        int top, steps;
        if (h >= 1 && h <= 128) { top = h > 127 ? 127 : h; steps = 8; }          // heightmap looks valid
        else { top = g_bPy + 6 > 127 ? 127 : g_bPy + 6; steps = 40; }            // otherwise look around the player
        int y = top, id = 0;
        for (int k = 0; k < steps && y > 0; k++, y--) {
            id = blockIdAt(region, wx, y, wz);
            if (id != 0 && !g_blockTransparent[id]) break;
            id = 0;
        }
        if (id == 0) { g_buildKey[idx] = 0xFFFF; g_buildHeights[idx] = 0; }
        else {
            int shade = 1;                                    // 0 darker, 1 normal, 2 lighter
            if (i > 0 && j > 0) {
                int hn = g_buildHeights[(j - 1) * MAP_CELLS + (i - 1)];
                if (hn > 0) shade = y >= hn + 2 ? 2 : (y <= hn - 2 ? 0 : 1);
            }
            g_buildKey[idx] = (unsigned short)(id * 3 + shade);
            g_buildHeights[idx] = y;
            g_bKnown++;
        }
        if ((g_buildPos & 7) == 0 && nowUs() - t0 > budgetUs) return;       // finish next frame
    }
    memcpy(g_pubKey, g_buildKey, sizeof g_pubKey);
    g_pubCx = g_bCx; g_pubCz = g_bCz; g_pubZoom = g_bZoom; g_pubValid = true;
    buildRects();
    g_mapStatus = g_bKnown ? 0 : 2;
    g_building = false;
    g_mapMs = nowMs();
}

static void drawMap(void* sr, void* font, void* client) {
    long long now = nowMs();
    void* player = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
    if (player != g_mapPlayer) {                       // new world / joined a server: wait a bit
        g_mapPlayer = player;
        clearMap();
        g_mapMs = now + 2500;
    }
    if (player && now >= g_mapMs) {
        if (!g_building && (g_mapDirty || now - g_mapMs >= 250)) {
            if (!startBuild(player)) g_mapMs = now;    // try again shortly
        }
        if (g_building) stepBuild(player, 2500);       // at most 2.5 ms per frame
    }

    // shift the published grid by whole cells so it scrolls with the player
    int di = 0, dj = 0;
    if (g_pubValid && player && entity_getPos) {
        const float* pos = entity_getPos(player);
        if (pos) {
            int cx = (int)__builtin_floorf(pos[0]), cz = (int)__builtin_floorf(pos[2]);
            di = (int)__builtin_floorf((g_pubCx - cx) / (float)g_pubZoom + 0.5f);
            dj = (int)__builtin_floorf((g_pubCz - cz) / (float)g_pubZoom + 0.5f);
        }
    }

    sr_fill(sr, MAP_X - 2, MAP_Y - 2, MAP_X + MAP_SIZE + 2, MAP_Y + MAP_SIZE + 2, &MAP_FRAME);
    if (g_blockInit) {
        for (int n = 0; n < g_rectCount; n++) {
            const Rect& r = g_rects[n];
            int i0 = r.i0 + di, i1 = r.i1 + di, j0 = r.j0 + dj, j1 = r.j1 + dj;
            if (i0 < 0) i0 = 0;
            if (j0 < 0) j0 = 0;
            if (i1 > MAP_CELLS) i1 = MAP_CELLS;
            if (j1 > MAP_CELLS) j1 = MAP_CELLS;
            if (i0 >= i1 || j0 >= j1) continue;
            sr_fill(sr, MAP_X + i0 * MAP_CELL, MAP_Y + j0 * MAP_CELL,
                        MAP_X + i1 * MAP_CELL, MAP_Y + j1 * MAP_CELL, &g_pal[r.key]);
        }
    }
    int cx = MAP_X + (MAP_CELLS / 2) * MAP_CELL;           // player marker
    int cy = MAP_Y + (MAP_CELLS / 2) * MAP_CELL;
    sr_fill(sr, cx - 1, cy - 1, cx + 3, cy + 3, &MAP_ME);
    text(SL_MAPN, sr, font, "N", MAP_X + MAP_SIZE / 2 - 2, MAP_Y + 1, &C_YELLOW);
    if (g_mapStatus != 0 && !g_pubValid) {             // tell why the map is empty
        static const char* ST[5] = {"", "LOADING", "NO DATA", "NO SYM", "NO REGION"};
        text(SL_MAPS, sr, font, ST[g_mapStatus], MAP_X + 3, MAP_Y + MAP_SIZE - 10, &C_RED);
    }
}

// ---------------- saved settings ----------------
// Button layout and the simple on/off flags are saved in the app's data folder.
static char g_cfgPath[300];

static bool persistFlag(int i) {
    return i == B_FPS || i == B_ARMOR || i == B_NOHAND || i == B_NOCLOUD || i == B_NOSTAR ||
           i == B_CLEAR || i == B_OUTLINE || i == B_AUTOSPR || i == B_POS || i == B_MAP;
}

static void cfgInitPath() {
    char name[200] = {0};
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) { size_t n = fread(name, 1, sizeof name - 1, f); name[n] = 0; fclose(f); }
    char* colon = strchr(name, ':');
    if (colon) *colon = 0;
    if (!name[0]) return;
    snprintf(g_cfgPath, sizeof g_cfgPath, "/data/data/%s/lp_buttons.cfg", name);
}

static void cfgSave() {
    if (!g_cfgPath[0]) return;
    FILE* f = fopen(g_cfgPath, "w");
    if (!f) return;
    fprintf(f, "z %d\n", g_zoomStep);
    for (int i = 0; i < B_COUNT; i++)
        fprintf(f, "b %d %d %d %d %d %d\n", i, g_btnOn[i] ? 1 : 0, g_btnX[i], g_btnY[i], g_btnW[i], g_btnH[i]);
    for (int i = 0; i < B_COUNT; i++)
        if (persistFlag(i)) fprintf(f, "f %d %d\n", i, g_on[i] ? 1 : 0);
    fclose(f);
}

static void cfgLoad() {
    if (!g_cfgPath[0]) return;
    FILE* f = fopen(g_cfgPath, "r");
    if (!f) return;
    char kind;
    while (fscanf(f, " %c", &kind) == 1) {
        if (kind == 'z') {
            int z;
            if (fscanf(f, "%d", &z) == 1 && z >= 0 && z <= 2) g_zoomStep = z;
        } else if (kind == 'b') {
            int i, on, x, y, w, h;
            if (fscanf(f, "%d %d %d %d %d %d", &i, &on, &x, &y, &w, &h) == 6 && i >= 0 && i < B_COUNT) {
                g_btnOn[i] = on != 0; g_btnX[i] = x; g_btnY[i] = y; g_btnW[i] = w; g_btnH[i] = h;
            }
        } else if (kind == 'f') {
            int i, v;
            if (fscanf(f, "%d %d", &i, &v) == 2 && i >= 0 && i < B_COUNT && persistFlag(i)) g_on[i] = v != 0;
        } else break;
    }
    fclose(f);
}

static void updateGuiSize(void* client) {
    if (!client_getGuiData) return;
    void* gd = client_getGuiData(client);
    if (!gd) return;
    if (gui_getScreenW) { float w = gui_getScreenW(gd); if (w > 100.f && w < 3000.f) g_guiW = w; }
    if (gui_getScreenH) { float h = gui_getScreenH(gd); if (h > 60.f && h < 3000.f) g_guiH = h; }
}

static void drawOverlay(void* client) {
    void* sr = sr_singleton();
    void* font = client_getFont(client);
    if (!sr || !font) return;

    updateGuiSize(client);
    if (g_on[B_MAP]) drawMap(sr, font, client);

    // info text: right of the map
    int tx = g_on[B_MAP] ? MAP_X + MAP_SIZE + 6 : 4;
    int ty = 4;

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

    // in-game buttons
    for (int i = 0; i < B_COUNT; i++) {
        int x, y, w, h;
        if (!inGameRect(i, &x, &y, &w, &h)) continue;
        if (g_editMode)
            sr_fill(sr, x - 1, y - 1, x + w + 1, y + h + 1, i == g_editSel ? &C_YELLOW : &C_OFF);
        sr_fill(sr, x, y, x + w, y + h, &C_BG);
        bool action = (i == B_VIEW || i == B_Q || i == B_MAPZ);
        const Color* tc = (!action && g_on[i]) ? &C_ON : &C_OFF;
        int tw = (int)strlen(SHORTL[i]) * 6;
        text(SL_GBTN + i, sr, font, SHORTL[i], x + (w - tw) / 2, y + (h - 8) / 2, tc);
    }

    // edit toolbar
    if (g_editMode) {
        text(SL_EDIT, sr, font, "EDIT: drag a button, then SIZE", 2, 88, &C_YELLOW);
        for (int k = 0; k < 4; k++) {
            int x, y, w, h;
            tbRect(k, &x, &y, &w, &h);
            sr_fill(sr, x, y, x + w, y + h, &C_BG);
            text(SL_EDIT + 1 + k, sr, font, TB_LABEL[k], x + 4, y + 3, k == 3 ? &C_ON : &C_OFF);
        }
    }
}

// ---------------- LegendPro page inside the game's Options screen ----------------
// The Options screen shows a column of category tabs on the left. We add a
// 4th tab ("LP") under them; when it is selected a list of all switches
// covers the content area, like a normal settings page. Every row has a
// BTN switch (show a button for it in the game) and the feature switch.
static const int OPT_TAB_X = 5, OPT_TAB_Y = 176, OPT_TAB_W = 28, OPT_TAB_H = 27;
static const int OPT_CX0 = 38, OPT_CY0 = 27, OPT_CY1 = 225;      // content area (GUI units)
static const int OPT_ROW0 = 44, OPT_ROW_H = 14, OPT_PER_COL = 12;
static const Color OPT_BG   = {0.07f, 0.07f, 0.08f, 1.f};
static const Color OPT_ROW  = {0.17f, 0.17f, 0.18f, 1.f};
static const Color OPT_SWON = {0.15f, 0.60f, 0.20f, 1.f};
static const Color OPT_SWOF = {0.38f, 0.38f, 0.38f, 1.f};
static const Color OPT_WHITE_FRAME = {1.f, 1.f, 1.f, 1.f};
static const Color OPT_GREY_FRAME  = {0.45f, 0.45f, 0.45f, 1.f};

static bool optRowRect(int i, int* x0, int* y0, int* x1, int* y1) {
    if (i < 0 || i >= OPT_ROWS_TOTAL) return false;
    float colW = (g_optW - OPT_CX0 - 12.f) / 2.f;
    int col = i / OPT_PER_COL, row = i % OPT_PER_COL;
    *x0 = OPT_CX0 + 6 + (int)(col * colW);
    *x1 = *x0 + (int)colW - 4;
    *y0 = OPT_ROW0 + row * OPT_ROW_H;
    *y1 = *y0 + OPT_ROW_H - 2;
    return true;
}

// hit codes: -2 = our tab, 0..22 = feature switch, 100+i = BTN switch, -3 = covered area, -1 = none
static int optHit(float gx, float gy) {
    if (gx >= OPT_TAB_X && gx < OPT_TAB_X + OPT_TAB_W && gy >= OPT_TAB_Y && gy < OPT_TAB_Y + OPT_TAB_H) return -2;
    if (!g_optPage) return -1;
    for (int i = 0; i < OPT_ROWS_TOTAL; i++) {
        int x0, y0, x1, y1;
        if (!optRowRect(i, &x0, &y0, &x1, &y1) || gy < y0 || gy >= y1) continue;
        if (gx >= x1 - 34 && gx < x1 - 3) return i;                              // feature switch
        if (i < B_COUNT && gx >= x1 - 72 && gx < x1 - 38) return 100 + i;        // BTN switch
    }
    if (gx >= OPT_CX0 && gy >= OPT_CY0 && gy < OPT_CY1) return -3;
    return -1;
}

static void optStateText(int i, char* buf, size_t n, bool* on) {
    if (i == R_EDIT)  { snprintf(buf, n, g_editMode ? "ON" : "OFF"); *on = g_editMode; return; }
    if (i == R_RESET) { snprintf(buf, n, "TAP"); *on = false; return; }
    *on = g_on[i];
    switch (i) {
    case B_VIEW: case B_Q:   snprintf(buf, n, "TAP"); *on = false; break;
    case B_MAPZ:             snprintf(buf, n, "%dB", ZOOMS[g_zoomStep]); *on = g_zoomStep != 1; break;
    case B_FOV:              if (g_fovStep) snprintf(buf, n, "%d", (int)FOVS[g_fovStep - 1]); else snprintf(buf, n, "OFF");
                             *on = g_fovStep != 0; break;
    default:                 snprintf(buf, n, g_on[i] ? "ON" : "OFF"); break;
    }
}

static void drawOptionsPage(void* client) {
    void* sr = sr_singleton();
    void* font = client_getFont(client);
    if (!sr || !font) return;

    if (client_getGuiData && gui_getScreenW) {
        void* gd = client_getGuiData(client);
        float w = gd ? gui_getScreenW(gd) : 0.f;
        if (w > 100.f && w < 3000.f) g_optW = w;
    }

    // the extra category tab under the game's three
    sr_fill(sr, OPT_TAB_X - 1, OPT_TAB_Y - 1, OPT_TAB_X + OPT_TAB_W + 1, OPT_TAB_Y + OPT_TAB_H + 1,
            g_optPage ? &OPT_WHITE_FRAME : &OPT_GREY_FRAME);
    sr_fill(sr, OPT_TAB_X, OPT_TAB_Y, OPT_TAB_X + OPT_TAB_W, OPT_TAB_Y + OPT_TAB_H, &C_BG);
    text(SL_OPTTAB, sr, font, "LP", OPT_TAB_X + 8, OPT_TAB_Y + 10, &C_CYAN);

    if (!g_optPage) return;

    sr_fill(sr, OPT_CX0, OPT_CY0, (int)g_optW, OPT_CY1, &OPT_BG);
    text(SL_OPTHDR,  sr, font, "LegendPro  -  Created by LegendPro", OPT_CX0 + 8, OPT_CY0 + 2, &C_CYAN);
    text(SL_OPTHDR2, sr, font, "BTN = show a button for it in the game.  Edit Button = move / resize them.",
         OPT_CX0 + 8, OPT_CY0 + 13, &C_YELLOW);
    for (int i = 0; i < OPT_ROWS_TOTAL; i++) {
        int x0, y0, x1, y1;
        if (!optRowRect(i, &x0, &y0, &x1, &y1)) continue;
        sr_fill(sr, x0, y0, x1, y1, &OPT_ROW);
        char buf[16];
        bool on;
        optStateText(i, buf, sizeof buf, &on);
        const char* label = i < B_COUNT ? LABELS[i] : (i == R_EDIT ? "Edit Button (move/resize)" : "Reset Button Layout");
        text(SL_OPTLBL + i, sr, font, label, x0 + 4, y0 + 2, on ? &C_ON : &C_OFF);
        if (i < B_COUNT) {
            sr_fill(sr, x1 - 72, y0 + 1, x1 - 38, y1 - 1, g_btnOn[i] ? &OPT_SWON : &OPT_SWOF);
            text(SL_OPTBTN, sr, font, "BTN", x1 - 66, y0 + 2, &C_OFF);
        }
        sr_fill(sr, x1 - 34, y0 + 1, x1 - 3, y1 - 1, on ? &OPT_SWON : &OPT_SWOF);
        text(SL_OPTST + i, sr, font, buf, x1 - 31, y0 + 2, &C_OFF);
    }
}

// ============================ actions (game thread) ============================
static void setFreecam(bool on, void* client, void* opts) {
    if (on) {
        if (!g_freecamReady || !client_getLocalPlayer) return;
        g_player = client_getLocalPlayer(client);
        if (!g_player) return;
        g_camOff = {0.f, 0.f, 0.f};
        if (opt_getPersp && opt_setPersp) {              // third person so the player body is visible
            g_origPersp = opt_getPersp(opts);
            opt_setPersp(opts, 1);
        }
        g_freecam = true;
    } else {
        g_freecam = false;
        g_camOff = {0.f, 0.f, 0.f};
        if (g_origPersp >= 0 && opt_setPersp) opt_setPersp(opts, g_origPersp);
        g_origPersp = -1;
    }
    g_on[B_FREECAM] = g_freecam;
}

static void runAction(int i, void* client) {
    void* opts = client_getOptions(client);
    switch (i) {
    case B_VIEW:
        if (client_toggleThirdPerson && client_getLocalPlayer && client_getLocalPlayer(client))
            client_toggleThirdPerson(client);
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
    case B_MAPZ:                                          // map zoom: 1 / 2 / 4 blocks per cell
        g_zoomStep = (g_zoomStep + 1) % 3;
        g_mapDirty = true;
        break;
    case R_EDIT:                                          // edit mode: drag / resize the in-game buttons
        g_editMode = !g_editMode;
        g_editSel = -1;
        break;
    case R_RESET:
        resetButtons();
        g_editSel = -1;
        break;
    case B_NOPART:
        if (!opt_setParticles || !opt_getParticles) break;
        if (g_origPart < 0) g_origPart = opt_getParticles(opts);
        g_on[i] = !g_on[i];
        opt_setParticles(opts, g_on[i] ? 0.f : g_origPart);
        break;
    default:                                              // plain flags read by hooks / overlay
        if (i >= 0 && i < B_COUNT) g_on[i] = !g_on[i];
        break;
    }
    g_cfgDirty = true;
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

    if (g_frame - g_lastOptFrame > 10) g_optPage = false;

    unsigned m2 = __atomic_exchange_n(&g_pending2, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; i < B_COUNT; i++)
        if (m2 & (1u << i)) { g_btnOn[i] = !g_btnOn[i]; g_cfgDirty = true; }

    unsigned m = __atomic_exchange_n(&g_pending, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; m; i++, m >>= 1)
        if (m & 1) runAction(i, client);

    static long long cfgMs = 0;
    if (g_cfgDirty && now - cfgMs > 1000) { cfgMs = now; g_cfgDirty = false; cfgSave(); }

    if (opt_getHideGui) {
        void* o = client_getOptions(client);
        if (o) { bool h = opt_getHideGui(o); g_hudHidden = h; g_on[B_HUD] = h; }
    }

    if (g_freecam) {
        void* p = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
        if (!p) setFreecam(false, client, client_getOptions(client));   // left the world
        else {
            g_player = p;
            if (mob_isSneaking && mob_isSneaking(p)) g_camOff.y -= FC_VERT_SPEED * dt;   // sneak = down
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
static bool g_cap[16];            // pointer ids whose events we swallow (they started on one of our buttons)
static int g_capFrame[16];
static int g_drag[16];            // in-game button being dragged by this pointer (edit mode), -1 = none
static float g_grabX[16], g_grabY[16];

static void my_feed(char b, char down, short x, short y, int id) {
    int s = id & 15;
    bool press = (b != 0 && down == 1);
    bool release = (b != 0 && down == 0);
    float inv = invScale();
    float gx = x * inv, gy = y * inv;

    if (g_cap[s]) {
        if (press) { g_cap[s] = false; g_drag[s] = -1; }              // a new touch with the same id
        else {
            if (g_drag[s] >= 0 && g_editMode) {                       // dragging an in-game button
                int i = g_drag[s], bx, by, bw, bh;
                if (inGameRect(i, &bx, &by, &bw, &bh)) {
                    int nx = (int)(gx - g_grabX[s]), ny = (int)(gy - g_grabY[s]);
                    int maxX = (int)g_guiW - bw, maxY = (int)g_guiH - bh;
                    g_btnX[i] = nx < 0 ? 0 : (nx > maxX ? maxX : nx);
                    g_btnY[i] = ny < 0 ? 0 : (ny > maxY ? maxY : ny);
                }
                g_capFrame[s] = g_frame;
            }
            if (release || g_frame - g_capFrame[s] > 120) {
                if (g_drag[s] >= 0) g_cfgDirty = true;
                g_cap[s] = false;
                g_drag[s] = -1;
            }
            return;
        }
    }

    bool optVisible = g_frame - g_lastOptFrame <= 2;
    if (optVisible && press) {
        int hit = optHit(gx, gy);
        if (hit == -2) {                                   // our tab
            g_optPage = true;
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            return;
        }
        if (g_optPage) {
            if (hit >= 100) __atomic_fetch_or(&g_pending2, 1u << (hit - 100), __ATOMIC_ACQ_REL);
            else if (hit >= 0) __atomic_fetch_or(&g_pending, 1u << hit, __ATOMIC_ACQ_REL);
            if (hit >= 0 || hit == -3) { g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1; return; }
            g_optPage = false;                             // another tab / Back: close our page, game handles the tap
        }
    }
    if (optVisible) { orig_feed(b, down, x, y, id); return; }   // no HUD buttons on the Options screen

    bool hudShown = g_frame - g_lastHudFrame <= 2;
    if (g_hudHidden && !hudShown && press) {
        if (gx < 78 && gy < 90) {                                  // HUD hidden: tap the top-left corner to bring it back
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            __atomic_fetch_or(&g_pending, 1u << B_HUD, __ATOMIC_ACQ_REL);
            return;
        }
    }
    if (hudShown && press) {
        if (g_editMode) {
            for (int k = 0; k < 4; k++) {                           // edit toolbar
                int tx, ty, tw, th;
                if (!tbRect(k, &tx, &ty, &tw, &th) || gx < tx || gx >= tx + tw || gy < ty || gy >= ty + th) continue;
                int i = g_editSel;
                if (k == 0 || k == 1) {
                    if (i >= 0 && i < B_COUNT && g_btnOn[i]) {
                        int bx, by, bw, bh;
                        inGameRect(i, &bx, &by, &bw, &bh);
                        int d = k == 1 ? 1 : -1;
                        if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; }    // keep it where it is
                        bw += d * 4; bh += d * 2;
                        g_btnW[i] = bw < 20 ? 20 : (bw > 90 ? 90 : bw);
                        g_btnH[i] = bh < 10 ? 10 : (bh > 40 ? 40 : bh);
                        g_cfgDirty = true;
                    }
                } else if (k == 2) {
                    if (i >= 0 && i < B_COUNT) { g_btnX[i] = -1; g_btnY[i] = -1; g_btnW[i] = 0; g_btnH[i] = 0; g_cfgDirty = true; }
                } else {
                    g_editMode = false; g_editSel = -1; g_cfgDirty = true;
                }
                g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
                return;
            }
        }
        for (int i = 0; i < B_COUNT; i++) {                         // in-game buttons
            int bx, by, bw, bh;
            if (!inGameRect(i, &bx, &by, &bw, &bh)) continue;
            if (gx < bx || gx >= bx + bw || gy < by || gy >= by + bh) continue;
            g_cap[s] = true; g_capFrame[s] = g_frame;
            if (g_editMode) {                                       // select and start dragging
                g_editSel = i;
                if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; g_btnW[i] = bw; g_btnH[i] = bh; }
                g_drag[s] = i;
                g_grabX[s] = gx - bx;
                g_grabY[s] = gy - by;
            } else {
                g_drag[s] = -1;
                __atomic_fetch_or(&g_pending, 1u << i, __ATOMIC_ACQ_REL);
            }
            return;
        }
    }
    orig_feed(b, down, x, y, id);
}

// The game's Options screen: draw our tab/page on top of it every frame.
static void (*orig_optRender)(void*, int, int, float) = nullptr;
static void my_optRender(void* self, int mx, int my, float a) {
    orig_optRender(self, mx, my, a);
    if (!g_client) return;
    g_lastOptFrame = g_frame;
    drawOptionsPage(g_client);
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
// have made is applied to the camera instead. Joystick/D-pad = horizontal,
// jump button = up (only upward motion is used, so gravity never pulls the
// camera down), sneak button = down (handled in my_update).
static void (*orig_lpmove)(void*, const float*) = nullptr;
static void my_lpmove(void* self, const float* d) {
    if (g_freecam && self && self == g_player) {
        g_camOff.x += d[0] * FC_MOVE_MUL;
        g_camOff.z += d[2] * FC_MOVE_MUL;
        if (d[1] > 0.f) g_camOff.y += d[1];           // jump held = rise
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

    resetButtons();
    cfgInitPath();
    cfgLoad();

    bool ok = true;
    ok &= hookSym(game, "_ZN15MinecraftClient6updateEv", (void*)my_update, (void**)&orig_update, true);
    ok &= hookSym(game, "_ZN14HotBarRenderer6renderER15MinecraftClientRSt10shared_ptrI9UIControlEiR13RectangleArea",
                  (void*)my_hotbar, (void**)&orig_hotbar, true);
    ok &= hookSym(game, "_ZN10Multitouch4feedEccssi", (void*)my_feed, (void**)&orig_feed, true);
    hookSym(game, "_ZN13OptionsScreen6renderEiif", (void*)my_optRender, (void**)&orig_optRender, false);
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
