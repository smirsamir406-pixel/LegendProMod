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
       SL_MAPN = 13, SL_MAPS = 14, SL_GBTN = 15 /* 29 */, SL_EDIT = 44 /* 5 */, SL_MAPC = 49 /* 3 */,
       SL_RP = 52 /* 10 */, SL_OPTTAB = 62, SL_OPTHDR = 63, SL_OPTHDR2 = 64,
       SL_OPTLBL = 65 /* 30 */, SL_OPTST = 95 /* 30 */, SL_CINE = 150 /* 6 */, SL_COUNT = 160 };
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
static void  (*client_handleDrop)(void*);                          // MinecraftClient::handleDropButtonPress (the game's own Drop button, network-aware)
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
static void* (*client_getServer)(void*);                          // the networking "Minecraft" object
static bool  (*mc_isOnlineClient)(void*);                         // true when connected to a remote server
static void* (*mc_getCommandParser)(void*);                       // Minecraft::getCommandParser()
static void* (*mc_getGameMode)(void*);
static void* (*client_getLevelArchiver)(void*);
static void* (*client_getTextures)(void*);
static void  (*parser_clear)(void*);                              // ServerCommandParser::clear()
static void  (*cmds_setupServer)(void*, void*);                   // ServerCommands::setupStandardServer(parser, Level&)
static void  (*cmds_setupDebug)(void*, void*);                    // ClientCommands::setupDebug(parser, Options&)
static void  (*cmds_setupProfiling)(void*, void*);                // ClientCommands::setupProfiling(parser, Options&)
static void  (*cmds_setupStandard)(void*, void*, void*, void*, void*);   // (parser, LocalPlayer&, TextureGroup&, LevelArchiver&, GameMode&)
static const void* (*client_getConnectedServer)(void*);          // std::string const& (empty in a local world)
static void  (*client_sendLocalMessage)(void*, const void*, const void*);
static void  (*entity_setPos)(void*, const float*);
static void* (*entity_getLevel)(void*);
static void  (*level_setTime)(void*, int);
static void  (*player_setGameType)(void*, int);
static void  (*mob_heal)(void*, int);
static int   (*mob_getHealth)(void*);
static int   (*level_getTime)(void*);
static void  (*level_setSpawn)(void*, const int*);               // BlockPos const& (x, y, z)
static void  (*player_addXp)(void*, int);
static void  (*player_addLevels)(void*, int);
static void  (*inv_clearSlot)(void*, int);                       // Inventory::clearSlot(int)
static void* (*player_getSelectedItem)(void*);                    // ItemInstance* in the selected slot
static int   (*item_getId)(void*);
static int   (*item_getAux)(void*);
static void  (*item_remove)(void*, int);                          // ItemInstance::remove(int count)
static const void* (*opt_getUsername)(void*);                     // NOTE: really returns std::string BY VALUE (sret: r0 = result slot, r1 = Options); use getOwnName()
static const void* getOwnName(void* options);                     // permanent std::string object (one pointer) holding the real player name
static void  (*entity_setNameTag)(void*, const void*);
static void  (*entity_setNameTagVisible)(void*, bool);
static void* (*disp_getRenderer)(void*, int);                      // EntityRenderDispatcher::getRenderer(EntityRendererId)
static void  (*er_renderText)(void*, void*, const void*, float, float);   // EntityRenderer::renderText(Entity&, string const&, float, float)
static void  (*entity_getRotation)(float*, void*);                // Vec2 comes back through a hidden pointer
static void  (*entity_setRot)(void*, const float*);               // Entity::setRot(Vec2 const&)
static void* (*disp_getInstance)();                               // EntityRenderDispatcher::getInstance()
static void  (*entity_getViewVector)(float*, void*, float);      // result (x,y,z) comes back through a hidden pointer
static void  (*item_ctor)(void*, int, int, int);
static void  (*player_drop)(void*, const void*, bool);
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
    OPT(client_handleDrop,        "_ZN15MinecraftClient21handleDropButtonPressEv")
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
    OPT(client_getServer,         "_ZN15MinecraftClient9getServerEv")
    OPT(mc_isOnlineClient,        "_ZN9Minecraft14isOnlineClientEv")
    OPT(mc_getCommandParser,      "_ZN9Minecraft16getCommandParserEv")
    OPT(mc_getGameMode,           "_ZN9Minecraft11getGameModeEv")
    OPT(client_getLevelArchiver,  "_ZN15MinecraftClient16getLevelArchiverEv")
    OPT(client_getTextures,       "_ZNK15MinecraftClient11getTexturesEv")
    OPT(parser_clear,             "_ZN19ServerCommandParser5clearEv")
    OPT(cmds_setupServer,         "_ZN14ServerCommands19setupStandardServerER19ServerCommandParserR5Level")
    OPT(cmds_setupDebug,          "_ZN14ClientCommands10setupDebugER19ServerCommandParserR7Options")
    OPT(cmds_setupProfiling,      "_ZN14ClientCommands14setupProfilingER19ServerCommandParserR7Options")
    OPT(cmds_setupStandard,       "_ZN14ClientCommands13setupStandardER19ServerCommandParserR11LocalPlayerRN3mce12TextureGroupER13LevelArchiverR8GameMode")
    OPT(client_getConnectedServer,"_ZNK15MinecraftClient18getConnectedServerEv")
    OPT(client_sendLocalMessage,  "_ZN15MinecraftClient16sendLocalMessageERKSsS1_")
    OPT(entity_setPos,            "_ZN6Entity6setPosERK4Vec3")
    OPT(entity_getLevel,          "_ZN6Entity8getLevelEv")
    OPT(level_setTime,            "_ZN5Level7setTimeEi")
    OPT(player_setGameType,       "_ZN6Player17setPlayerGameTypeE8GameType")
    OPT(mob_heal,                 "_ZN3Mob4healEi")
    OPT(mob_getHealth,            "_ZNK3Mob9getHealthEv")
    OPT(level_getTime,            "_ZNK5Level7getTimeEv")
    OPT(level_setSpawn,           "_ZN5Level15setDefaultSpawnERK8BlockPos")
    OPT(player_addXp,             "_ZN6Player13addExperienceEi")
    OPT(player_addLevels,         "_ZN6Player9addLevelsEi")
    OPT(inv_clearSlot,            "_ZN9Inventory9clearSlotEi")
    OPT(player_getSelectedItem,   "_ZNK6Player15getSelectedItemEv")
    OPT(item_getId,               "_ZNK12ItemInstance5getIdEv")
    OPT(item_getAux,              "_ZNK12ItemInstance11getAuxValueEv")
    OPT(item_remove,              "_ZN12ItemInstance6removeEi")
    OPT(opt_getUsername,          "_ZNK7Options11getUsernameEv")
    OPT(entity_setNameTag,        "_ZN6Entity10setNameTagERKSs")
    OPT(entity_setNameTagVisible, "_ZN6Entity17setNameTagVisibleEb")
    OPT(disp_getRenderer,         "_ZN22EntityRenderDispatcher11getRendererE16EntityRendererId")
    OPT(er_renderText,            "_ZN14EntityRenderer10renderTextER6EntityRKSsff")
    OPT(entity_getRotation,       "_ZNK6Entity11getRotationEv")
    OPT(entity_setRot,            "_ZN6Entity6setRotERK4Vec2")
    OPT(disp_getInstance,         "_ZN22EntityRenderDispatcher11getInstanceEv")
    OPT(entity_getViewVector,     "_ZNK6Entity13getViewVectorEf")
    OPT(item_ctor,                "_ZN12ItemInstanceC1Eiii")
    OPT(player_drop,              "_ZN6Player4dropEPK12ItemInstanceb")
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
static void* g_localPlayer = nullptr;           // the local player, refreshed every frame
static int g_timeLock = -1;                     // >= 0: /timelock keeps the world time at this value
static char g_msgQueue[8][200];                 // extra answer lines, printed from the update hook
static int g_msgQueued = 0;
static void* g_nativeTriedFor = nullptr;        // the player for whom we already enabled the game's own commands
static bool g_nativeCmdDisabled = false;        // set when a previous start died while enabling them
static bool isLocalWorld();
static void registerNativeCommands(void* client);
static void localMsg(const char* msg);
static void setFreecam(bool on, void* client, void* opts);
static void installLookHook();                // installs the freecam look hook the first time freecam is switched on

// ---- replay: the last 60 s of YOUR movement, played back with an orbit camera (own world only)
struct RpSample { float x, y, z, a, b; };       // position + the raw rotation pair of the player
static const int RP_MAX = 1210;                 // 60 s at 20 samples per second
static RpSample g_rpRing[RP_MAX];               // recording (ring buffer)
static int g_rpHead = 0, g_rpCount = 0;
static long long g_rpLastMs = 0;
static RpSample g_rpPlay[RP_MAX];               // the snapshot being played
static int g_rpN = 0;
static volatile bool g_replay = false;          // replay mode is running
static bool g_rpPlaying = true, g_rpOrbit = true;
static float g_rpT = 0.f, g_rpSpeed = 1.f, g_rpOrbitYaw = 0.f;
static float g_rpSavedPos[3] = {0.f, 0.f, 0.f};
static int g_rpSavedPersp = -1;
// cinematic camera inside the replay: FREE camera (first person, our own position/rotation), up to 8 SHOTS
// (camera position + rotation + replay time), START flies the camera through the shots while the replay plays.
static bool g_fcLook = false;                   // look-decoupling hook installed (turn + camera rotation)
struct CineKf { float x, y, z, yaw, pitch, t; };
static const int CINE_MAX = 8;
static CineKf g_kf[CINE_MAX];
static int g_kfN = 0;
static bool g_rpCine = false;                   // free camera on
static bool g_cinePlay = false;                 // camera follows the shots
static float g_cinePos[3] = {0.f, 0.f, 0.f};
static float g_cineRot[2] = {0.f, 0.f};         // same order as the game's rotation pair ([g_yawIdx] = yaw)
static volatile int g_cineReq = 0;              // touch thread -> game thread
static bool g_rpHideUi = false;                 // HIDE: every replay control disappears (clean footage); a tiny dim square brings them back
static float g_gwSpeed = 0.f, g_gwPos = 0.f;    // walking animation of the ghost (Mob::walkAnimSpeed / walkAnimPos), simulated here
enum { CR_FREE = 1, CR_ADD = 2, CR_CLEAR = 4, CR_START = 8 };
static int g_yawIdx = 1;                        // which number of the rotation pair is the yaw (auto-detected)
static float g_ghostPos[3] = {0.f, 0.f, 0.f};   // where the recorded "you" is right now (the ghost)
static float g_ghostRot[2] = {0.f, 0.f};
static volatile bool g_ghostPass = false;       // true while the ghost model is being drawn
static volatile int g_frame = 0;
static volatile int g_lastHudFrame = -100;
static volatile int g_lastOptFrame = -100;      // last frame the game's Options screen was drawn
static volatile bool g_optPage = false;         // our "LP" page is selected in Options
static float g_optW = 515.f;                    // screen size in GUI units (read from the game)
static float g_guiW = 515.f, g_guiH = 240.f;

enum { B_VIEW, B_FPS, B_ARMOR, B_LIGHT, B_FOV, B_LEFT, B_NOHAND,
       B_NOCLOUD, B_NOSTAR, B_CLEAR, B_OUTLINE, B_AUTOSPR, B_NOBOB, B_SMOOTH,
       B_Q, B_HUD, B_FREECAM, B_NOPART, B_POS, B_MAP, B_EDIT,
       B_KEEPINV, B_NOSOUL, B_CMDS, B_CLOCK, B_NOHURT, B_ZOOM, B_NAMES, B_REPLAY, B_COUNT };
static const int R_RESET = B_COUNT;                              // "reset layout" action

// short names of the in-game buttons
static const char* SHORTL[B_COUNT] = {
    "F5", "FPS", "ARMOR", "LIGHT", "FOV", "LEFT", "HAND",
    "CLOUD", "STAR", "CLEAR", "OUTLINE", "SPRINT", "BOB", "SMOOTH",
    "DROP", "HUD", "CAM", "PART", "XYZ", "MAP", "EDIT",
    "KEEP", "SOUL", "CMD", "12:00 DAY 1", "HURT", "ZOOM 3", "NAMES", "REPLAY"};
// features that can have a button on the screen (Options: BUTTONS section)
static bool isButtonFeature(int i) {
    return !(i == B_FPS || i == B_ARMOR || i == B_POS || i == B_MAP || i == B_FOV || i == B_KEEPINV || i == B_CMDS || i == B_NAMES);
}
static volatile bool g_on[B_COUNT] = {
    false, true, false, false, false, false, false,
    false, false, false, true, false, false, false,
    false, false, false, false, true, true, false,
    false, false, false,
    false,
    false,
    false,
    false,
    false};
static volatile bool g_hudHidden = false;
static unsigned g_pending = 0;                  // feature actions waiting for the game thread
static unsigned g_pending2 = 0;                 // "BTN" switches waiting for the game thread

// layout, GUI units: minimap top-left, info text to its right
static const int MAP_CELLS = 64, MAP_DEF_X = 2, MAP_DEF_Y = 2;   // the map position/size can be edited (Edit Button)

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
    int w = (int)strlen(SHORTL[i]) * 6 + 8;
    return w < 22 ? 22 : w;
}

static int mapCell() { return g_btnW[B_MAP] > 0 ? g_btnW[B_MAP] : 2; }   // gui units per map cell (1..3), default 2

static bool inGameRect(int i, int* x, int* y, int* w, int* h) {
    if (i == B_MAP) {                                  // the minimap is editable like a button
        if (!g_on[B_MAP]) return false;
        *w = *h = MAP_CELLS * mapCell();
        *x = g_btnX[B_MAP] >= 0 ? g_btnX[B_MAP] : MAP_DEF_X;
        *y = g_btnY[B_MAP] >= 0 ? g_btnY[B_MAP] : MAP_DEF_Y;
        return true;
    }
    if (i < 0 || i >= B_COUNT || !g_btnOn[i]) return false;
    *w = g_btnW[i] > 0 ? g_btnW[i] : defBtnW(i);
    *h = g_btnH[i] > 0 ? g_btnH[i] : 11;
    if (g_btnX[i] >= 0) { *x = g_btnX[i]; *y = g_btnY[i]; }
    else {
        int slot = 0;
        for (int j = 0; j < i; j++) if (g_btnOn[j] && g_btnX[j] < 0) slot++;
        *x = (int)g_guiW - *w - 8 - (slot / 8) * 62;
        *y = 24 + (slot % 8) * 16;
    }
    return true;
}

// toolbar shown while editing the buttons: SIZE- SIZE+ RESET DONE
static const char* TB_LABEL[4] = {"SIZE-", "SIZE+", "RESET", "DONE"};
static bool tbRect(int k, int* x, int* y, int* w, int* h) {
    if (k < 0 || k > 3) return false;
    *x = (int)g_guiW / 2 - 78 + k * 40; *y = 26; *w = 38; *h = 14;
    return true;
}

static float g_origGamma = -1.f, g_origFov = -1.f, g_origPart = -1.f, g_zoomOrigFov = -1.f;
static int g_zoomLevel = 0;                    // 0 = normal, 1..3 = zoomed in
static const float ZOOM_FOV[3] = {50.f, 35.f, 20.f};
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
static uintptr_t g_rngPrep = 0, g_rngLevel = 0;   // EntityRenderDispatcher::prepare (name tag billboard), LevelRenderer::renderLevel
static float g_fcRot[2] = {0.f, 0.f};         // freecam: the camera's own rotation (the body keeps its rotation)
static const float FC_MAX_H = 48.f;           // max horizontal distance of the camera from the player (chunks only load around the player)
static const float FC_MAX_UP = 48.f, FC_MAX_DOWN = 24.f;
static void fcClamp() {                       // keep the camera inside the area the game keeps loaded
    float h = __builtin_sqrtf(g_camOff.x * g_camOff.x + g_camOff.z * g_camOff.z);
    if (h > FC_MAX_H) { float k = FC_MAX_H / h; g_camOff.x *= k; g_camOff.z *= k; }
    if (g_camOff.y > FC_MAX_UP) g_camOff.y = FC_MAX_UP;
    if (g_camOff.y < -FC_MAX_DOWN) g_camOff.y = -FC_MAX_DOWN;
}

static const Color C_ON   = {0.2f, 0.9f, 0.3f, 1.f};
static const Color C_OFF  = {1.f, 1.f, 1.f, 1.f};
static const Color C_BG   = {0.f, 0.f, 0.f, 0.85f};     // black buttons
static const Color C_CYAN = {0.3f, 0.9f, 1.0f, 1.f};
static const Color C_YELLOW = {1.0f, 0.9f, 0.2f, 1.f};
static const Color C_RED  = {1.0f, 0.3f, 0.3f, 1.f};
static const Color C_BTN_BG = {0.f, 0.f, 0.f, 0.60f};         // in-game buttons: off
static const Color C_BTN_ON = {0.10f, 0.55f, 0.20f, 0.85f};   // in-game buttons: feature on

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
static const int ZOOMS[3] = {1, 2, 4};              // blocks per map cell (default: 1 -> 64 blocks wide)
static int g_zoomStep = 0;                         // 0 = 1 block per cell (sharpest)
struct Rect { unsigned char i0, i1, j0, j1; unsigned short key; };
static Rect g_rects[MAP_CELLS * MAP_CELLS];
static int g_rectCount = 0;
static unsigned short g_pubKey[MAP_CELLS * MAP_CELLS];     // grid being drawn
static unsigned short g_buildKey[MAP_CELLS * MAP_CELLS];   // grid under construction
static int g_buildHeights[MAP_CELLS * MAP_CELLS];
static int g_buildPos = 0;
static bool g_building = false;
static bool g_mapDirty = true;
static bool g_reanchor = true;                       // next build centres on the player
static bool g_bShade = true;                         // relief shading
static int g_bStride = 1;                            // map detail: 1 = every block, 2 / 4 = coarser (much faster, far fewer fills)
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
        {1, .50f, .50f, .50f}, {2, .45f, .76f, .28f}, {3, .50f, .35f, .20f}, {4, .45f, .45f, .45f},
        {5, .65f, .52f, .30f}, {7, .18f, .18f, .18f}, {8, .20f, .35f, .85f}, {9, .20f, .35f, .85f},
        {10, .95f, .45f, .10f}, {11, .95f, .45f, .10f}, {12, .87f, .82f, .55f}, {13, .55f, .50f, .48f},
        {14, .70f, .65f, .40f}, {15, .60f, .52f, .45f}, {16, .35f, .35f, .35f}, {17, .30f, .20f, .09f},
        {18, .05f, .30f, .06f}, {24, .80f, .75f, .50f}, {45, .60f, .30f, .25f}, {48, .30f, .45f, .30f},
        {49, .10f, .05f, .15f}, {60, .45f, .30f, .15f}, {78, .95f, .95f, 1.f}, {79, .60f, .80f, .95f},
        {80, .95f, .95f, 1.f}, {81, .10f, .50f, .15f}, {82, .60f, .62f, .70f}, {86, .85f, .50f, .10f},
        {87, .45f, .15f, .15f}, {88, .35f, .25f, .20f}, {89, .95f, .85f, .40f}, {98, .48f, .48f, .48f},
        {110, .45f, .38f, .45f}, {121, .85f, .85f, .60f}, {161, .07f, .34f, .09f}, {162, .28f, .19f, .09f},
        {172, .60f, .35f, .25f}, {174, .70f, .85f, .95f}, {155, .90f, .90f, .88f}, {43, .55f, .55f, .55f},
        {21, .25f, .35f, .70f}, {56, .45f, .80f, .85f}, {73, .60f, .25f, .25f}, {74, .60f, .25f, .25f}, {129, .30f, .75f, .40f},
        {9, .20f, .35f, .85f}, {44, .55f, .55f, .55f}, {53, .60f, .48f, .28f}, {67, .45f, .45f, .45f}, {85, .60f, .48f, .28f},
    };
    for (const E& e : T) base[e.id] = {e.r, e.g, e.b, 1.f};
    for (int id = 0; id < 256; id++) {
        static const float M[3] = {0.72f, 1.0f, 1.22f};   // darker / normal / lighter
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
    g_reanchor = true;
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
    if (g_pubValid && !g_reanchor && g_pubZoom == g_bZoom) {          // refresh: keep the map where it is
        g_bCx = g_pubCx;
        g_bCz = g_pubCz;
    } else {                                                          // (re)centre on the player
        g_bCx = (int)__builtin_floorf(pos[0]);
        g_bCz = (int)__builtin_floorf(pos[2]);
    }
    g_reanchor = false;
    g_bShade = true;
    {                                                                 // choose the detail: coarser on servers / slow phones / busy maps
        bool local = isLocalWorld();
        int st = local ? 1 : 2;
        if (g_fps > 0 && g_fps < 12) st = local ? 2 : 4;
        if (g_rectCount > 1800) st *= 2;
        g_bStride = st > 4 ? 4 : st;
    }
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
    const int st = g_bStride;
    const int sg = MAP_CELLS / st;                      // size of the sampled grid
    const int total = sg * sg;
    long long t0 = nowUs();
    while (g_buildPos < total) {
        int sidx = g_buildPos++;
        int sj = sidx / sg, si = sidx % sg;
        int i = si * st, j = sj * st;                   // top-left cell of this sample (covers st x st cells)
        int idx = j * MAP_CELLS + i;
        int wx = g_bCx + (i - half) * g_bZoom + (st * g_bZoom) / 2;
        int wz = g_bCz + (j - half) * g_bZoom + (st * g_bZoom) / 2;
        int h = region_getHeightmap(region, wx, wz);
        int top, steps;
        if (h >= 1 && h <= 128) { top = h > 127 ? 127 : h; steps = 8; }          // heightmap looks valid
        else {                                                                   // otherwise start near the neighbour's height
            int hint = i >= st ? g_buildHeights[idx - st] : (j >= st ? g_buildHeights[idx - st * MAP_CELLS] : 0);
            top = (hint > 0 ? hint + 6 : g_bPy + 6);
            if (top > 127) top = 127;
            steps = 36;
        }
        int y = top, id = 0;
        for (int k = 0; k < steps && y > 0; k++, y--) {
            id = blockIdAt(region, wx, y, wz);
            if (id != 0 && !g_blockTransparent[id]) break;
            id = 0;
        }
        unsigned short key = 0xFFFF;
        int ycell = 0;
        if (id != 0) {
            int shade = 1;                                    // 0 darker, 1 normal, 2 lighter
            if (id == 8 || id == 9) {                         // water: depth decides the shade (deep = dark)
                int d = 0;
                for (int yy = y - 1; yy > 0 && d < 8; yy--) {
                    int bb = blockIdAt(region, wx, yy, wz);
                    if (bb == 8 || bb == 9) d++; else break;
                }
                shade = d >= 5 ? 0 : (d >= 2 ? 1 : 2);
            } else if (g_bShade && i >= st && j >= st) {
                int hn = g_buildHeights[(j - st) * MAP_CELLS + (i - st)];
                if (hn > 0) shade = y > hn ? 2 : (y < hn ? 0 : 1);
            }
            key = (unsigned short)(id * 3 + shade);
            ycell = y;
            g_bKnown++;
        }
        for (int dj = 0; dj < st; dj++)
            for (int di = 0; di < st; di++) {
                g_buildKey[(j + dj) * MAP_CELLS + i + di] = key;
                g_buildHeights[(j + dj) * MAP_CELLS + i + di] = ycell;
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

static void drawMap(void* sr, void* font, void* client, int mx, int my, int cell) {
    long long now = nowMs();
    void* player = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
    if (player != g_mapPlayer) {                       // new world / joined a server: wait a bit
        g_mapPlayer = player;
        clearMap();
        g_mapMs = now + 2500;
    }

    // The map is rebuilt rarely (terrain refresh) and in small time slices, so it does not lag -
    // especially on servers, where the refresh is even gentler and slow phones get smaller slices.
    static long long localCheckMs = 0;
    static bool local = true;
    if (now - localCheckMs > 1000) { localCheckMs = now; local = isLocalWorld(); }
    const long long refreshMs = local ? 250 : 1500;    // servers: gentler
    const long long budgetUs = local ? 2500 : 1200;
    if (player && now >= g_mapMs) {
        if (!g_building && (g_mapDirty || now - g_mapMs >= refreshMs)) {
            if (!startBuild(player)) g_mapMs = now;    // try again shortly
        }
        if (g_building) stepBuild(player, budgetUs);
    }

    // the red dot moves over the map; near the edge the map is re-centred on the player
    const float half = MAP_CELLS / 2.f;
    float dotX = half, dotZ = half;
    if (g_pubValid && player && entity_getPos) {
        const float* pos = entity_getPos(player);
        if (pos) {
            dotX = half + (pos[0] - g_pubCx) / (float)g_pubZoom;
            dotZ = half + (pos[2] - g_pubCz) / (float)g_pubZoom;
            float offX = dotX - half, offZ = dotZ - half;
            if (!g_building && (__builtin_fabsf(offX) > half - 8.f || __builtin_fabsf(offZ) > half - 8.f)) {
                g_reanchor = true;
                g_mapDirty = true;
            }
        }
    }
    if (dotX < 0.f) dotX = 0.f;
    if (dotX > MAP_CELLS - 1.f) dotX = MAP_CELLS - 1.f;
    if (dotZ < 0.f) dotZ = 0.f;
    if (dotZ > MAP_CELLS - 1.f) dotZ = MAP_CELLS - 1.f;

    const int S = MAP_CELLS * cell;                    // map size on the screen (GUI units)
    sr_fill(sr, mx - 2, my - 2, mx + S + 2, my + S + 2, &MAP_FRAME);
    if (g_blockInit) {
        for (int n = 0; n < g_rectCount; n++) {
            const Rect& r = g_rects[n];
            sr_fill(sr, mx + r.i0 * cell, my + r.j0 * cell, mx + r.i1 * cell, my + r.j1 * cell, &g_pal[r.key]);
        }
    }
    int cx = mx + (int)(dotX * cell);                  // player marker (moves over the map)
    int cy = my + (int)(dotZ * cell);
    sr_fill(sr, cx - 1, cy - 1, cx + cell + 1, cy + cell + 1, &MAP_ME);
    if (player && entity_getViewVector) {              // little white dot in the direction you look
        float v[3] = {0.f, 0.f, 0.f};
        entity_getViewVector(v, player, 1.0f);
        float len = __builtin_sqrtf(v[0] * v[0] + v[2] * v[2]);
        if (len > 0.2f) {
            int fx = cx + (int)(v[0] / len * (3.5f * cell)), fz = cy + (int)(v[2] / len * (3.5f * cell));
            sr_fill(sr, fx, fz, fx + 2, fz + 2, &C_OFF);
        }
    }

    // compass letters on the edges
    text(SL_MAPN,     sr, font, "N", mx + S / 2 - 2, my + 1,          &C_YELLOW);
    text(SL_MAPC + 0, sr, font, "E", mx + S - 8,     my + S / 2 - 4,  &C_YELLOW);
    text(SL_MAPC + 1, sr, font, "S", mx + S / 2 - 2, my + S - 9,      &C_YELLOW);
    text(SL_MAPC + 2, sr, font, "W", mx + 2,         my + S / 2 - 4,  &C_YELLOW);
    if (g_mapStatus != 0 && !g_pubValid) {             // tell why the map is empty
        static const char* ST[5] = {"", "LOADING", "NO DATA", "NO SYM", "NO REGION"};
        text(SL_MAPS, sr, font, ST[g_mapStatus], mx + 14, my + S / 2 + 8, &C_RED);
    }
}

// ---------------- saved settings ----------------
// Button layout and the simple on/off flags are saved in the app's data folder.
static char g_cfgPath[300];
static char g_guardPath[300];                       // exists while the game's commands are being enabled (crash guard)

static bool persistFlag(int i) {
    return i == B_FPS || i == B_ARMOR || i == B_NOHAND || i == B_NOCLOUD || i == B_NOSTAR ||
           i == B_CLEAR || i == B_OUTLINE || i == B_AUTOSPR || i == B_POS || i == B_MAP ||
           i == B_KEEPINV || i == B_NOSOUL || i == B_CMDS || i == B_NOHURT || i == B_NAMES;
}

static void cfgInitPath() {
    char name[200] = {0};
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) { size_t n = fread(name, 1, sizeof name - 1, f); name[n] = 0; fclose(f); }
    char* colon = strchr(name, ':');
    if (colon) *colon = 0;
    if (!name[0]) return;
    snprintf(g_cfgPath, sizeof g_cfgPath, "/data/data/%s/lp_buttons.cfg", name);
    snprintf(g_guardPath, sizeof g_guardPath, "/data/data/%s/lp_cmd.guard", name);
}

static void cfgSave() {
    if (!g_cfgPath[0]) return;
    FILE* f = fopen(g_cfgPath, "w");
    if (!f) return;
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
            if (fscanf(f, "%d", &z) != 1) break;                 // old files: ignored
        } else if (kind == 'b') {
            int i, on, x, y, w, h;
            if (fscanf(f, "%d %d %d %d %d %d", &i, &on, &x, &y, &w, &h) == 6 && i >= 0 && i < B_COUNT) {
                g_btnOn[i] = isButtonFeature(i) && on != 0; g_btnX[i] = x; g_btnY[i] = y; g_btnW[i] = w; g_btnH[i] = h;
            }
        } else if (kind == 'f') {
            int i, v;
            if (fscanf(f, "%d %d", &i, &v) == 2 && i >= 0 && i < B_COUNT && persistFlag(i)) g_on[i] = v != 0;
        } else break;
    }
    fclose(f);
}

// ---------------- Replay ----------------
// Records your position and rotation 20 times a second (only while the Replay button is switched on).
// Tapping the REPLAY button plays the last minute back: a ghost of your character walks the recorded path
// and the camera orbits it. Your real character is not touched (it stands still and is not drawn).
static const Color RP_BAR = {0.10f, 0.75f, 0.90f, 0.90f};

static void replayRecord(long long now) {
    if (g_replay || !g_localPlayer || !entity_getPos || !entity_getRotation) return;
    if (now - g_rpLastMs < 50) return;
    g_rpLastMs = now;
    const float* p = entity_getPos(g_localPlayer);
    if (!p) return;
    float r[2] = {0.f, 0.f};
    entity_getRotation(r, g_localPlayer);
    RpSample& s = g_rpRing[g_rpHead];
    s.x = p[0]; s.y = p[1]; s.z = p[2]; s.a = r[0]; s.b = r[1];
    g_rpHead = (g_rpHead + 1) % RP_MAX;
    if (g_rpCount < RP_MAX) g_rpCount++;
    static bool aBig = false, bBig = false;             // the pitch never leaves -90..90, the yaw does
    if (__builtin_fabsf(r[0]) > 95.f) aBig = true;
    if (__builtin_fabsf(r[1]) > 95.f) bBig = true;
    if (aBig && !bBig) g_yawIdx = 0; else if (bBig && !aBig) g_yawIdx = 1;
}

static float lerpAngle(float a, float b, float f) {
    float d = b - a;
    while (d > 180.f) d -= 360.f;
    while (d < -180.f) d += 360.f;
    return a + d * f;
}

static void replayEnter(void* client) {
    if (g_replay) return;
    if (g_rpCount < 40) { localMsg("LegendPro: not enough recorded yet - play a few seconds with the Replay button on"); return; }
    if (!g_localPlayer) return;
    void* opts = client_getOptions(client);
    if (g_freecam) setFreecam(false, client, opts);
    int n = g_rpCount;
    for (int i = 0; i < n; i++) g_rpPlay[i] = g_rpRing[(g_rpHead - n + i + RP_MAX) % RP_MAX];
    g_rpN = n;
    g_kfN = 0; g_rpCine = false; g_cinePlay = false; g_cineReq = 0;
    g_rpSavedPersp = opt_getPersp ? opt_getPersp(opts) : -1;
    if (opt_setPersp) opt_setPersp(opts, 1);              // third person (the camera orbits the ghost)
    g_rpT = 0.f; g_rpPlaying = false; g_rpSpeed = 1.f; g_rpOrbit = true;     // starts PAUSED: set the shots first, then PLAY/START
    g_rpHideUi = false; g_gwSpeed = 0.f; g_gwPos = 0.f;
    g_rpOrbitYaw = g_yawIdx ? g_rpPlay[0].b : g_rpPlay[0].a;
    g_ghostPos[0] = g_rpPlay[0].x; g_ghostPos[1] = g_rpPlay[0].y; g_ghostPos[2] = g_rpPlay[0].z;
    g_ghostRot[0] = g_rpPlay[0].a; g_ghostRot[1] = g_rpPlay[0].b;
    g_replay = true;
}

static void replayExit(void* client) {
    if (!g_replay) return;
    g_replay = false;
    g_rpCine = false; g_cinePlay = false;
    void* opts = client_getOptions(client);
    if (g_rpSavedPersp >= 0 && opt_setPersp) opt_setPersp(opts, g_rpSavedPersp);
}

// every frame while the replay runs (game thread): move the GHOST along the recording.
// Nothing about your real character changes, so this also works on servers.
static float catmull(float a, float b, float c, float d, float u) {
    return 0.5f * ((2.f * b) + (-a + c) * u + (2.f * a - 5.f * b + 4.f * c - d) * u * u + (-a + 3.f * b - 3.f * c + d) * u * u * u);
}

// camera for replay time t, from the shots (needs g_kfN >= 1)
static void cineEval(float t) {
    int n = g_kfN;
    if (n <= 0) return;
    const CineKf* k;
    if (n == 1 || t <= g_kf[0].t) k = &g_kf[0];
    else if (t >= g_kf[n - 1].t) k = &g_kf[n - 1];
    else k = nullptr;
    if (k) {
        g_cinePos[0] = k->x; g_cinePos[1] = k->y; g_cinePos[2] = k->z;
        g_cineRot[g_yawIdx] = k->yaw; g_cineRot[1 - g_yawIdx] = k->pitch;
        return;
    }
    int i = 0;
    while (i < n - 2 && t >= g_kf[i + 1].t) i++;
    const CineKf& p1 = g_kf[i];
    const CineKf& p2 = g_kf[i + 1];
    const CineKf& p0 = g_kf[i > 0 ? i - 1 : i];
    const CineKf& p3 = g_kf[i + 2 < n ? i + 2 : i + 1];
    float span = p2.t - p1.t;
    float u = span > 0.001f ? (t - p1.t) / span : 1.f;
    if (u < 0.f) u = 0.f;
    if (u > 1.f) u = 1.f;
    g_cinePos[0] = catmull(p0.x, p1.x, p2.x, p3.x, u);          // smooth curve through the shots
    g_cinePos[1] = catmull(p0.y, p1.y, p2.y, p3.y, u);
    g_cinePos[2] = catmull(p0.z, p1.z, p2.z, p3.z, u);
    g_cineRot[g_yawIdx] = lerpAngle(p1.yaw, p2.yaw, u);
    g_cineRot[1 - g_yawIdx] = p1.pitch + (p2.pitch - p1.pitch) * u;
}

static void cineAddShot() {
    CineKf k = {g_cinePos[0], g_cinePos[1], g_cinePos[2], g_cineRot[g_yawIdx], g_cineRot[1 - g_yawIdx], g_rpT};
    for (int i = 0; i < g_kfN; i++) {                            // a shot at (nearly) the same replay time is replaced
        if (__builtin_fabsf(g_kf[i].t - k.t) < 0.15f) { g_kf[i] = k; return; }
    }
    if (g_kfN >= CINE_MAX) { localMsg("LegendPro: max 8 shots - tap CLEAR to start again"); return; }
    int at = g_kfN;
    while (at > 0 && g_kf[at - 1].t > k.t) { g_kf[at] = g_kf[at - 1]; at--; }   // keep the shots sorted by time
    g_kf[at] = k;
    g_kfN++;
}

static bool cineEnter(void* opts) {                              // game thread
    if (!g_fcLook) installLookHook();
    if (!g_fcLook) { localMsg("LegendPro: free camera not available (look hook missing)"); return false; }
    // Start exactly where the orbit camera is right now (same direction, 4 blocks away from the ghost, same
    // downward angle), so the picture does not jump when FREE is switched on.
    const float pitch = 22.f;
    float yaw = g_rpOrbitYaw * 0.017453292f, pit = pitch * 0.017453292f;
    float cp = __builtin_cosf(pit);
    g_cinePos[0] = g_ghostPos[0] + __builtin_sinf(yaw) * cp * 4.f;
    g_cinePos[1] = g_ghostPos[1] + __builtin_sinf(pit) * 4.f;
    g_cinePos[2] = g_ghostPos[2] - __builtin_cosf(yaw) * cp * 4.f;
    g_cineRot[g_yawIdx] = g_rpOrbitYaw;
    g_cineRot[1 - g_yawIdx] = pitch;
    g_rpCine = true;
    g_cinePlay = false;
    if (opts && opt_setPersp) opt_setPersp(opts, 0);             // first person: the camera sits exactly at g_cinePos
    return true;
}

static void replayTick(float dt, void* client) {
    if (!g_replay || g_rpN < 2) return;
    void* opts = client ? client_getOptions(client) : nullptr;
    int req = __atomic_exchange_n(&g_cineReq, 0, __ATOMIC_ACQ_REL);
    if (req & CR_FREE) {
        if (!g_rpCine) cineEnter(opts);
        else { g_rpCine = false; g_cinePlay = false; if (opts && opt_setPersp) opt_setPersp(opts, 1); }
    }
    if (req & CR_CLEAR) { g_kfN = 0; g_cinePlay = false; }
    if (req & CR_ADD) {
        if (!g_rpCine) localMsg("LegendPro: tap FREE first, fly the camera, then +SHOT");
        else cineAddShot();
    }
    if (req & CR_START) {
        if (g_kfN < 1) localMsg("LegendPro: add at least 1 shot first (FREE, fly, +SHOT)");
        else if (g_rpCine || cineEnter(opts)) {
            g_cinePlay = true;
            g_rpT = g_kf[0].t;
            g_rpPlaying = true;
        }
    }
    if (g_rpCine && !g_cinePlay && client_getLocalPlayer && mob_isSneaking) {
        void* p = client_getLocalPlayer(client);
        if (p && mob_isSneaking(p)) g_cinePos[1] -= FC_VERT_SPEED * dt;      // sneak = camera down
    }
    float dur = (g_rpN - 1) * 0.05f;
    if (g_rpPlaying) { g_rpT += dt * g_rpSpeed; if (g_rpT >= dur) { g_rpT = dur; g_rpPlaying = false; } }
    if (g_rpT < 0.f) g_rpT = 0.f;
    if (g_rpT > dur) g_rpT = dur;
    float f = g_rpT / 0.05f;
    int i0 = (int)f;
    if (i0 > g_rpN - 2) i0 = g_rpN - 2;
    float fr = f - i0;
    if (fr > 1.f) fr = 1.f;
    const RpSample& A = g_rpPlay[i0];
    const RpSample& B = g_rpPlay[i0 + 1];
    g_ghostPos[0] = A.x + (B.x - A.x) * fr;
    g_ghostPos[1] = A.y + (B.y - A.y) * fr;
    g_ghostPos[2] = A.z + (B.z - A.z) * fr;
    int yi = g_yawIdx, pi = 1 - g_yawIdx;
    float aY = yi ? A.b : A.a, bY = yi ? B.b : B.a;
    float aP = pi ? A.b : A.a, bP = pi ? B.b : B.a;
    g_ghostRot[yi] = lerpAngle(aY, bY, fr);
    g_ghostRot[pi] = aP + (bP - aP) * fr;
    {   // ghost walk cycle: same rule as Mob::updateWalkAnim (speed follows 4 x distance per tick, position adds speed)
        float ticks = dt * 20.f * g_rpSpeed;
        float dx = B.x - A.x, dz = B.z - A.z;
        float target = g_rpPlaying ? __builtin_sqrtf(dx * dx + dz * dz) * 4.f : 0.f;
        if (target > 1.f) target = 1.f;
        float k = 0.4f * ticks;
        if (k > 1.f) k = 1.f;
        g_gwSpeed += (target - g_gwSpeed) * k;
        if (g_rpPlaying) g_gwPos += g_gwSpeed * ticks;
    }
    if (g_rpOrbit) g_rpOrbitYaw += 30.f * dt;             // the camera circles around the ghost
    if (g_cinePlay && g_rpCine) {
        cineEval(g_rpT);
        if (g_rpT >= g_kf[g_kfN - 1].t - 0.001f) g_cinePlay = false;       // last shot reached: camera stays there
    }
}

static bool rpBtnRect(int k, int* x, int* y, int* w, int* h) {
    if (k < 0 || k > 6) return false;
    *x = (int)g_guiW / 2 - 120 + k * 40; *y = (int)g_guiH - 42; *w = 38; *h = 14;
    return true;
}
static bool rpBtn2Rect(int k, int* x, int* y, int* w, int* h) {      // second row: the cinematic camera buttons
    if (k < 0 || k > 3) return false;
    *x = (int)g_guiW / 2 - 120 + k * 40; *y = (int)g_guiH - 82; *w = 38; *h = 14;
    return true;
}
static void rpBarRect(int* x, int* y, int* w, int* h) {
    *x = (int)g_guiW / 2 - 120; *y = (int)g_guiH - 56; *w = 238; *h = 8;
}

static const Color C_DIM = {1.f, 1.f, 1.f, 0.18f};
static void replayDraw(void* sr, void* font) {
    if (!g_replay) return;
    float dur = (g_rpN - 1) * 0.05f;
    char b[64];
    if (g_rpHideUi) {                                   // clean footage: only a tiny, almost invisible square stays
        int hx, hy, hw, hh;
        rpBtnRect(6, &hx, &hy, &hw, &hh);
        sr_fill(sr, hx + hw - 8, hy + hh - 8, hx + hw, hy + hh, &C_DIM);
        return;
    }
    static const char* SPD[3] = {"0.5X", "1X", "2X"};
    const char* spd = g_rpSpeed < 0.9f ? SPD[0] : (g_rpSpeed > 1.1f ? SPD[2] : SPD[1]);
    snprintf(b, sizeof b, "REPLAY %s %s", g_rpOrbit ? "ORBIT" : "FOLLOW", spd);
    text(SL_RP + 0, sr, font, b, (int)g_guiW / 2 - 40, 6, &C_CYAN);

    int x, y, w, h;
    rpBarRect(&x, &y, &w, &h);
    sr_fill(sr, x - 1, y - 1, x + w + 1, y + h + 1, &C_BG);
    int fillW = dur > 0.f ? (int)(w * (g_rpT / dur)) : 0;
    if (fillW > 0) sr_fill(sr, x, y, x + fillW, y + h, &RP_BAR);
    int t = (int)g_rpT, d = (int)dur;
    snprintf(b, sizeof b, "%02d:%02d / %02d:%02d", t / 60, t % 60, d / 60, d % 60);
    text(SL_RP + 1, sr, font, b, x, y - 10, &C_OFF, 200);

    // cinematic camera row
    for (int k = 0; k < 4; k++) {
        rpBtn2Rect(k, &x, &y, &w, &h);
        sr_fill(sr, x, y, x + w, y + h, &C_BG);
        const char* label = "";
        const Color* c = &C_OFF;
        switch (k) {
        case 0: label = "FREE"; c = g_rpCine ? &C_ON : &C_OFF; break;
        case 1: label = "+SHOT"; c = g_rpCine ? &C_CYAN : &C_OFF; break;
        case 2: label = "CLEAR"; break;
        default: label = g_cinePlay ? "STOP" : "START"; c = g_cinePlay ? &C_RED : &C_ON; break;
        }
        text(SL_CINE + k, sr, font, label, x + (w - (int)strlen(label) * 6) / 2, y + 3, c);
    }
    rpBtn2Rect(0, &x, &y, &w, &h);
    if (g_rpCine) snprintf(b, sizeof b, "SHOTS %d/%d %s %d %d %d", g_kfN, CINE_MAX, g_cinePlay ? "PLAY" : "CAM",
                           (int)g_cinePos[0], (int)g_cinePos[1], (int)g_cinePos[2]);
    else snprintf(b, sizeof b, "SHOTS %d/%d", g_kfN, CINE_MAX);
    text(SL_CINE + 4, sr, font, b, x, y - 10, &C_YELLOW, 200);

    for (int k = 0; k < 7; k++) {
        rpBtnRect(k, &x, &y, &w, &h);
        sr_fill(sr, x, y, x + w, y + h, &C_BG);
        const char* label = "";
        const Color* c = &C_OFF;
        switch (k) {
        case 6: label = "HIDE"; c = &C_YELLOW; break;
        case 0: label = "-5S"; break;
        case 1: label = g_rpPlaying ? "PAUSE" : "PLAY"; break;
        case 2: label = "+5S"; break;
        case 3: label = spd; break;
        case 4: label = "ORBIT"; c = g_rpOrbit ? &C_ON : &C_OFF; break;
        default: label = "EXIT"; c = &C_RED; break;
        }
        text(k == 6 ? SL_CINE + 5 : SL_RP + 2 + k, sr, font, label, x + (w - (int)strlen(label) * 6) / 2, y + 3, c);
    }
}

// a tap while the replay runs; returns true when it hit the replay controls
static bool replayTouch(float gx, float gy) {
    if (!g_replay) return false;
    float dur = (g_rpN - 1) * 0.05f;
    int x, y, w, h;
    if (g_rpHideUi) {                                   // hidden: only the tiny square answers
        rpBtnRect(6, &x, &y, &w, &h);
        if (gx >= x + w - 12 && gx < x + w + 2 && gy >= y + h - 12 && gy < y + h + 2) { g_rpHideUi = false; return true; }
        return false;
    }
    rpBarRect(&x, &y, &w, &h);
    if (gx >= x && gx < x + w && gy >= y - 6 && gy < y + h + 8) {            // tap the bar = jump there
        g_rpT = dur * (gx - x) / (float)w;
        return true;
    }
    for (int k = 0; k < 4; k++) {                                          // cinematic camera row
        if (!rpBtn2Rect(k, &x, &y, &w, &h) || gx < x || gx >= x + w || gy < y || gy >= y + h) continue;
        if (k == 3 && g_cinePlay) g_cinePlay = false;                       // STOP
        else __atomic_fetch_or(&g_cineReq, k == 0 ? CR_FREE : (k == 1 ? CR_ADD : (k == 2 ? CR_CLEAR : CR_START)), __ATOMIC_ACQ_REL);
        return true;
    }
    for (int k = 0; k < 7; k++) {
        if (!rpBtnRect(k, &x, &y, &w, &h) || gx < x || gx >= x + w || gy < y || gy >= y + h) continue;
        switch (k) {
        case 6: g_rpHideUi = true; break;
        case 0: g_rpT -= 5.f; if (g_rpT < 0.f) g_rpT = 0.f; break;
        case 1:
            if (!g_rpPlaying && g_rpT >= dur - 0.01f) g_rpT = 0.f;          // at the end: play again from the start
            g_rpPlaying = !g_rpPlaying;
            break;
        case 2: g_rpT += 5.f; if (g_rpT > dur) g_rpT = dur; break;
        case 3: g_rpSpeed = g_rpSpeed < 0.9f ? 1.f : (g_rpSpeed < 1.1f ? 2.f : 0.5f); break;
        case 4: g_rpOrbit = !g_rpOrbit; break;
        default: __atomic_fetch_or(&g_pending, 1u << B_REPLAY, __ATOMIC_ACQ_REL); break;   // EXIT: done on the game thread
        }
        return true;
    }
    return false;
}

// Clock button: in-game time + day (tap = switch to the real clock and date of the phone)
static void clockText(char* buf, size_t n) {
    if (g_on[B_CLOCK]) {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        snprintf(buf, n, "%02d:%02d %02d/%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_mon + 1, tmv.tm_mday);
        return;
    }
    void* level = (g_localPlayer && entity_getLevel) ? entity_getLevel(g_localPlayer) : nullptr;
    if (!level || !level_getTime) { snprintf(buf, n, "--:-- DAY -"); return; }
    int t = level_getTime(level);
    if (t < 0) t = 0;
    snprintf(buf, n, "%02d:%02d DAY %d", (t / 1000 + 6) % 24, (t % 1000) * 60 / 1000, t / 24000 + 1);
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
    // minimap (movable / resizable in edit mode)
    int mx = MAP_DEF_X, my = MAP_DEF_Y, mw = 0, mh = 0;
    if (g_on[B_MAP]) {
        inGameRect(B_MAP, &mx, &my, &mw, &mh);
        if (g_editMode)
            sr_fill(sr, mx - 4, my - 4, mx + mw + 4, my + mh + 4, g_editSel == B_MAP ? &C_YELLOW : &C_OFF);
        drawMap(sr, font, client, mx, my, mapCell());
    }

    // info text: right of the map
    int tx = g_on[B_MAP] ? mx + mw + 6 : 4;
    int ty = g_on[B_MAP] ? my + 2 : 4;


    // welcome banner for the first 12 seconds after the HUD first appears
    static long long hudStart = 0;
    long long now = nowMs();
    if (!hudStart) hudStart = now;
    if (now - hudStart < 12000) {
        text(SL_BANNER + 0, sr, font, "LegendPro Client",           tx, ty, &C_CYAN);   ty += 10;
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
        if (i == B_MAP) continue;                       // drawn above
        int x, y, w, h;
        if (!inGameRect(i, &x, &y, &w, &h)) continue;
        if (g_editMode)
            sr_fill(sr, x - 1, y - 1, x + w + 1, y + h + 1, i == g_editSel ? &C_YELLOW : &C_OFF);
        bool action = (i == B_VIEW || i == B_Q || i == B_CLOCK);
        sr_fill(sr, x, y, x + w, y + h, (!action && g_on[i]) ? &C_BTN_ON : &C_BTN_BG);
        const char* label = SHORTL[i];
        char clk[32];
        if (i == B_CLOCK) { clockText(clk, sizeof clk); label = clk; }
        else if (i == B_ZOOM) { snprintf(clk, sizeof clk, g_zoomLevel ? "ZOOM %d" : "ZOOM", g_zoomLevel); label = clk; }
        int tw = (int)strlen(label) * 6;
        text(SL_GBTN + i, sr, font, label, x + (w - tw) / 2, y + (h - 8) / 2 + 1, &C_OFF, i == B_CLOCK ? 500 : 0);
    }

    replayDraw(sr, font);

    // edit toolbar
    if (g_editMode) {
        int ex, ey, ew, eh;
        tbRect(0, &ex, &ey, &ew, &eh);
        text(SL_EDIT, sr, font, "EDIT: drag a button or the map, then SIZE", ex - 12, ey - 10, &C_YELLOW);
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
// 4th tab ("LP") under them; when it is selected a list of switches covers
// the content area, like a normal settings page: one switch per row.
//  - BUTTONS rows: switch on = a button for it appears on the screen
//  - HUD rows: switch on = shown;  ALWAYS ON rows cannot be switched off
static const int OPT_TAB_X = 5, OPT_TAB_Y = 176, OPT_TAB_W = 28, OPT_TAB_H = 27;
static const int OPT_CX0 = 38, OPT_CY0 = 27, OPT_CY1 = 225;      // content area (GUI units)
static const int OPT_ROW0 = 44, OPT_ROW_H = 14, OPT_PER_COL = 10, OPT_COLS = 3;
static const Color OPT_BG   = {0.07f, 0.07f, 0.08f, 1.f};
static const Color OPT_ROW  = {0.17f, 0.17f, 0.18f, 1.f};
static const Color OPT_SWON = {0.15f, 0.60f, 0.20f, 1.f};
static const Color OPT_SWOF = {0.38f, 0.38f, 0.38f, 1.f};
static const Color OPT_WHITE_FRAME = {1.f, 1.f, 1.f, 1.f};
static const Color OPT_GREY_FRAME  = {0.45f, 0.45f, 0.45f, 1.f};

struct OptRow { int kind; int idx; const char* label; };   // kind: 0 header, 1 button switch, 2 display switch, 3 reset, 4 always on
static const OptRow OPT_TABLE[] = {
    {0, -1, "BUTTONS"},
    {1, B_VIEW,    "F5 Button"},
    {1, B_FREECAM, "Cam Button"},
    {1, B_Q,       "Drop Button"},
    {1, B_HUD,     "HUD Button"},
    {1, B_EDIT,    "Edit Button"},
    {1, B_CLOCK,   "Clock (time/day)"},
    {1, B_ZOOM,    "Zoom Button"},
    {1, B_REPLAY,  "Replay Button"},
    {1, B_LIGHT,   "Full Bright"},
    {1, B_LEFT,    "Left Hand"},
    {1, B_NOHAND,  "Hide Hand"},
    {1, B_NOCLOUD, "No Clouds"},
    {1, B_NOSTAR,  "No Stars"},
    {1, B_CLEAR,   "Clear Weather"},
    {1, B_OUTLINE, "Outline Selection"},
    {1, B_AUTOSPR, "Auto Sprint"},
    {1, B_NOBOB,   "No View Bobbing"},
    {1, B_SMOOTH,  "Smooth Camera"},
    {1, B_NOPART,  "No Particles"},
    {1, B_NOHURT,  "No Hurt Cam"},
    {3, -1,        "Reset Button Layout"},
    {0, -1, "HUD"},
    {2, B_MAP,     "Minimap"},
    {2, B_FPS,     "FPS Counter"},
    {2, B_POS,     "XYZ Position"},
    {2, B_ARMOR,   "Armor Durability"},
    {2, B_NAMES,   "Player Names"},
    {0, -1, "COMMANDS"},
    {4, B_CMDS,    "Commands"},
};
static const int OPT_ROW_COUNT = (int)(sizeof(OPT_TABLE) / sizeof(OPT_TABLE[0]));

static bool optRowRect(int r, int* x0, int* y0, int* x1, int* y1) {
    if (r < 0 || r >= OPT_ROW_COUNT) return false;
    float colW = (g_optW - OPT_CX0 - 12.f) / (float)OPT_COLS;
    int col = r / OPT_PER_COL, row = r % OPT_PER_COL;
    *x0 = OPT_CX0 + 6 + (int)(col * colW);
    *x1 = *x0 + (int)colW - 4;
    *y0 = OPT_ROW0 + row * OPT_ROW_H;
    *y1 = *y0 + OPT_ROW_H - 2;
    return true;
}

// hit codes: -2 = our tab, >= 0 = a switch row (index into OPT_TABLE), -3 = covered area, -1 = none
static int optHit(float gx, float gy) {
    if (gx >= OPT_TAB_X && gx < OPT_TAB_X + OPT_TAB_W && gy >= OPT_TAB_Y && gy < OPT_TAB_Y + OPT_TAB_H) return -2;
    if (!g_optPage) return -1;
    for (int r = 0; r < OPT_ROW_COUNT; r++) {
        int x0, y0, x1, y1;
        if (OPT_TABLE[r].kind == 0 || OPT_TABLE[r].kind == 4 || !optRowRect(r, &x0, &y0, &x1, &y1)) continue;
        if (gx >= x0 && gx < x1 && gy >= y0 && gy < y1) return r;               // the whole row is the switch
    }
    if (gx >= OPT_CX0 && gy >= OPT_CY0 && gy < OPT_CY1) return -3;
    return -1;
}

static void optStateText(const OptRow& R, char* buf, size_t n, bool* on) {
    switch (R.kind) {
    case 1:  *on = g_btnOn[R.idx]; snprintf(buf, n, *on ? "ON" : "OFF"); break;
    case 3:  *on = false; snprintf(buf, n, "TAP"); break;
    case 4:  *on = true;  snprintf(buf, n, "ON"); break;
    default:
        *on = g_on[R.idx];
        if (R.idx == B_FOV) {
            if (g_fovStep) snprintf(buf, n, "%d", (int)FOVS[g_fovStep - 1]); else snprintf(buf, n, "OFF");
            *on = g_fovStep != 0;
        } else snprintf(buf, n, *on ? "ON" : "OFF");
        break;
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
    text(SL_OPTHDR,  sr, font, "LegendPro Client", OPT_CX0 + 8, OPT_CY0 + 6, &C_CYAN);
    text(SL_OPTHDR2, sr, font, "Created by LegendPro", OPT_CX0 + 8 + 110, OPT_CY0 + 6, &C_ON);
    for (int r = 0; r < OPT_ROW_COUNT; r++) {
        int x0, y0, x1, y1;
        if (!optRowRect(r, &x0, &y0, &x1, &y1)) continue;
        const OptRow& R = OPT_TABLE[r];
        if (R.kind == 0) {                                   // section header
            text(SL_OPTLBL + r, sr, font, R.label, x0 + 2, y0 + 3, &C_YELLOW);
            continue;
        }
        sr_fill(sr, x0, y0, x1, y1, &OPT_ROW);
        char buf[16];
        bool on;
        optStateText(R, buf, sizeof buf, &on);
        bool lit = R.kind == 1 ? (g_on[R.idx] && R.idx != B_VIEW && R.idx != B_Q) : on;
        text(SL_OPTLBL + r, sr, font, R.label, x0 + 4, y0 + 2, lit ? &C_ON : &C_OFF);
        sr_fill(sr, x1 - 34, y0 + 1, x1 - 3, y1 - 1, on ? &OPT_SWON : &OPT_SWOF);
        text(SL_OPTST + r, sr, font, buf, x1 - 31, y0 + 2, &C_OFF);
    }
}

// ============================ actions (game thread) ============================
static void setFreecam(bool on, void* client, void* opts) {
    if (on) {
        if (!g_freecamReady || !client_getLocalPlayer) return;
        g_player = client_getLocalPlayer(client);
        if (!g_player) return;
        g_camOff = {0.f, 0.f, 0.f};
        installLookHook();
        g_fcRot[0] = g_fcRot[1] = 0.f;
        if (entity_getRotation) entity_getRotation(g_fcRot, g_player);   // start looking where the player looks
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
    case B_ZOOM:                                          // camera zoom: 3 steps, then back to normal
        if (!opt_setFov || !opt_getFov) break;
        if (g_zoomLevel == 0) g_zoomOrigFov = opt_getFov(opts);
        g_zoomLevel = (g_zoomLevel + 1) % 4;
        g_on[i] = g_zoomLevel != 0;
        opt_setFov(opts, g_zoomLevel ? ZOOM_FOV[g_zoomLevel - 1] : (g_zoomOrigFov > 0.f ? g_zoomOrigFov : 70.f));
        break;
    case B_REPLAY:                                        // start the replay / exit it
        if (g_replay) replayExit(client); else replayEnter(client);
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
    case B_Q: {                                           // drop ONE item of the held stack
        void* p = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;
        if (!p) break;
        // Best: the game's own Drop button. It goes through FillingContainer::dropSlot -> LocalPlayer::drop, which
        // tells the server, so the item can be picked up by everybody. (Player::drop alone only made a local ghost item.)
        if (client_handleDrop) { client_handleDrop(client); break; }
        void* sel = player_getSelectedItem ? player_getSelectedItem(p) : nullptr;
        if (sel && item_getId && item_getAux && item_ctor && player_drop && item_remove) {
            if (item_isNull && item_isNull(sel)) break;                      // empty hand
            int id = item_getId(sel), aux = item_getAux(sel);
            if (id > 0) {
                alignas(8) unsigned char one[128];
                memset(one, 0, sizeof one);
                item_ctor(one, id, 1, aux);
                player_drop(p, one, false);                                   // the single item falls in front of you
                item_remove(sel, 1);                                          // and leaves the stack
            }
        } else if (mob_dropHeldItem) {
            mob_dropHeldItem(p);                                              // fallback: the old behaviour
        }
        break;
    }
    case B_HUD:                                           // F1: hide / show the game HUD
        if (opt_setHideGui && opt_getHideGui) opt_setHideGui(opts, !opt_getHideGui(opts));
        break;
    case B_FREECAM:
        setFreecam(!g_freecam, client, opts);
        break;
    case B_EDIT:                                          // edit mode: drag / resize the in-game buttons
        g_editMode = !g_editMode;
        g_editSel = -1;
        g_on[B_EDIT] = g_editMode;
    g_on[B_REPLAY] = g_replay;
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
    g_localPlayer = client_getLocalPlayer ? client_getLocalPlayer(client) : nullptr;

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

    if (g_btnOn[B_REPLAY]) replayRecord(now);                       // record only while the Replay button is on
    replayTick(dt, client);

    unsigned m2 = __atomic_exchange_n(&g_pending2, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; i < B_COUNT; i++)
        if ((m2 & (1u << i)) && isButtonFeature(i)) { g_btnOn[i] = !g_btnOn[i]; g_cfgDirty = true; }

    unsigned m = __atomic_exchange_n(&g_pending, 0u, __ATOMIC_ACQ_REL);
    for (int i = 0; m; i++, m >>= 1)
        if (m & 1) runAction(i, client);

    static long long cfgMs = 0;
    if (g_cfgDirty && now - cfgMs > 1000) { cfgMs = now; g_cfgDirty = false; cfgSave(); }

    g_on[B_EDIT] = g_editMode;
    g_on[B_KEEPINV] = true;                      // always on
    g_on[B_CMDS] = true;

    static int queueShown = 0;
    if (g_msgQueued > 0) {                                          // print queued answer lines (outside addMessage)
        if (queueShown < g_msgQueued) localMsg(g_msgQueue[queueShown++]);
        if (queueShown >= g_msgQueued) { g_msgQueued = 0; queueShown = 0; }
    } else queueShown = 0;

    static long long nameMs = 0;
    static bool namesWereOn = false;
    if (g_localPlayer && now - nameMs > 2000) {                      // Player Names: your own real name as name tag
        nameMs = now;
        if (g_on[B_NAMES]) {
            void* o = client_getOptions(client);
            const void* un = o ? getOwnName(o) : nullptr;
            if (un && entity_setNameTag) {
                entity_setNameTag(g_localPlayer, un);
                if (entity_setNameTagVisible) entity_setNameTagVisible(g_localPlayer, true);
            }
            namesWereOn = true;
        } else if (namesWereOn) {
            namesWereOn = false;
            if (entity_setNameTagVisible) entity_setNameTagVisible(g_localPlayer, false);
            if (entity_setNameTag) { const void* e = makeStr(""); if (e) entity_setNameTag(g_localPlayer, e); }
        }
    }

    static void* lastPlayer = nullptr;
    static int lastPlayerFrame = 0;
    if (g_localPlayer != lastPlayer) {                              // new world
        lastPlayer = g_localPlayer; g_timeLock = -1; lastPlayerFrame = g_frame;
        g_replay = false; g_rpCount = 0; g_rpHead = 0;              // unlock time, forget the old recording
    }
    if (g_localPlayer && g_nativeTriedFor != g_localPlayer && !g_nativeCmdDisabled &&
        g_frame - lastPlayerFrame > 180 && isLocalWorld()) {       // 3 s after the world appeared
        g_nativeTriedFor = g_localPlayer;
        registerNativeCommands(client);
    }
    if (g_timeLock >= 0 && g_localPlayer && entity_getLevel && level_setTime) {
        void* lv = entity_getLevel(g_localPlayer);
        if (lv) level_setTime(lv, g_timeLock);
    }

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
            fcClamp();
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
            if (hit >= 0) {
                const OptRow& R = OPT_TABLE[hit];
                if (R.kind == 1)      __atomic_fetch_or(&g_pending2, 1u << R.idx, __ATOMIC_ACQ_REL);   // show / hide the button
                else if (R.kind == 2) __atomic_fetch_or(&g_pending, 1u << R.idx, __ATOMIC_ACQ_REL);   // feature on / off
                else if (R.kind == 3) __atomic_fetch_or(&g_pending, 1u << R_RESET, __ATOMIC_ACQ_REL); // reset layout
            }
            if (hit >= 0 || hit == -3) { g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1; return; }
            g_optPage = false;                             // another tab / Back: close our page, game handles the tap
        }
    }
    if (optVisible) { orig_feed(b, down, x, y, id); return; }   // no HUD buttons on the Options screen

    bool hudShown = g_frame - g_lastHudFrame <= 2;
    if (g_hudHidden && !hudShown && press) {
        if (gx < 92 && gy < 92) {                                  // HUD hidden: tap the top-left corner to bring it back
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            __atomic_fetch_or(&g_pending, 1u << B_HUD, __ATOMIC_ACQ_REL);
            return;
        }
    }
    if (hudShown && press) {
        if (replayTouch(gx, gy)) {                                  // replay controls
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            return;
        }
        if (g_editMode) {
            for (int k = 0; k < 4; k++) {                           // edit toolbar
                int tx, ty, tw, th;
                if (!tbRect(k, &tx, &ty, &tw, &th) || gx < tx || gx >= tx + tw || gy < ty || gy >= ty + th) continue;
                int i = g_editSel;
                if (k == 0 || k == 1) {
                    int bx, by, bw, bh;
                    if (i >= 0 && i < B_COUNT && inGameRect(i, &bx, &by, &bw, &bh)) {
                        int d = k == 1 ? 1 : -1;
                        if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; }    // keep it where it is
                        if (i == B_MAP) {                                         // map: cell size 1..3
                            int c = mapCell() + d;
                            g_btnW[B_MAP] = c < 1 ? 1 : (c > 3 ? 3 : c);
                        } else {
                            bw += d * 4; bh += d * 2;
                            g_btnW[i] = bw < 20 ? 20 : (bw > 90 ? 90 : bw);
                            g_btnH[i] = bh < 10 ? 10 : (bh > 40 ? 40 : bh);
                        }
                        g_cfgDirty = true;
                    }
                } else if (k == 2) {
                    if (i >= 0 && i < B_COUNT) { g_btnX[i] = -1; g_btnY[i] = -1; g_btnW[i] = 0; g_btnH[i] = 0; g_cfgDirty = true; }
                } else {
                    g_editMode = false; g_editSel = -1; g_on[B_EDIT] = false; g_cfgDirty = true;
                }
                g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
                return;
            }
        }
        for (int i = 0; i < B_COUNT; i++) {                         // in-game buttons (and the map in edit mode)
            int bx, by, bw, bh;
            if (i == B_MAP && !g_editMode) continue;                 // a normal tap on the map does nothing
            if (!inGameRect(i, &bx, &by, &bw, &bh)) continue;
            if (gx < bx || gx >= bx + bw || gy < by || gy >= by + bh) continue;
            g_cap[s] = true; g_capFrame[s] = g_frame;
            if (g_editMode) {                                       // select and start dragging
                g_editSel = i;
                if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; if (i != B_MAP) { g_btnW[i] = bw; g_btnH[i] = bh; } }
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

// ---- Keep Inventory / No Soulsand Slow ----
static void (*orig_dropGear)(void*, int) = nullptr;
static void my_dropGear(void* self, int a) {
    if (g_on[B_KEEPINV] && self == g_localPlayer) return;          // keep the items when you die
    orig_dropGear(self, a);
}

static void (*orig_invDropAll)(void*, bool) = nullptr;
static void my_invDropAll(void* self, bool b) {
    if (g_on[B_KEEPINV]) return;
    orig_invDropAll(self, b);
}

static void (*orig_soul)(void*, void*, const void*, void*) = nullptr;
static void my_soul(void* self, void* region, const void* pos, void* ent) {
    if (g_on[B_NOSOUL]) return;                                    // soulsand does not slow you down
    orig_soul(self, region, pos, ent);
}

// ---- chat commands: only in a local (single-player) world, never on a server ----
// The game itself runs lines that start with '/' through its own command parser, but only in a
// local world (GuiData::addMessage checks that). The game already contains the whole list of
// commands (/timeset, /give, /gamemode, /summon ...) - they are just never switched on in this
// build. We switch them on once per world, and add a few of our own as a fallback.
static char g_say[8][200];
static int g_nsay = 0;
static void say(const char* msg) {                                  // collect the answer lines of one command
    if (g_nsay < 8) { strncpy(g_say[g_nsay], msg, 199); g_say[g_nsay][199] = 0; g_nsay++; }
}

static void localMsg(const char* msg) {                             // print one extra line in the chat
    static const void* empty = nullptr;
    if (!empty) empty = makeStr("");
    const void* m = makeStr(msg);
    if (client_sendLocalMessage && g_client && empty && m) client_sendLocalMessage(g_client, empty, m);
}

// A world counts as local only when every available signal says so (the game uses the same test).
static bool isLocalWorld() {
    if (!client_getServer || !g_client) return false;
    void* mc = client_getServer(g_client);
    if (!mc) return false;
    if (!mc_isOnlineClient && !client_getConnectedServer) return false;     // cannot tell: treat as server
    if (mc_isOnlineClient && mc_isOnlineClient(mc)) return false;
    if (client_getConnectedServer) {
        const void* ref = client_getConnectedServer(g_client);
        const char* name = ref ? *(const char* const*)ref : nullptr;
        if (name && name[0]) return false;
    }
    return true;
}

// Switch on the game's own commands. A guard file is written first; if the game ever dies in here
// the next start sees it and skips this step (our own fallback commands keep working).
static void registerNativeCommands(void* client) {
    if (!client_getServer || !mc_getCommandParser || !g_localPlayer) return;
    void* mc = client_getServer(client);
    if (!mc) return;
    void* parser = mc_getCommandParser(mc);
    void* level = entity_getLevel ? entity_getLevel(g_localPlayer) : nullptr;
    void* opts = client_getOptions(client);
    if (!parser || !level || !opts) return;

    if (g_guardPath[0]) { FILE* f = fopen(g_guardPath, "w"); if (f) { fputs("enabling commands\n", f); fclose(f); } }
    if (parser_clear) parser_clear(parser);                         // forget commands that point at an old world
    if (cmds_setupServer) cmds_setupServer(parser, level);
    if (cmds_setupDebug) cmds_setupDebug(parser, opts);
    if (cmds_setupProfiling) cmds_setupProfiling(parser, opts);
    void* gm = mc_getGameMode ? mc_getGameMode(mc) : nullptr;
    void* arch = client_getLevelArchiver ? client_getLevelArchiver(client) : nullptr;
    void* tex = client_getTextures ? client_getTextures(client) : nullptr;
    if (cmds_setupStandard && gm && arch && tex) cmds_setupStandard(parser, g_localPlayer, tex, arch, gm);
    if (g_guardPath[0]) remove(g_guardPath);
    LOG("game commands enabled");
}

// returns true when the line was one of our own commands
static bool runCommand(void* self, const char* line) {
    char buf[160];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char* argv[8];
    int argc = 0;
    for (char* p = strtok(buf, " "); p && argc < 8; p = strtok(nullptr, " ")) argv[argc++] = p;
    if (argc == 0) return false;
    const char* cmd = argv[0][0] == '/' ? argv[0] + 1 : argv[0];    // the leading '/' may or may not be there
    char out[120];

    if (!strcmp(cmd, "help")) {
        say("LegendPro: /timeset /timeadd /timelock /timestop /tickset /setworldspawn /spawning /give /clearInventory");
        say("LegendPro: /player /health /hunger /gethealth /getpos /gamemode /effect /enchant /disenchant /xp");
        say("LegendPro: /rain /lightning /summon /wipeout /longpick /screenshot /export /import /setskin");
        say("LegendPro: /aigoals /bounds /paths /villages /profile    mine: /tp x y z  /jmp n  /heal  /lvl N  /timestart");
        return true;
    }
    if (!strcmp(cmd, "getpos")) {
        const float* p = entity_getPos ? entity_getPos(self) : nullptr;
        if (!p) return false;
        snprintf(out, sizeof out, "LegendPro: x=%.1f y=%.1f z=%.1f", p[0], p[1], p[2]);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "tp")) {
        if (!entity_setPos) return false;
        if (argc < 4) { say("LegendPro: usage /tp x y z"); return true; }
        float v[3] = {(float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3])};
        entity_setPos(self, v);
        snprintf(out, sizeof out, "LegendPro: teleported to %.1f %.1f %.1f", v[0], v[1], v[2]);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "gamemode") || !strcmp(cmd, "gm")) {
        if (!player_setGameType) return false;
        int mode = -1;
        if (argc >= 2) {
            const char* a = argv[1];
            if (!strcmp(a, "0") || !strcmp(a, "s") || !strcmp(a, "survival")) mode = 0;
            else if (!strcmp(a, "1") || !strcmp(a, "c") || !strcmp(a, "creative")) mode = 1;
        }
        if (mode < 0) { say("LegendPro: usage /gamemode 0|1"); return true; }
        player_setGameType(self, mode);
        say(mode ? "LegendPro: creative mode" : "LegendPro: survival mode");
        return true;
    }
    if (!strcmp(cmd, "time")) {
        void* level = entity_getLevel ? entity_getLevel(self) : nullptr;
        if (!level_setTime || !level) return false;
        if (argc < 3 || strcmp(argv[1], "set") != 0) { say("LegendPro: usage /time set day|night|noon|midnight|N"); return true; }
        int t = -1;
        const char* a = argv[2];
        if (!strcmp(a, "day")) t = 1000;
        else if (!strcmp(a, "noon")) t = 6000;
        else if (!strcmp(a, "night")) t = 14000;
        else if (!strcmp(a, "midnight")) t = 18000;
        else if (a[0] >= '0' && a[0] <= '9') t = atoi(a);
        if (t < 0) { say("LegendPro: usage /time set day|night|noon|midnight|N"); return true; }
        level_setTime(level, t);
        snprintf(out, sizeof out, "LegendPro: time set to %d", t);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "timeset") || !strcmp(cmd, "timeadd")) {
        void* level = entity_getLevel ? entity_getLevel(self) : nullptr;
        if (!level_setTime || !level) return false;
        bool add = cmd[4] == 'a';
        if (argc < 2) { say("LegendPro: usage /timeset day|night|noon|midnight|N   /timeadd N"); return true; }
        int t = -1;
        const char* a = argv[1];
        if (!add && !strcmp(a, "day")) t = 1000;
        else if (!add && !strcmp(a, "noon")) t = 6000;
        else if (!add && !strcmp(a, "night")) t = 14000;
        else if (!add && !strcmp(a, "midnight")) t = 18000;
        else if ((a[0] >= '0' && a[0] <= '9') || (a[0] == '-' && a[1])) t = atoi(a);
        else { say("LegendPro: usage /timeset day|night|noon|midnight|N   /timeadd N"); return true; }
        if (add) { if (!level_getTime) return false; t = level_getTime(level) + t; }
        if (t < 0) t = 0;
        level_setTime(level, t);
        snprintf(out, sizeof out, "LegendPro: time set to %d", t);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "gethealth")) {
        if (!mob_getHealth) return false;
        snprintf(out, sizeof out, "LegendPro: health %d", mob_getHealth(self));
        say(out);
        return true;
    }
    if (!strcmp(cmd, "health")) {
        if (!mob_heal || !mob_getHealth) return false;
        int want = argc >= 2 ? atoi(argv[1]) : 20;
        int cur = mob_getHealth(self);
        if (want > cur) mob_heal(self, want - cur);
        snprintf(out, sizeof out, "LegendPro: health %d", mob_getHealth(self));
        say(out);
        return true;
    }
    if (!strcmp(cmd, "xp") || !strcmp(cmd, "lvl")) {
        bool lvl = cmd[0] == 'l';
        if (lvl ? !player_addLevels : !player_addXp) return false;
        if (argc < 2) { say(lvl ? "LegendPro: usage /lvl N" : "LegendPro: usage /xp N"); return true; }
        int n = atoi(argv[1]);
        if (n < 0) n = 0;
        if (n > 1000) n = 1000;
        if (lvl) player_addLevels(self, n); else player_addXp(self, n);
        snprintf(out, sizeof out, lvl ? "LegendPro: +%d levels" : "LegendPro: +%d xp", n);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "setworldspawn")) {
        void* level = entity_getLevel ? entity_getLevel(self) : nullptr;
        const float* p = entity_getPos ? entity_getPos(self) : nullptr;
        if (!level_setSpawn || !level || !p) return false;
        int pos[3] = {(int)__builtin_floorf(p[0]), (int)__builtin_floorf(p[1]), (int)__builtin_floorf(p[2])};
        level_setSpawn(level, pos);
        snprintf(out, sizeof out, "LegendPro: world spawn set to %d %d %d", pos[0], pos[1], pos[2]);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "timelock") || !strcmp(cmd, "timestop") || !strcmp(cmd, "timestart")) {
        void* level = entity_getLevel ? entity_getLevel(self) : nullptr;
        if (!level_setTime || !level || !level_getTime) return false;
        if (!strcmp(cmd, "timestart")) {
            g_timeLock = -1;
            say("LegendPro: time runs again");
        } else {
            int t = argc >= 2 ? atoi(argv[1]) : level_getTime(level);
            if (t < 0) t = 0;
            g_timeLock = t;
            snprintf(out, sizeof out, "LegendPro: time locked at %d  (/timestart to unlock)", t);
            say(out);
        }
        return true;
    }
    if (!strcmp(cmd, "clearinventory")) {
        if (!inv_clearSlot) return false;
        void* inv = *(void**)((char*)self + 0xDFC);                 // Player::mInventory (read from Player::getSelectedItem)
        if (!inv) return false;
        for (int i = 0; i < 45; i++) inv_clearSlot(inv, i);
        say("LegendPro: inventory cleared");
        return true;
    }
    if (!strcmp(cmd, "jmp")) {
        const float* p = entity_getPos ? entity_getPos(self) : nullptr;
        if (!entity_setPos || !entity_getViewVector || !p) return false;
        float n = argc >= 2 ? (float)atof(argv[1]) : 5.f;
        if (n > 200.f) n = 200.f;
        if (n < -200.f) n = -200.f;
        float v[3] = {0.f, 0.f, 0.f};
        entity_getViewVector(v, self, 1.0f);
        float np[3] = {p[0] + v[0] * n, p[1] + v[1] * n, p[2] + v[2] * n};
        entity_setPos(self, np);
        snprintf(out, sizeof out, "LegendPro: moved %.0f blocks along your view", n);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "heal")) {
        if (!mob_heal) return false;
        int n = argc >= 2 ? atoi(argv[1]) : 20;
        if (n < 1) n = 1;
        if (n > 40) n = 40;
        mob_heal(self, n);
        snprintf(out, sizeof out, "LegendPro: healed %d", n);
        say(out);
        return true;
    }
    if (!strcmp(cmd, "give")) {
        if (!item_ctor || !player_drop) return false;
        int id = argc >= 2 ? atoi(argv[1]) : 0;
        int count = argc >= 3 ? atoi(argv[2]) : 1;
        int data = argc >= 4 ? atoi(argv[3]) : 0;
        if (id < 1 || id > 511) { say("LegendPro: usage /give id [count] [data]  (id 1-511)"); return true; }
        if (count < 1) count = 1;
        if (count > 64) count = 64;
        alignas(8) unsigned char item[128];
        memset(item, 0, sizeof item);
        item_ctor(item, id, count, data);
        player_drop(self, item, false);                             // drops the stack at your feet
        snprintf(out, sizeof out, "LegendPro: gave %d x item %d", count, id);
        say(out);
        return true;
    }
    return false;                                                   // not ours: normal chat / server command
}

// The game's command parser: let it run first. If it does not know the command ("... not found"),
// try our own list; its answer replaces the error line.
// ServerCommandParser::executeCommand is a static function: r0 = the result (std::string, returned through
// a hidden pointer), r1 = the command line (std::string const&), r2 = the user name (std::string by value).
// All four registers are forwarded untouched; only r1 is read.
// IMPORTANT: the function also returns that result pointer in r0 and the game uses it right after the
// call, so this hook must return `ret` on every path.
static void* (*orig_exec)(void*, const void*, void*, void*) = nullptr;
static void* my_exec(void* ret, const void* cmd, void* user, void* extra) {
    orig_exec(ret, cmd, user, extra);
    if (!ret || !cmd || !g_on[B_CMDS] || !g_localPlayer) return ret;
    const char* res = *(const char* const*)ret;                     // the std::string the parser returned
    if (!res || !strstr(res, "not found")) return ret;                  // the game ran it (or gave another error)
    const char* line = *(const char* const*)cmd;
    if (!line || (uintptr_t)line < 0x10000) return ret;
    g_nsay = 0;
    if (!runCommand(g_localPlayer, line)) return ret;               // not ours either: the game's error stays
    g_msgQueued = 0;                                                // lines 2.. wait until the game is done with this message
    for (int i = 1; i < g_nsay && g_msgQueued < 8; i++) {
        strncpy(g_msgQueue[g_msgQueued], g_say[i], 199);
        g_msgQueue[g_msgQueued][199] = 0;
        g_msgQueued++;
    }
    const void* holder = makeStr(g_nsay ? g_say[0] : "");
    if (holder) *(const void**)ret = *(const void* const*)holder;
    return ret;
}

// Player Names: the game asks every entity whether its name tag may be shown; say yes
// (names of players are drawn by LevelRenderer::renderNameTags, which only handles players).
static bool (*orig_canTag)(void*) = nullptr;
static bool my_canTag(void* self) {
    if (g_on[B_NAMES]) return true;
    return orig_canTag(self);
}

// Player Names, own tag. The game's renderNameTags loop may not draw the local player's tag (or the player
// is not in its list). We watch EntityRenderer::renderText: if the game did not draw our own tag during
// renderNameTags, we draw it ourselves right after (third person only; in first person the camera sits
// inside the head so nothing could be seen anyway).
static const char* g_nameHolder = nullptr;                          // one-pointer COW string object (kept forever)
static const void* getOwnName(void* o) {
    // 1) The name the game itself uses for the local player (Player::mName, written by Player::setName at +0xD28).
    //    On servers this is the real login name; Options::getUsername can be a different, saved value.
    if (g_localPlayer) {
        const void* obj = (const char*)g_localPlayer + 0xD28;
        const char* c = *(const char* const*)obj;
        if ((uintptr_t)c > 0x10000) {
            int len = *(const int*)(c - 12);
            if (len > 0 && len <= 32 && (unsigned char)c[0] >= 32) return obj;
        }
    }
    // 2) Fallback: Options::getUsername (kept forever, see below)
    if (g_nameHolder && *(const int*)(g_nameHolder - 12) > 0) return &g_nameHolder;
    if (!o || !opt_getUsername) return nullptr;
    const char* tmp = nullptr;
    ((void* (*)(const char**, void*))opt_getUsername)(&tmp, o);      // sret call; we keep the returned reference on purpose
    if (tmp && *(const int*)(tmp - 12) > 0) { g_nameHolder = tmp; return &g_nameHolder; }
    return nullptr;
}
static void (*orig_renderNameTags)(void*, float) = nullptr;
static void my_renderNameTags(void* self, float a) {
    orig_renderNameTags(self, a);
    // The game never draws the local player's own tag (EntityRenderer::renderText returns when the entity is the
    // dispatcher's camera entity), so after the normal loop we draw it ourselves. Third person only.
    if (!g_on[B_NAMES] || g_replay || !g_localPlayer || !g_client) return;
    if (!disp_getInstance || !disp_getRenderer || !er_renderText || !client_getOptions || !opt_getPersp) return;
    void* o = client_getOptions(g_client);
    if (!o || opt_getPersp(o) == 0) return;                         // first person: own tag cannot be seen
    const void* un = getOwnName(o);
    if (!un) return;
    void* disp = disp_getInstance();
    if (!disp) return;
    int rid = *(int*)((char*)g_localPlayer + 0x108);               // entity's renderer id (as renderNameTags reads it)
    void* rend = disp_getRenderer(disp, rid);
    if (!rend) return;
    // The camera-entity check reads dispatcher+0x1c (getter is only 4 bytes, cannot be hooked). For this one call
    // that field points to a small fake "entity" whose position (+8,+0xc,+0x10, the only fields
    // Entity::distanceToSqr(Entity const&) reads) equals the player's: distance 0, so the tag is drawn. Then restore.
    void** camSlot = (void**)((char*)disp + 0x1c);
    void* savedCam = *camSlot;
    if (savedCam != g_localPlayer) return;                          // camera is not the local player (freecam): do nothing
    static float fake[16];
    memset(fake, 0, sizeof fake);
    memcpy((char*)fake + 8, (char*)g_localPlayer + 8, 12);
    *camSlot = (void*)fake;
    er_renderText(rend, g_localPlayer, un, a, 0.f);
    *camSlot = savedCam;
}

// No Hurt Cam: skip the camera shake / tilt when you take damage.
static void (*orig_bobHurt)(void*, void*, float) = nullptr;
static void my_bobHurt(void* self, void* matrix, float t) {
    if (g_on[B_NOHURT]) return;
    orig_bobHurt(self, matrix, t);
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

static Vec3* (*orig_interp)(Vec3*, void*, float) = nullptr;
__attribute__((noinline)) static Vec3* my_interp(Vec3* ret, void* self, float a) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    orig_interp(ret, self, a);
    if (g_replay && g_rpCine && !g_ghostPass && self && self == g_localPlayer &&
        (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngComp, 480) || inRange(lr, g_rngFrom, 164))) {
        ret->x = g_cinePos[0];                             // cinematic camera: exactly our own position (first person)
        ret->y = g_cinePos[1];
        ret->z = g_cinePos[2];
        return ret;
    }
    if (self && self == g_localPlayer &&
        (g_ghostPass || (g_replay && (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngComp, 480) || inRange(lr, g_rngFrom, 164))))) {
        ret->x = g_ghostPos[0];                            // replay: the camera and the ghost model use the recorded position
        ret->y = g_ghostPos[1];
        ret->z = g_ghostPos[2];
        return ret;
    }
    if (g_freecam && self && self == g_player) {
        if (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngComp, 480) || inRange(lr, g_rngFrom, 164)) {
            ret->x += g_camOff.x;
            ret->y += g_camOff.y;
            ret->z += g_camOff.z;
        }
    }
    return ret;
}

// Replay ghost. The game draws every entity through EntityRenderDispatcher::render(entity, partialTick).
// During a replay the real you is skipped there, and after the entity loop (which would cull a far-away
// ghost) the local player is drawn once more while the position/rotation getters return the recording.
static void (*orig_dispRender)(void*, void*, float) = nullptr;
static void my_dispRender(void* self, void* ent, float a) {
    if (g_replay && ent && ent == g_localPlayer && !g_ghostPass) return;     // the real you stays invisible
    orig_dispRender(self, ent, a);
}

static void (*orig_rEnt)(void*, float) = nullptr;
static void my_renderEntities(void* self, float a) {
    orig_rEnt(self, a);
    if (!g_replay || !g_localPlayer || !disp_getInstance || !orig_dispRender) return;
    void* disp = disp_getInstance();
    if (!disp) return;
    // walk animation: Mob fields found in Mob::updateWalkAnim (walkAnimSpeedO +0x1e4, walkAnimSpeed +0x1e8,
    // walkAnimPos +0x1ec). The frozen real player never walks, so we write the simulated values for this one draw.
    float* wa = (float*)((char*)g_localPlayer + 0x1e4);
    float save[3] = {wa[0], wa[1], wa[2]};
    wa[0] = g_gwSpeed; wa[1] = g_gwSpeed; wa[2] = g_gwPos + g_gwSpeed * (1.f - a);
    g_ghostPass = true;
    orig_dispRender(disp, g_localPlayer, a);
    g_ghostPass = false;
    wa[0] = save[0]; wa[1] = save[1]; wa[2] = save[2];
}

// Replay camera: only the CAMERA's rotation is replaced (the character keeps its recorded one), so the
// third-person camera - which sits behind the view direction - circles around your character.
static float* (*orig_irot)(float*, void*, float) = nullptr;
__attribute__((noinline)) static float* my_irot(float* ret, void* self, float a) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    orig_irot(ret, self, a);
    if (g_ghostPass && self && self == g_localPlayer) {   // the ghost model faces the way you were facing
        ret[0] = g_ghostRot[0];
        ret[1] = g_ghostRot[1];
        return ret;
    }
    if (g_freecam && g_fcLook && !g_replay && self && self == g_player &&
        (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngPrep, 456))) {
        // moveCameraToPlayer = the picture; EntityRenderDispatcher::prepare = the direction text/name tags face (billboard).
        ret[0] = g_fcRot[0];                                   // freecam: the camera looks where the camera (not the body) looks
        ret[1] = g_fcRot[1];
        return ret;
    }
    if (g_replay && g_rpCine && !g_ghostPass && self && self == g_localPlayer &&
        (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngPrep, 456))) {
        ret[0] = g_cineRot[0];                                 // cinematic camera rotation (also used for name tag billboards)
        ret[1] = g_cineRot[1];
        return ret;
    }
    if (g_replay && g_rpOrbit && self && self == g_localPlayer && inRange(lr, g_rngMove, 2720)) {
        ret[g_yawIdx] = g_rpOrbitYaw;
        ret[1 - g_yawIdx] = 22.f;                                // look a little downwards
    }
    return ret;
}

// While freecam is on the player does not walk: the movement the player would
// have made is applied to the camera instead. Joystick/D-pad = horizontal,
// jump button = up (only upward motion is used, so gravity never pulls the
// camera down), sneak button = down (handled in my_update).
static void (*orig_lpmove)(void*, const float*) = nullptr;
static void my_lpmove(void* self, const float* d) {
    if (g_replay && self && self == g_localPlayer) {                // the real you is frozen while the replay plays
        if (g_rpCine && !g_cinePlay) {                              // ...but the joystick flies the cinematic camera
            float dx = d[0], dz = d[2];
            if (g_fcLook && entity_getRotation) {
                float br[2] = {0.f, 0.f};
                entity_getRotation(br, self);
                float dy = (g_cineRot[g_yawIdx] - br[g_yawIdx]) * 0.017453292f;
                float cs = __builtin_cosf(dy), sn = __builtin_sinf(dy);
                float nx = dx * cs - dz * sn, nz = dz * cs + dx * sn;
                dx = nx; dz = nz;
            }
            g_cinePos[0] += dx * FC_MOVE_MUL;
            g_cinePos[2] += dz * FC_MOVE_MUL;
            if (d[1] > 0.f) g_cinePos[1] += d[1];                   // jump held = camera up
        }
        return;
    }
    if (g_freecam && self && self == g_player) {
        float dx = d[0], dz = d[2];
        if (g_fcLook && entity_getRotation) {                // walking direction follows the camera, not the body
            float br[2] = {0.f, 0.f};
            entity_getRotation(br, self);
            float dy = (g_fcRot[g_yawIdx] - br[g_yawIdx]) * 0.017453292f;
            float cs = __builtin_cosf(dy), sn = __builtin_sinf(dy);
            float nx = dx * cs - dz * sn, nz = dz * cs + dx * sn;
            dx = nx; dz = nz;
        }
        g_camOff.x += dx * FC_MOVE_MUL;
        g_camOff.z += dz * FC_MOVE_MUL;
        if (d[1] > 0.f) g_camOff.y += d[1];           // jump held = rise
        fcClamp();
        return;
    }
    orig_lpmove(self, d);
}

// Freecam look: touch-look normally turns the BODY (and the camera follows it). While freecam is on we let the game
// turn the body as usual, measure how much it turned, give that turn to the camera rotation instead and put the body
// rotation back. So the body does not spin and you can look at its face.
static void (*orig_lpturn)(void*, const float*, bool) = nullptr;
static void my_lpturn(void* self, const float* v, bool b) {
    bool fc = g_freecam && !g_replay && self && self == g_player;
    bool cine = g_replay && g_rpCine && self && self == g_localPlayer;
    if ((fc || cine) && g_fcLook && entity_getRotation && entity_setRot) {
        float* rot = cine ? g_cineRot : g_fcRot;
        float before[2] = {0.f, 0.f}, after[2] = {0.f, 0.f};
        entity_getRotation(before, self);
        orig_lpturn(self, v, b);
        entity_getRotation(after, self);
        if (!(cine && g_cinePlay)) {                                // while the shots play the camera ignores touch-look
            rot[0] += after[0] - before[0];
            rot[1] += after[1] - before[1];
            float pi = rot[1 - g_yawIdx];                           // pitch stays between -90 and 90
            if (pi > 90.f) pi = 90.f;
            if (pi < -90.f) pi = -90.f;
            rot[1 - g_yawIdx] = pi;
        }
        entity_setRot(self, before);                                // the body keeps its rotation
        return;
    }
    orig_lpturn(self, v, b);
}

// Installed lazily (first time freecam is switched on, on the game thread, which is the only thread that runs
// LocalPlayer::localPlayerTurn), so merely loading a world can never be affected by this hook.
static void* g_gameLib = nullptr;
static bool hookSym(void* game, const char* name, void* repl, void** orig, bool required);
static void installLookHook() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!g_gameLib || !orig_irot || !entity_getRotation || !entity_setRot) return;
    bool t = hookSym(g_gameLib, "_ZN11LocalPlayer15localPlayerTurnERK4Vec2b", (void*)my_lpturn, (void**)&orig_lpturn, false);
    g_fcLook = t && orig_lpturn;
}

// ---- feature hooks: skip the original function when the toggle is on ----
static void (*orig_hand)(void*, bool, float) = nullptr;
static void my_hand(void* self, bool a, float t) {
    if (g_on[B_NOHAND] || (g_replay && g_rpCine)) return;      // cinematic camera is first person: no hand in the picture
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

// Clear Weather: no rain level, not raining, no rain ticks.
// (Weather::setRainLevel is only 4 bytes long - too small to hook safely.)
static float (*orig_getRain)(void*, float) = nullptr;
static float my_getRain(void* self, float a) {
    if (g_on[B_CLEAR]) return 0.f;
    return orig_getRain(self, a);
}

static bool (*orig_isRaining)(void*) = nullptr;
static bool my_isRaining(void* self) {
    if (g_on[B_CLEAR]) return false;
    return orig_isRaining(self);
}

static void (*orig_tickRain)(void*) = nullptr;
static void my_tickRain(void* self) {
    if (g_on[B_CLEAR]) return;
    orig_tickRain(self);
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
    g_gameLib = game;
    if (!game) { LOG("libminecraftpe.so not loaded"); return JNI_VERSION_1_6; }
    if (!loadSymbols(game)) { LOG("symbol resolve failed"); return JNI_VERSION_1_6; }

    void* sub = dlopen("libsubstrate.so", RTLD_NOW);
    g_hook = sub ? (MSHookFunction_t)dlsym(sub, "MSHookFunction") : nullptr;
    if (!g_hook) { LOG("libsubstrate.so / MSHookFunction not found"); return JNI_VERSION_1_6; }

    resetButtons();
    cfgInitPath();
    cfgLoad();
    if (g_guardPath[0]) {                                  // an earlier start died while enabling the game's commands
        FILE* gf = fopen(g_guardPath, "r");
        if (gf) { fclose(gf); g_nativeCmdDisabled = true; LOG("game commands disabled (crash guard)"); }
    }

    bool ok = true;
    ok &= hookSym(game, "_ZN15MinecraftClient6updateEv", (void*)my_update, (void**)&orig_update, true);
    ok &= hookSym(game, "_ZN14HotBarRenderer6renderER15MinecraftClientRSt10shared_ptrI9UIControlEiR13RectangleArea",
                  (void*)my_hotbar, (void**)&orig_hotbar, true);
    ok &= hookSym(game, "_ZN10Multitouch4feedEccssi", (void*)my_feed, (void**)&orig_feed, true);
    hookSym(game, "_ZN13OptionsScreen6renderEiif", (void*)my_optRender, (void**)&orig_optRender, false);
    hookSym(game, "_ZN6Player11dropAllGearEi",      (void*)my_dropGear,   (void**)&orig_dropGear,   false);
    hookSym(game, "_ZN9Inventory7dropAllEb",        (void*)my_invDropAll, (void**)&orig_invDropAll, false);
    hookSym(game, "_ZN13SoulSandBlock12entityInsideER11BlockSourceRK8BlockPosR6Entity", (void*)my_soul, (void**)&orig_soul, false);
    hookSym(game, "_ZN18ItemInHandRenderer6renderEbf",  (void*)my_hand,   (void**)&orig_hand,   false);
    hookSym(game, "_ZN13LevelRenderer12renderCloudsEf", (void*)my_clouds, (void**)&orig_clouds, false);
    hookSym(game, "_ZN13LevelRenderer12_renderStarsEf", (void*)my_stars,  (void**)&orig_stars,  false);
    hookSym(game, "_ZNK7Weather12getRainLevelEf",       (void*)my_getRain,   (void**)&orig_getRain,   false);
    hookSym(game, "_ZNK7Weather9isRainingEv",           (void*)my_isRaining, (void**)&orig_isRaining, false);
    hookSym(game, "_ZN19ServerCommandParser14executeCommandERKSsSs", (void*)my_exec, (void**)&orig_exec, false);
    hookSym(game, "_ZNK6Entity23getInterpolatedRotationEf", (void*)my_irot, (void**)&orig_irot, false);
    hookSym(game, "_ZN22EntityRenderDispatcher6renderER6Entityf", (void*)my_dispRender, (void**)&orig_dispRender, false);
    hookSym(game, "_ZN13LevelRenderer14renderEntitiesEf", (void*)my_renderEntities, (void**)&orig_rEnt, false);
    hookSym(game, "_ZN6Entity14canShowNameTagEv", (void*)my_canTag, (void**)&orig_canTag, false);
    if (er_renderText && disp_getRenderer) {
        hookSym(game, "_ZN13LevelRenderer14renderNameTagsEf", (void*)my_renderNameTags, (void**)&orig_renderNameTags, false);
    }
    hookSym(game, "_ZN13LevelRenderer7bobHurtER6Matrixf", (void*)my_bobHurt, (void**)&orig_bobHurt, false);
    hookSym(game, "_ZN13LevelRenderer8tickRainEv",      (void*)my_tickRain,  (void**)&orig_tickRain,  false);
    hookSym(game, "_ZN13LevelRenderer15renderHitSelectER11BlockSourceRK8BlockPosfb",
            (void*)my_outline, (void**)&orig_outline, false);

    // freecam: needs the camera function ranges plus both hooks
    g_rngMove = (uintptr_t)dlsym(game, "_ZN13LevelRenderer18moveCameraToPlayerER6Matrixf") & ~(uintptr_t)1;
    g_rngComp = (uintptr_t)dlsym(game, "_ZN13LevelRenderer16computeCameraPosEf") & ~(uintptr_t)1;
    g_rngFrom = (uintptr_t)dlsym(game, "_ZNK13LevelRenderer22getCameraPosFromEntityEfRK6Entity") & ~(uintptr_t)1;
    g_rngPrep = (uintptr_t)dlsym(game, "_ZN22EntityRenderDispatcher7prepareEP5LevelP4FontP6EntityS5_P7Optionsf") & ~(uintptr_t)1;
    g_rngLevel = (uintptr_t)dlsym(game, "_ZN13LevelRenderer11renderLevelER6EntityR13FrustumCullerS3_ff") & ~(uintptr_t)1;
    bool fc = g_rngMove && g_rngComp && g_rngFrom;
    fc &= hookSym(game, "_ZNK6Entity23getInterpolatedPositionEf", (void*)my_interp, (void**)&orig_interp, true);
    fc &= hookSym(game, "_ZN11LocalPlayer4moveERK4Vec3", (void*)my_lpmove, (void**)&orig_lpmove, true);
    g_freecamReady = fc && orig_interp && orig_lpmove;
    LOG("hooks %s, freecam %s", ok ? "installed" : "FAILED (required hook missing)",
        g_freecamReady ? "ready" : "unavailable");
    return JNI_VERSION_1_6;
}
