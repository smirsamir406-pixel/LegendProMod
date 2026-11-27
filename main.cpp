// libclient.so v19 - client-side mod for Minecraft PE 0.15.10 (armeabi-v7a).
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
#include <math.h>
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
       SL_MAPN = 13, SL_MAPS = 14, SL_GBTN = 15 /* 31 */, SL_EDIT = 168 /* 16 */, SL_MAPC = 49 /* 3 */,
       SL_OPTTAB = 62, SL_OPTHDR = 63, SL_OPTHDR2 = 64,
       SL_OPTLBL = 65 /* 40 */, SL_OPTST = 105 /* 40 */, SL_CTRL = 146 /* 22 */, SL_COUNT = 184 };
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
static void  (*opt_setBob)(void*, bool);
static bool  (*opt_getBob)(void*);
static void  (*opt_setSmooth)(void*, bool);
static void  (*opt_setHideGui)(void*, bool);
static bool  (*opt_getHideGui)(void*);
static void  (*opt_setParticles)(void*, float);
static float (*opt_getParticles)(void*);
static void  (*opt_setPersp)(void*, int);
static int   (*opt_getPersp)(void*);
static int   (*opt_getViewDist)(void*);                           // Options::getViewDistanceChunks
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
static int   (*fc_getContainerSize)(void*);                       // FillingContainer::getContainerSize()
static int   (*fc_getLinkedSlot)(void*, int);                     // hotbar slot -> raw item index
static int   (*fc_getLinkedCount)(void*);
static void  (*fc_swapSlots)(void*, int, int);                    // swaps the two raw item pointers
static int   (*inv_getSelectedSlot)(void*);
static void* (*mc_getInput)(void*);                               // MinecraftClient::getInput() const -> MinecraftInputHandler*
static void* (*mih_getMoveTurnInput)(void*);                      // MinecraftInputHandler::getMoveTurnInput() -> MoveInput* (the one LocalPlayer::updateAi reads)
static void  (*mc_attackPress2)(void*);                           // fallback: handleDestoryOrAttackButtonPress()
static void  (*mc_attackPress)(void*);                            // MinecraftClient::handleDestoryOrAttackButtonPress()
static void  (*mc_attackRelease)(void*);                          // MinecraftClient::handleAttackActionButtonRelease()
static void  (*mc_placePress)(void*);                             // MinecraftClient::handleBuildOrInteractButtonPress()
static void  (*mc_placeRelease)(void*);                           // MinecraftClient::handleBuildActionButtonRelease()
static void  (*lp_setSneaking)(void*, bool);
static void* (*level_getHitResult)(void*);                        // Level::getHitResult(): HitResult at level+0x160, type at +0 (1 = entity), entity* at +0x20
static void  (*gm_attack)(void*, void*, void*);                   // GameMode::attack(Player&, Entity&)
static void  (*lp_swing)(void*);                                  // LocalPlayer::swing()                      // LocalPlayer::setSneaking(bool)
static void* (*item_getHoverName)(const char**, void*);           // really returns std::string BY VALUE (sret)

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
    OPT(opt_setBob,               "_ZN7Options10setBobViewEb")
    OPT(opt_getBob,               "_ZNK7Options10getBobViewEv")
    OPT(opt_setSmooth,            "_ZN7Options15setSmoothCameraEb")
    OPT(opt_setHideGui,           "_ZN7Options10setHideGuiEb")
    OPT(opt_getHideGui,           "_ZNK7Options10getHideGuiEv")
    OPT(opt_setParticles,         "_ZN7Options23setParticleViewDistanceEf")
    OPT(opt_getParticles,         "_ZNK7Options23getParticleViewDistanceEv")
    OPT(opt_setPersp,             "_ZN7Options24setPlayerViewPerspectiveEi")
    OPT(opt_getPersp,             "_ZNK7Options24getPlayerViewPerspectiveEv")
    OPT(opt_getViewDist,          "_ZNK7Options21getViewDistanceChunksEv")
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
    OPT(fc_getContainerSize,      "_ZNK16FillingContainer16getContainerSizeEv")
    OPT(fc_getLinkedSlot,         "_ZNK16FillingContainer13getLinkedSlotEi")
    OPT(fc_getLinkedCount,        "_ZNK16FillingContainer19getLinkedSlotsCountEv")
    OPT(fc_swapSlots,             "_ZN16FillingContainer9swapSlotsEii")
    OPT(inv_getSelectedSlot,      "_ZNK9Inventory15getSelectedSlotEv")
    OPT(item_getHoverName,        "_ZNK12ItemInstance12getHoverNameEv")
    OPT(mc_getInput,              "_ZNK15MinecraftClient8getInputEv")
    OPT(mih_getMoveTurnInput,     "_ZN21MinecraftInputHandler16getMoveTurnInputEv")
    OPT(mc_attackPress,           "_ZN15MinecraftClient34handleDestroyOrInteractButtonPressEv")
    OPT(mc_attackPress2,          "_ZN15MinecraftClient32handleDestoryOrAttackButtonPressEv")
    OPT(mc_attackRelease,         "_ZN15MinecraftClient31handleAttackActionButtonReleaseEv")
    OPT(mc_placePress,            "_ZN15MinecraftClient32handleBuildOrInteractButtonPressEv")
    OPT(mc_placeRelease,          "_ZN15MinecraftClient30handleBuildActionButtonReleaseEv")
    if (!mc_attackPress) mc_attackPress = mc_attackPress2;
    OPT(level_getHitResult,       "_ZN5Level12getHitResultEv")
    OPT(gm_attack,                "_ZN8GameMode6attackER6PlayerR6Entity")
    OPT(lp_swing,                 "_ZN11LocalPlayer5swingEv")
    OPT(lp_setSneaking,           "_ZN11LocalPlayer11setSneakingEb")
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

static void* g_gameLib = nullptr;               // handle of libminecraftpe.so
static bool g_fcLook = false;                   // look-decoupling hook installed (turn + camera rotation)
static int g_yawIdx = 1;                        // which number of the rotation pair is the yaw (auto-detected)
static volatile int g_frame = 0;
static volatile int g_lastHudFrame = -100;
static volatile int g_lastOptFrame = -100;      // last frame the game's Options screen was drawn
static volatile bool g_optPage = false;         // our "LP" page is selected in Options
static float g_optW = 515.f;                    // screen size in GUI units (read from the game)
static float g_guiW = 515.f, g_guiH = 240.f;

enum { B_VIEW, B_FPS, B_ARMOR, B_LIGHT, B_JOY, B_OFFHAND,
       B_NOCLOUD, B_NOSTAR, B_CLEAR, B_OUTLINE, B_AUTOSPR, B_NOBOB, B_SMOOTH,
       B_Q, B_FREECAM, B_NOPART, B_POS, B_MAP, B_EDIT,
       B_KEEPINV, B_NOSOUL, B_CMDS, B_CLOCK, B_NOHURT, B_ZOOM, B_NAMES,
       B_ATTACK, B_PLACE, B_JUMP, B_SNEAK, B_BREAK, B_COUNT };
static const int R_RESET = B_COUNT;                              // "reset layout" action

// short names of the in-game buttons
static const char* SHORTL[B_COUNT] = {
    "F5", "FPS", "ARMOR", "LIGHT", "JOY", "OFFHAND",
    "CLOUD", "STAR", "CLEAR", "OUTLINE", "SPRINT", "BOB", "SMOOTH",
    "DROP", "CAM", "PART", "XYZ", "MAP", "EDIT",
    "KEEP", "SOUL", "CMD", "12:00 DAY 1", "HURT", "ZOOM 3", "NAMES",
    "ATTACK", "PLACE", "JUMP", "SNEAK", "BREAK"};
// features that can have a button on the screen (Options: BUTTONS section)
static bool isButtonFeature(int i) {
    return !(i == B_FPS || i == B_ARMOR || i == B_POS || i == B_MAP || i == B_KEEPINV || i == B_CMDS || i == B_NAMES);
}
static volatile bool g_on[B_COUNT] = {
    false, true, false, false, false, false,
    false, false, false, true, false, false, false,
    false, false, false, true, true, false,
    false, false, false, false, false, false, false,
    false, false, false, false, false};
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

// ---- touch controls: our own Attack / Place / Jump / Sneak / D-pad buttons (graphic, editable like all buttons)
static bool isHoldBtn(int i) { return i == B_JOY || i == B_ATTACK || i == B_PLACE || i == B_JUMP || i == B_SNEAK || i == B_BREAK; }
static bool isGfxBtn(int i)  { return isHoldBtn(i) || i == B_AUTOSPR; }
static volatile unsigned g_holdMask = 0;        // bit i = hold button i is held down (touch thread -> game thread)
static volatile unsigned g_dpadMask = 0;        // bit c = D-pad cell c is held (cells 0..8, row by row; 4 = centre = sneak)
static int g_holdBtn[16], g_dpadCell[16];       // per touch pointer
static int g_ctrlMode = 0;                      // preset in use: 0 off (game's own), 1 joystick+tap, 2 d-pad+tap, 3 joystick+crosshair
static int g_ctrlSel = 1;                       // radio selection in the Touch Controls page (not applied until SELECT)
static volatile int g_ctrlApply = 0;            // SELECT pressed: preset number + 1 (touch thread -> game thread)
static bool g_dpFlipX = false, g_dpFlipY = false;   // fix for mirrored D-pad directions (axis signs are unverified)
static volatile bool g_ctrlPage = false;        // the Touch Controls sub page is open inside Options
static volatile int g_dbgTick = 0, g_dbgSame = 0, g_dbgAtk = 0, g_dbgPlace = 0, g_dbgJump = 0, g_dbgDpad = 0;   // shown on the Touch Controls page
static bool g_hideNative = true;                 // hide (and disable) the game's own touch buttons while a preset is on
static volatile float g_cmdYa = 0.f, g_cmdXa = 0.f;   // last values we wrote into the game's move input (0 = idle)
static int g_yaFwd = 0, g_xaRight = 0;           // learned: +1 = ya>0 is forward / xa>0 is right, -1 = opposite, 0 = not learned yet
static float g_calF = 0.f, g_calS = 0.f;
static volatile float g_joyX = 0.f, g_joyY = 0.f;   // joystick: x right +, y down + (-1..1), 0 when released
static volatile int g_dbgTouch = 0, g_dbgHit = 0, g_dbgJoy = 0;
static int joySize() { return g_btnW[B_JOY] > 0 ? g_btnW[B_JOY] : 108; }

static int defBtnW(int i) {
    if (i == B_OFFHAND) return 74;                     // shows the offhand item name + count
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
    if (i == B_JOY) {                                  // square joystick base, g_btnW = size
        int sz = joySize();
        *w = *h = sz;
        *x = g_btnX[i] >= 0 ? g_btnX[i] : 24;
        *y = g_btnY[i] >= 0 ? g_btnY[i] : (int)g_guiH - sz - 8;
        return true;
    }
    if (isGfxBtn(i)) {                                 // square graphic buttons, default place on the right edge
        *w = g_btnW[i] > 0 ? g_btnW[i] : 32;
        *h = g_btnH[i] > 0 ? g_btnH[i] : 32;
        if (g_btnX[i] >= 0) { *x = g_btnX[i]; *y = g_btnY[i]; return true; }
        int gx = (int)g_guiW, gy = (int)(g_guiH * 0.40f);
        switch (i) {
        case B_PLACE:   *x = gx - 42; *y = gy;      break;
        case B_JUMP:    *x = gx - 42; *y = gy + 38; break;
        case B_SNEAK:   *x = gx - 42; *y = gy + 76; break;
        case B_ATTACK:  *x = gx - 80; *y = gy + 10; break;
        case B_BREAK:   *x = gx - 80; *y = gy + 86; break;
        default:        *x = gx - 80; *y = gy + 48; break;   // sprint
        }
        return true;
    }
    *w = g_btnW[i] > 0 ? g_btnW[i] : defBtnW(i);
    *h = g_btnH[i] > 0 ? g_btnH[i] : 11;
    if (g_btnX[i] >= 0) { *x = g_btnX[i]; *y = g_btnY[i]; }
    else {
        int slot = 0;
        for (int j = 0; j < i; j++) if (g_btnOn[j] && g_btnX[j] < 0 && !isGfxBtn(j)) slot++;
        *x = (int)g_guiW - *w - 8 - (slot / 8) * 62;
        *y = 24 + (slot % 8) * 16;
    }
    return true;
}

// Edit panel (shown while editing the buttons). Row 1: move < > ^ v, STEP (1/2/4/8 units), RESET (this button), DONE.
// Row 2: width -/+, height -/+, whole size -/+.
static const int EP_N = 13;
static const char* EP_LABEL[EP_N] = {"<", ">", "^", "v", "STEP", "RESET", "DONE", "W-", "W+", "H-", "H+", "S-", "S+"};
static const int EP_STEPS[4] = {1, 2, 4, 8};
static int g_editStep = 1;
static bool tbRect(int k, int* x, int* y, int* w, int* h) {
    if (k < 0 || k >= EP_N) return false;
    int row = k < 7 ? 0 : 1, col = k < 7 ? k : k - 7, cnt = row == 0 ? 7 : 6;
    *x = (int)g_guiW / 2 - cnt * 17 + col * 34; *y = 36 + row * 15; *w = 32; *h = 13;
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
static char g_nameGuardPath[300];                   // exists while the first item-name lookup runs (crash guard)
static char g_guardPath[300];                       // exists while the game's commands are being enabled (crash guard)

static bool persistFlag(int i) {
    return i == B_FPS || i == B_ARMOR || i == B_NOCLOUD || i == B_NOSTAR ||
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
    snprintf(g_cfgPath, sizeof g_cfgPath, "/data/data/%s/lp_buttons2.cfg", name);
    snprintf(g_guardPath, sizeof g_guardPath, "/data/data/%s/lp_cmd.guard", name);
    snprintf(g_nameGuardPath, sizeof g_nameGuardPath, "/data/data/%s/lp_name.guard", name);
}

static void cfgSave() {
    if (!g_cfgPath[0]) return;
    FILE* f = fopen(g_cfgPath, "w");
    if (!f) return;
    for (int i = 0; i < B_COUNT; i++)
        fprintf(f, "b %d %d %d %d %d %d\n", i, g_btnOn[i] ? 1 : 0, g_btnX[i], g_btnY[i], g_btnW[i], g_btnH[i]);
    for (int i = 0; i < B_COUNT; i++)
        if (persistFlag(i)) fprintf(f, "f %d %d\n", i, g_on[i] ? 1 : 0);
    fprintf(f, "m %d %d %d %d %d %d\n", g_ctrlMode, g_dpFlipX ? 1 : 0, g_dpFlipY ? 1 : 0, g_yaFwd, g_xaRight, g_hideNative ? 1 : 0);
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
        } else if (kind == 'm') {
            int m = 0, fx = 0, fy = 0, yf = 0, xr = 0, hn = 1;
            int got = fscanf(f, "%d %d %d %d %d %d", &m, &fx, &fy, &yf, &xr, &hn);
            if (got >= 1) g_ctrlMode = m == 3 ? 3 : (m == 0 ? 0 : 1);          // the old D-pad mode (2) became the joystick mode
            if (got == 6) { g_dpFlipX = fx != 0; g_dpFlipY = fy != 0; g_yaFwd = yf < -1 || yf > 1 ? 0 : yf; g_xaRight = xr < -1 || xr > 1 ? 0 : xr; g_hideNative = hn != 0; }
        } else if (kind == 'f') {
            int i, v;
            if (fscanf(f, "%d %d", &i, &v) == 2 && i >= 0 && i < B_COUNT && persistFlag(i)) g_on[i] = v != 0;
        } else break;
    }
    fclose(f);
}

static bool hookSym(void* game, const char* name, void* repl, void** orig, bool required);

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

// ---------------- Offhand ----------------
// The game has no offhand. We reserve ONE inventory slot (the last raw slot that is not a hotbar slot) as the
// offhand slot. The Offhand button swaps the item you hold with the item in that slot (like F in Java Edition).
// Own world only: on a server the inventory belongs to the server.
// Inventory layout (decoded): Player+0xdfc = Inventory*, items = vector<ItemInstance*> at +0x2c (begin) / +0x30 (end),
// ItemInstance: count at +0xc. An empty slot is a null pointer or count 0.
static void* getInventory(void* player) {
    if (!player) return nullptr;
    void* inv = *(void**)((char*)player + 0xdfc);
    return (uintptr_t)inv > 0x10000 ? inv : nullptr;
}
static int offhandIndex(void* inv) {
    if (!fc_getContainerSize || !fc_getLinkedCount || !fc_getLinkedSlot) return -1;
    int n = fc_getContainerSize(inv), lc = fc_getLinkedCount(inv);
    if (n <= 0 || n > 200 || lc < 0 || lc > 64) return -1;
    for (int idx = n - 1; idx >= 0; idx--) {
        bool linked = false;
        for (int k = 0; k < lc; k++) if (fc_getLinkedSlot(inv, k) == idx) { linked = true; break; }
        if (!linked) return idx;
    }
    return -1;
}
static void* offhandItem(void* inv, int idx) {                   // null when the slot is empty
    void** items = *(void***)((char*)inv + 0x2c);
    if (!items || idx < 0) return nullptr;
    void* it = items[idx];
    if (!it || *(int*)((char*)it + 0xc) <= 0) return nullptr;
    return it;
}
static void offhandSwap(void* client) {
    if (!client_getLocalPlayer || !fc_swapSlots || !inv_getSelectedSlot || !fc_getLinkedSlot || !fc_getContainerSize) {
        localMsg("LegendPro: offhand is not available in this build");
        return;
    }
    if (!isLocalWorld()) { localMsg("LegendPro: offhand works in your own world only"); return; }
    void* inv = getInventory(client_getLocalPlayer(client));
    if (!inv) return;
    int off = offhandIndex(inv);
    int main = fc_getLinkedSlot(inv, inv_getSelectedSlot(inv));
    int n = fc_getContainerSize(inv);
    if (off < 0 || main < 0 || main >= n || main == off) return;
    fc_swapSlots(inv, main, off);
}
static bool g_nameDisabled = false;                               // set when an earlier start died inside the name lookup
static char g_offLabel[24] = "OFFHAND";
static int g_offKeyId = -2, g_offKeyAux = -2, g_offKeyCount = -2;
static char g_offName[20];
// text on the Offhand button: the item name (looked up once per item) and its count
static const char* offhandLabel(int maxChars) {
    void* inv = getInventory(g_localPlayer);
    void* it = inv ? offhandItem(inv, offhandIndex(inv)) : nullptr;
    if (!it || !item_getId || !item_getAux) return "OFFHAND";
    int id = item_getId(it), aux = item_getAux(it), cnt = *(int*)((char*)it + 0xc);
    if (id != g_offKeyId || aux != g_offKeyAux) {
        g_offKeyId = id; g_offKeyAux = aux; g_offKeyCount = -2;
        g_offName[0] = 0;
        if (item_getHoverName && !g_nameDisabled) {
            if (g_nameGuardPath[0]) { FILE* gf = fopen(g_nameGuardPath, "w"); if (gf) fclose(gf); }
            const char* tmp = nullptr;
            item_getHoverName(&tmp, it);                          // the returned string is kept on purpose (never freed)
            if (g_nameGuardPath[0]) remove(g_nameGuardPath);
            int len = ((uintptr_t)tmp > 0x10000) ? *(const int*)(tmp - 12) : 0;
            if (len > 0 && len < 100) {
                int o = 0;
                for (int i = 0; i < len && o < (int)sizeof g_offName - 1; i++) {
                    unsigned char c = (unsigned char)tmp[i];
                    if (c == 0xC2 && i + 2 < len && (unsigned char)tmp[i + 1] == 0xA7) { i += 2; continue; }   // colour code
                    if (c >= 32 && c < 127) g_offName[o++] = (char)c;
                }
                g_offName[o] = 0;
            }
        }
        if (!g_offName[0]) snprintf(g_offName, sizeof g_offName, "#%d:%d", id, aux);
    }
    char cs[8] = "";
    if (cnt > 1) snprintf(cs, sizeof cs, " x%d", cnt);
    int room = maxChars - (int)strlen(cs);
    if (room < 3) room = 3;
    int nl = (int)strlen(g_offName);
    if (nl > room) nl = room;
    snprintf(g_offLabel, sizeof g_offLabel, "%.*s%s", nl, g_offName, cs);
    return g_offLabel;
}

// ---------------- Edit panel actions ----------------
static void editPanelTap(int k) {
    int st = EP_STEPS[g_editStep & 3];
    if (k == 4) { g_editStep = (g_editStep + 1) & 3; return; }
    if (k == 6) { g_editMode = false; g_editSel = -1; g_on[B_EDIT] = false; g_cfgDirty = true; return; }
    int i = g_editSel, bx, by, bw, bh;
    if (i < 0 || i >= B_COUNT || !inGameRect(i, &bx, &by, &bw, &bh)) return;
    if (k == 5) { g_btnX[i] = -1; g_btnY[i] = -1; g_btnW[i] = 0; g_btnH[i] = 0; g_cfgDirty = true; return; }
    if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; if (i != B_MAP && i != B_JOY) { g_btnW[i] = bw; g_btnH[i] = bh; } }
    if (k < 4) {                                                    // move
        int nx = g_btnX[i] + (k == 0 ? -st : (k == 1 ? st : 0));
        int ny = g_btnY[i] + (k == 2 ? -st : (k == 3 ? st : 0));
        int maxX = (int)g_guiW - bw, maxY = (int)g_guiH - bh;
        g_btnX[i] = nx < 0 ? 0 : (nx > maxX ? maxX : nx);
        g_btnY[i] = ny < 0 ? 0 : (ny > maxY ? maxY : ny);
    } else if (i == B_JOY) {                                        // joystick: size 50..170
        int c = joySize() + ((k == 8 || k == 10 || k == 12) ? 2 * st : -2 * st);
        g_btnW[B_JOY] = c < 50 ? 50 : (c > 170 ? 170 : c);
    } else if (i == B_MAP) {                                        // map: cell size 1..3
        int c = mapCell() + ((k == 8 || k == 10 || k == 12) ? 1 : -1);
        g_btnW[B_MAP] = c < 1 ? 1 : (c > 3 ? 3 : c);
    } else {                                                        // button size
        int w = bw, h = bh;
        if (k == 7) w -= st; else if (k == 8) w += st;
        else if (k == 9) h -= st; else if (k == 10) h += st;
        else if (k == 11) { w -= st * 2; h -= st; } else if (k == 12) { w += st * 2; h += st; }
        g_btnW[i] = w < 16 ? 16 : (w > 120 ? 120 : w);
        g_btnH[i] = h < 9 ? 9 : (h > 40 ? 40 : h);
    }
    g_cfgDirty = true;
}

// ---------------- graphic buttons (pixel icons drawn with fills) ----------------
static const Color G_FACE   = {0.56f, 0.56f, 0.56f, 0.92f};
static const Color G_FACE_ON = {0.42f, 0.64f, 0.42f, 0.92f};
static const Color G_PRESS  = {0.36f, 0.36f, 0.36f, 0.95f};
static const Color G_HI     = {0.82f, 0.82f, 0.82f, 0.95f};
static const Color G_SH     = {0.26f, 0.26f, 0.26f, 0.95f};
static const Color G_BORDER = {0.06f, 0.06f, 0.06f, 0.95f};
static const Color G_ICON   = {0.05f, 0.05f, 0.05f, 1.f};
static const char* const IC_UP[8]    = {"...##...", "..####..", ".######.", "...##...", "...##...", "........", "########", "........"};   // jump: arrow + bar
static const char* const IC_PICK[8]  = {".#####..", "##...##.", "#.....#.", "....#...", "...#....", "..#.....", ".#......", "#......."};
static const char* const IC_SWORD[8] = {"......##", ".....###", "....###.", "#..###..", ".####...", "..##....", ".#.##...", "#...#..."};
static const char* const IC_CROSS[8] = {"...##...", "...##...", "........", "###..###", "###..###", "........", "...##...", "...##..."};
static const char* const IC_RUN[8]   = {"#...#...", "##..##..", ".##..##.", "..##..##", "..##..##", ".##..##.", "##..##..", "#...#..."};
static const char* const IC_CORNER[8] = {"#####...", "####....", "###.....", "##......", "#.......", "........", "........", "........"};
static const char* const IC_DOWN[8]  = {"........", "########", "........", "...##...", "...##...", ".######.", "..####..", "...##..."};   // sneak: bar + arrow

static bool icBit(const char* const* r, int rot, int x, int y) {
    switch (rot & 3) {
    case 1:  return r[7 - x][y] == '#';
    case 2:  return r[7 - y][7 - x] == '#';
    case 3:  return r[x][7 - y] == '#';
    default: return r[y][x] == '#';
    }
}
static void drawIcon(void* sr, const char* const* rows, int rot, int x0, int y0, int size) {
    int s = size / 8;
    if (s < 1) s = 1;
    int ox = x0 + (size - s * 8) / 2, oy = y0 + (size - s * 8) / 2;
    for (int y = 0; y < 8; y++) {
        int x = 0;
        while (x < 8) {
            if (!icBit(rows, rot, x, y)) { x++; continue; }
            int e = x;
            while (e < 8 && icBit(rows, rot, e, y)) e++;
            sr_fill(sr, ox + x * s, oy + y * s, ox + e * s, oy + (y + 1) * s, &G_ICON);
            x = e;
        }
    }
}
static void drawBevel(void* sr, int x, int y, int w, int h, const Color* face) {
    sr_fill(sr, x, y, x + w, y + h, &G_BORDER);
    sr_fill(sr, x + 1, y + 1, x + w - 1, y + h - 1, &G_SH);
    sr_fill(sr, x + 1, y + 1, x + w - 2, y + h - 2, &G_HI);
    sr_fill(sr, x + 2, y + 2, x + w - 2, y + h - 2, face);
}
static const Color G_JBASE = {0.40f, 0.48f, 0.40f, 0.92f};
static void drawJoy(void* sr, int x, int y, int w) {
    drawBevel(sr, x, y, w, w, &G_JBASE);
    int k = w / 4;
    float jx = g_joyX, jy = g_joyY;
    int kx = x + (w - k) / 2 + (int)(jx * (w - k) / 2), ky = y + (w - k) / 2 + (int)(jy * (w - k) / 2);
    drawBevel(sr, kx, ky, k, k, (jx != 0.f || jy != 0.f) ? &G_PRESS : &G_FACE);
}
static void drawGfxBtn(void* sr, int i, int x, int y, int w, int h) {
    bool held = (g_holdMask >> i) & 1u;
    bool on = i == B_AUTOSPR && g_on[B_AUTOSPR];
    drawBevel(sr, x, y, w, h, held ? &G_PRESS : (on ? &G_FACE_ON : &G_FACE));
    int sz = (w < h ? w : h) - 8;
    if (sz < 8) sz = 8;
    const char* const* ic = IC_UP; int rot = 0;
    switch (i) {
    case B_ATTACK: ic = IC_SWORD; break;
    case B_PLACE:  ic = IC_CROSS; break;
    case B_SNEAK:  ic = IC_DOWN; break;
    case B_BREAK:  ic = IC_PICK; break;
    case B_AUTOSPR: ic = IC_RUN; break;
    default: break;
    }
    drawIcon(sr, ic, rot, x + (w - sz) / 2, y + (h - sz) / 2, sz);
}
// Attack button: hits only what the crosshair is on if it is an entity (mob / player). Never touches blocks.
static void doAttack(void* client) {
    if (!g_localPlayer || !entity_getLevel || !mc_getGameMode || !gm_attack || !level_getHitResult || !client) return;
    void* level = entity_getLevel(g_localPlayer);
    void* gm = mc_getGameMode(client);
    if (!level || !gm) return;
    if (lp_swing) lp_swing(g_localPlayer);                          // arm swing like a normal attack
    const char* hr = (const char*)level_getHitResult(level);
    if (!hr || *(const int*)hr != 1) return;                         // 1 = the crosshair is on an entity
    void* ent = *(void* const*)(hr + 0x20);
    if ((uintptr_t)ent < 0x10000 || ent == g_localPlayer) return;
    gm_attack(gm, g_localPlayer, ent);
}
static volatile bool g_ownCall = false;                              // true while WE call the game's press functions

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
        if (i == B_JOY) { drawJoy(sr, x, y, w); continue; }
        if (isGfxBtn(i)) { drawGfxBtn(sr, i, x, y, w, h); continue; }
        bool action = (i == B_VIEW || i == B_Q || i == B_CLOCK);
        sr_fill(sr, x, y, x + w, y + h, (!action && g_on[i]) ? &C_BTN_ON : &C_BTN_BG);
        const char* label = SHORTL[i];
        char clk[32];
        if (i == B_CLOCK) { clockText(clk, sizeof clk); label = clk; }
        else if (i == B_OFFHAND) label = offhandLabel(w / 6 - 1);
        else if (i == B_ZOOM) { snprintf(clk, sizeof clk, g_zoomLevel ? "ZOOM %d" : "ZOOM", g_zoomLevel); label = clk; }
        int tw = (int)strlen(label) * 6;
        text(SL_GBTN + i, sr, font, label, x + (w - tw) / 2, y + (h - 8) / 2 + 1, &C_OFF, i == B_CLOCK ? 500 : 0);
    }

    // edit panel
    if (g_editMode) {
        int ex, ey, ew, eh;
        tbRect(0, &ex, &ey, &ew, &eh);
        char info[64];
        int bx, by, bw, bh;
        if (g_editSel >= 0 && g_editSel < B_COUNT && inGameRect(g_editSel, &bx, &by, &bw, &bh))
            snprintf(info, sizeof info, "EDIT %s  x%d y%d  %dx%d  step %d", g_editSel == B_MAP ? "MAP" : SHORTL[g_editSel], bx, by, bw, bh, EP_STEPS[g_editStep & 3]);
        else
            snprintf(info, sizeof info, "EDIT: tap a button, drag it, or use the arrows");
        text(SL_EDIT + 14, sr, font, info, (int)g_guiW / 2 - (int)strlen(info) * 3, ey - 12, &C_YELLOW, 120);
        for (int k = 0; k < EP_N; k++) {
            int x, y, w, h;
            tbRect(k, &x, &y, &w, &h);
            sr_fill(sr, x, y, x + w, y + h, &C_BG);
            char lb[12];
            if (k == 4) snprintf(lb, sizeof lb, "ST %d", EP_STEPS[g_editStep & 3]); else snprintf(lb, sizeof lb, "%s", EP_LABEL[k]);
            text(SL_EDIT + k, sr, font, lb, x + (w - (int)strlen(lb) * 6) / 2, y + 3, k == 6 ? &C_ON : (k == 5 ? &C_RED : &C_OFF));
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
static const int OPT_ROW0 = 44, OPT_ROW_H = 14, OPT_PER_COL = 12, OPT_COLS = 3;
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
    {1, B_EDIT,    "Edit Button"},
    {1, B_CLOCK,   "Clock (time/day)"},
    {1, B_ZOOM,    "Zoom Button"},
    {1, B_OFFHAND, "Offhand Button"},
    {1, B_LIGHT,   "Full Bright"},
    {1, B_NOCLOUD, "No Clouds"},
    {1, B_NOSTAR,  "No Stars"},
    {1, B_CLEAR,   "Clear Weather"},
    {1, B_OUTLINE, "Outline Selection"},
    {1, B_AUTOSPR, "Sprint Button"},
    {1, B_NOBOB,   "No View Bobbing"},
    {1, B_SMOOTH,  "Smooth Camera"},
    {1, B_NOPART,  "No Particles"},
    {1, B_NOHURT,  "No Hurt Cam"},
    {3, -1,        "Reset Button Layout"},
    {0, -1, ""},
    {0, -1, "TOUCH"},
    {5, -1,        "Touch Controls  >"},
    {0, -1, ""},
    {0, -1, ""},
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
    case 5:  *on = false; snprintf(buf, n, "OPEN"); break;
    case 4:  *on = true;  snprintf(buf, n, "ON"); break;
    default:
        *on = g_on[R.idx];
        snprintf(buf, n, *on ? "ON" : "OFF");
        break;
    }
}

// ---------------- Touch Controls page (inside the LP tab of Options) ----------------
static const int CTRL_PRESET[3] = {1, 3, 0};
static const char* CTRL_NAME[3] = {"Joystick and tap to interact", "Joystick and crosshair aiming", "Off (the game's own controls)"};
static const char* CTRL_DESC[3][2] = {
    {"Own joystick moves you, tap the screen to interact.", "Own Jump, Sneak and Sprint buttons."},
    {"Screen only turns the camera. Sword hits mobs/players,", "pickaxe breaks, crosshair places / uses."},
    {"No own control buttons. Nothing is changed.", ""}};
static const Color CP_SKY = {0.47f, 0.64f, 0.95f, 1.f};
static const Color CP_GND = {0.38f, 0.62f, 0.25f, 1.f};

static bool ctrlRect(int kind, int idx, int* x, int* y, int* w, int* h) {
    int lx = (int)g_optW - 182;
    switch (kind) {
    case 0: *x = OPT_CX0 + 4; *y = OPT_CY0 + 3; *w = 46; *h = 13; return true;                       // back
    case 1: *x = lx; *y = 44 + idx * 25; *w = 176; *h = 23; return idx >= 0 && idx < 3;               // radio
    case 2: *x = lx; *y = 146 + idx * 15; *w = 176; *h = 13; return idx >= 0 && idx < 3;              // switches
    case 3: *x = lx; *y = 194; *w = 176; *h = 18; return true;                                        // SELECT
    }
    return false;
}
static void ctrlTap(float gx, float gy) {
    int x, y, w, h;
    ctrlRect(0, 0, &x, &y, &w, &h);
    if (gx >= x && gx < x + w && gy >= y && gy < y + h) { g_ctrlPage = false; return; }
    for (int i = 0; i < 3; i++) {
        ctrlRect(1, i, &x, &y, &w, &h);
        if (gx >= x && gx < x + w && gy >= y && gy < y + h) { g_ctrlSel = CTRL_PRESET[i]; return; }
    }
    for (int i = 0; i < 3; i++) {
        ctrlRect(2, i, &x, &y, &w, &h);
        if (gx >= x && gx < x + w && gy >= y && gy < y + h) { if (i == 0) g_hideNative = !g_hideNative; else if (i == 1) g_dpFlipX = !g_dpFlipX; else g_dpFlipY = !g_dpFlipY; g_cfgDirty = true; return; }
    }
    ctrlRect(3, 0, &x, &y, &w, &h);
    if (gx >= x && gx < x + w && gy >= y && gy < y + h) g_ctrlApply = g_ctrlSel + 1;
}
static void drawCtrlPage(void* sr, void* font) {
    int x, y, w, h;
    ctrlRect(0, 0, &x, &y, &w, &h);
    sr_fill(sr, x, y, x + w, y + h, &OPT_ROW);
    text(SL_CTRL + 1, sr, font, "< BACK", x + 5, y + 3, &C_OFF);
    text(SL_CTRL + 0, sr, font, "TOUCH CONTROLS - pick a control mode", x + w + 8, y + 3, &C_CYAN);
    for (int i = 0; i < 3; i++) {
        ctrlRect(1, i, &x, &y, &w, &h);
        bool sel = g_ctrlSel == CTRL_PRESET[i];
        sr_fill(sr, x, y, x + w, y + h, &OPT_ROW);
        sr_fill(sr, x + 5, y + 7, x + 13, y + 15, sel ? &OPT_SWON : &OPT_SWOF);
        text(SL_CTRL + 2 + i, sr, font, CTRL_NAME[i], x + 19, y + 3, &C_OFF);
        if (g_ctrlMode == CTRL_PRESET[i]) text(SL_CTRL + 6, sr, font, "in use", x + 19, y + 13, &C_ON);
    }
    static const char* FL[3] = {"Hide game's own buttons", "Extra flip left / right", "Extra flip forward / back"};
    static const int FLS[3] = {7, 8, 20}, FSS[3] = {9, 10, 21};
    for (int i = 0; i < 3; i++) {
        ctrlRect(2, i, &x, &y, &w, &h);
        bool on = i == 0 ? g_hideNative : (i == 1 ? g_dpFlipX : g_dpFlipY);
        sr_fill(sr, x, y, x + w, y + h, &OPT_ROW);
        text(SL_CTRL + FLS[i], sr, font, FL[i], x + 4, y + 3, &C_OFF);
        sr_fill(sr, x + w - 34, y + 1, x + w - 3, y + h - 1, on ? &OPT_SWON : &OPT_SWOF);
        text(SL_CTRL + FSS[i], sr, font, on ? "ON" : "OFF", x + w - 28, y + 3, &C_OFF);
    }
    ctrlRect(3, 0, &x, &y, &w, &h);
    sr_fill(sr, x, y, x + w, y + h, &OPT_SWON);
    text(SL_CTRL + 11, sr, font, "SELECT", x + (w - 36) / 2, y + 5, &C_OFF);

    // preview of the selected mode
    int lx = (int)g_optW - 182;
    int px = OPT_CX0 + 8, py = 44, pw = lx - 8 - px, ph = 100;
    sr_fill(sr, px, py, px + pw, py + ph, &CP_SKY);
    sr_fill(sr, px, py + ph * 6 / 10, px + pw, py + ph, &CP_GND);
    int m = g_ctrlSel;
    if (m == 1 || m == 3) {                                  // game joystick
        drawBevel(sr, px + 10, py + ph - 52, 40, 40, &G_SH);
        drawBevel(sr, px + 22, py + ph - 40, 16, 16, &G_FACE);
    }
    int rx = px + pw - 28;
    if (m == 3) {
        drawGfxBtn(sr, B_PLACE,  rx, py + 6, 22, 22);
        drawGfxBtn(sr, B_JUMP,   rx, py + 32, 22, 22);
        drawGfxBtn(sr, B_SNEAK,  rx, py + 58, 22, 22);
        drawGfxBtn(sr, B_ATTACK, rx - 26, py + 6, 22, 22);
        drawGfxBtn(sr, B_BREAK,  rx - 26, py + 32, 22, 22);
        drawGfxBtn(sr, B_AUTOSPR, rx - 26, py + 58, 22, 22);
    } else if (m == 1) {
        drawGfxBtn(sr, B_JUMP,   rx, py + 32, 22, 22);
        drawGfxBtn(sr, B_SNEAK,  rx, py + 58, 22, 22);
        drawGfxBtn(sr, B_AUTOSPR, rx - 26, py + 40, 22, 22);
    } else {
        text(SL_CTRL + 15, sr, font, "(game controls)", px + pw / 2 - 45, py + 40, &C_OFF);
    }
    int di = 2;
    for (int i = 0; i < 3; i++) if (CTRL_PRESET[i] == m) di = i;
    text(SL_CTRL + 12, sr, font, CTRL_DESC[di][0], px, py + ph + 6, &C_OFF);
    text(SL_CTRL + 13, sr, font, CTRL_DESC[di][1], px, py + ph + 17, &C_OFF);
    char dbg[96];
    snprintf(dbg, sizeof dbg, "tick %d same %d | tap %d hit %d | atk %d plc %d jmp %d dpd %d joy %d", g_dbgTick, g_dbgSame, g_dbgTouch, g_dbgHit, g_dbgAtk, g_dbgPlace, g_dbgJump, g_dbgDpad, g_dbgJoy);
    text(SL_CTRL + 16, sr, font, dbg, px, py + ph + 43, &C_OFF);
    snprintf(dbg, sizeof dbg, "sym %d%d%d%d cal fwd %d side %d (%d,%d)", mc_attackPress ? 1 : 0, mc_placePress ? 1 : 0, lp_setSneaking ? 1 : 0, (mc_getInput && mih_getMoveTurnInput) ? 1 : 0, g_yaFwd, g_xaRight, (int)(g_calF * 10), (int)(g_calS * 10));
    text(SL_CTRL + 17, sr, font, dbg, px, py + ph + 54, &C_OFF);
    text(SL_CTRL + 14, sr, font, "Buttons can be moved / resized with the Edit button.", px, py + ph + 30, &C_YELLOW);
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
    if (g_ctrlPage) { drawCtrlPage(sr, font); return; }              // the page has its own title row
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
    case B_ZOOM:                                          // camera zoom: 3 steps, then back to normal
        if (!opt_setFov || !opt_getFov) break;
        if (g_zoomLevel == 0) g_zoomOrigFov = opt_getFov(opts);
        g_zoomLevel = (g_zoomLevel + 1) % 4;
        g_on[i] = g_zoomLevel != 0;
        opt_setFov(opts, g_zoomLevel ? ZOOM_FOV[g_zoomLevel - 1] : (g_zoomOrigFov > 0.f ? g_zoomOrigFov : 70.f));
        break;
    case B_OFFHAND:                                       // swap the held item with the offhand slot
        offhandSwap(client);
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
    case B_FREECAM:
        setFreecam(!g_freecam, client, opts);
        break;
    case B_EDIT:                                          // edit mode: drag / resize the in-game buttons
        g_editMode = !g_editMode;
        g_editSel = -1;
        g_on[B_EDIT] = g_editMode;
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

    if (g_frame - g_lastOptFrame > 10) { g_optPage = false; g_ctrlPage = false; }

    // Touch Controls: apply a preset chosen on the page, safety release, press / release edges
    int ap = __atomic_exchange_n(&g_ctrlApply, 0, __ATOMIC_ACQ_REL);
    if (ap > 0) {
        int m = ap - 1;
        static const int GFX[6] = {B_ATTACK, B_PLACE, B_JUMP, B_SNEAK, B_BREAK, B_AUTOSPR};
        for (int k = 0; k < 6; k++) {
            int i = GFX[k];
            bool want = false;
            if (m == 1) want = (i == B_JUMP || i == B_SNEAK || i == B_AUTOSPR);
            else if (m == 3) want = true;
            g_btnOn[i] = want;
        }
        g_btnOn[B_JOY] = (m == 1 || m == 3);
        g_ctrlMode = m;
        g_holdMask = 0; g_dpadMask = 0; g_joyX = 0.f; g_joyY = 0.f;
        g_yaFwd = 0; g_xaRight = 0; g_calF = 0.f; g_calS = 0.f;      // learn the directions again
        g_cfgDirty = true;
    }
    {   // learn which way the move input really moves the player (compares real movement with the look direction)
        static float lastX = 0.f, lastZ = 0.f;
        static bool haveLast = false;
        float cy = g_cmdYa, cx = g_cmdXa;
        bool driving = (fabsf(cy) > 0.5f || fabsf(cx) > 0.5f) && (g_yaFwd == 0 || g_xaRight == 0);
        if (driving && g_localPlayer && entity_getPos && entity_getRotation) {
            const float* p = entity_getPos(g_localPlayer);
            float rot[2] = {0.f, 0.f};
            entity_getRotation(rot, g_localPlayer);
            if (p) {
                if (haveLast) {
                    float dx = p[0] - lastX, dz = p[2] - lastZ;
                    if (dx * dx + dz * dz < 4.f) {
                        float yaw = rot[g_yawIdx & 1] * 0.0174532925f;
                        float fwx = -sinf(yaw), fwz = cosf(yaw);       // forward on the ground
                        float rtx = -cosf(yaw), rtz = -sinf(yaw);      // right
                        if (fabsf(cy) > 0.5f) g_calF += (dx * fwx + dz * fwz) * (cy > 0.f ? 1.f : -1.f);
                        if (fabsf(cx) > 0.5f) g_calS += (dx * rtx + dz * rtz) * (cx > 0.f ? 1.f : -1.f);
                    }
                }
                lastX = p[0]; lastZ = p[2]; haveLast = true;
            }
        } else haveLast = false;
        if (g_yaFwd == 0 && fabsf(g_calF) > 1.2f) { g_yaFwd = g_calF > 0.f ? 1 : -1; g_cfgDirty = true; }
        if (g_xaRight == 0 && fabsf(g_calS) > 1.2f) { g_xaRight = g_calS > 0.f ? 1 : -1; g_cfgDirty = true; }
    }
    {
        static unsigned prevHold = 0;
        static bool sneakWas = false;
        if (g_frame - g_lastHudFrame > 5 || g_editMode || !g_localPlayer) {   // no HUD (menu, edit, no world): nothing may stay pressed
            g_holdMask = 0; g_dpadMask = 0; g_joyX = 0.f; g_joyY = 0.f;
            for (int s = 0; s < 16; s++) { g_holdBtn[s] = -1; g_dpadCell[s] = -1; }
        }
        unsigned hm = g_holdMask, ch = hm ^ prevHold;
        static int atkNext = 0;
        if ((hm >> B_ATTACK) & 1u) {                                  // tap = one hit, hold = repeated hits
            if (((ch >> B_ATTACK) & 1u) || g_frame >= atkNext) { doAttack(client); g_dbgAtk++; atkNext = g_frame + 24; }
        }
        if ((ch >> B_BREAK) & 1u) {                                   // pickaxe: hold to break blocks
            g_ownCall = true;
            if ((hm >> B_BREAK) & 1u) { if (mc_attackPress) mc_attackPress(client); }
            else if (mc_attackRelease) mc_attackRelease(client);
            g_ownCall = false;
        }
        if ((ch >> B_PLACE) & 1u) {
            g_ownCall = true;
            if ((hm >> B_PLACE) & 1u) { if (mc_placePress) mc_placePress(client); g_dbgPlace++; }
            else if (mc_placeRelease) mc_placeRelease(client);
            g_ownCall = false;
        }
        prevHold = hm;
        bool sn = ((hm >> B_SNEAK) & 1u) || ((g_dpadMask >> 4) & 1u);
        if (sn != sneakWas && g_localPlayer && lp_setSneaking) lp_setSneaking(g_localPlayer, sn);
        sneakWas = sn;
    }

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

static void releaseHold(int s) {
    int i = g_holdBtn[s];
    if (i == B_JOY) {
        g_joyX = 0.f; g_joyY = 0.f;
    } else if (i >= 0) {
        __atomic_and_fetch(&g_holdMask, ~(1u << i), __ATOMIC_ACQ_REL);
    }
    g_holdBtn[s] = -1;
}
static void joyUpdate(float gx, float gy) {
    int bx, by, bw, bh;
    if (!inGameRect(B_JOY, &bx, &by, &bw, &bh)) return;
    float r = bw * 0.5f;
    float dx = (gx - (bx + r)) / r, dy = (gy - (by + r)) / r;
    float len = __builtin_sqrtf(dx * dx + dy * dy);
    if (len > 1.f) { dx /= len; dy /= len; len = 1.f; }
    if (len < 0.14f) { dx = 0.f; dy = 0.f; }                       // dead zone
    g_joyX = dx; g_joyY = dy;
}
static void my_feed(char b, char down, short x, short y, int id) {
    int s = id & 15;
    bool press = (b != 0 && down == 1);
    bool release = (b != 0 && down == 0);
    float inv = invScale();
    float gx = x * inv, gy = y * inv;

    if (g_cap[s]) {
        if (press) { releaseHold(s); g_cap[s] = false; g_drag[s] = -1; }   // a new touch with the same id
        else {
            if (g_holdBtn[s] >= 0) {                                  // a held game button (attack / place / jump / sneak / d-pad)
                if (b == 0 && g_holdBtn[s] == B_JOY) joyUpdate(gx, gy);
                if (release) { releaseHold(s); g_cap[s] = false; g_drag[s] = -1; }
                return;
            }
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
            g_optPage = true; g_ctrlPage = false;
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            return;
        }
        if (g_optPage && g_ctrlPage && gx >= OPT_CX0 && gy >= OPT_CY0 && gy < OPT_CY1) {   // Touch Controls page
            ctrlTap(gx, gy);
            g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1;
            return;
        }
        if (g_optPage) {
            if (hit >= 0) {
                const OptRow& R = OPT_TABLE[hit];
                if (R.kind == 1)      __atomic_fetch_or(&g_pending2, 1u << R.idx, __ATOMIC_ACQ_REL);   // show / hide the button
                else if (R.kind == 2) __atomic_fetch_or(&g_pending, 1u << R.idx, __ATOMIC_ACQ_REL);   // feature on / off
                else if (R.kind == 3) __atomic_fetch_or(&g_pending, 1u << R_RESET, __ATOMIC_ACQ_REL); // reset layout
                else if (R.kind == 5) { g_ctrlSel = g_ctrlMode; g_ctrlPage = true; }                  // open Touch Controls
            }
            if (hit >= 0 || hit == -3) { g_cap[s] = true; g_capFrame[s] = g_frame; g_drag[s] = -1; return; }
            g_optPage = false; g_ctrlPage = false;         // another tab / Back: close our page, game handles the tap
        }
    }
    if (optVisible) { orig_feed(b, down, x, y, id); return; }   // no HUD buttons on the Options screen

    bool hudShown = g_frame - g_lastHudFrame <= 2;
    if (press) g_dbgTouch++;
    if (hudShown && press) {
        if (g_editMode) {
            for (int k = 0; k < EP_N; k++) {                        // edit panel
                int tx, ty, tw, th;
                if (!tbRect(k, &tx, &ty, &tw, &th) || gx < tx || gx >= tx + tw || gy < ty || gy >= ty + th) continue;
                editPanelTap(k);
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
                if (g_btnX[i] < 0) { g_btnX[i] = bx; g_btnY[i] = by; if (i != B_MAP && i != B_JOY) { g_btnW[i] = bw; g_btnH[i] = bh; } }
                g_drag[s] = i;
                g_grabX[s] = gx - bx;
                g_grabY[s] = gy - by;
            } else if (isHoldBtn(i)) {                              // hold buttons: down now, up on release
                g_drag[s] = -1;
                g_holdBtn[s] = i;
                g_dbgHit++;
                if (i == B_JOY) joyUpdate(gx, gy);
                else __atomic_or_fetch(&g_holdMask, 1u << i, __ATOMIC_ACQ_REL);
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
    if (!g_on[B_NAMES] || !g_localPlayer || !g_client) return;
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
    if (g_freecam && self && self == g_player) {
        if (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngComp, 480) || inRange(lr, g_rngFrom, 164)) {
            ret->x += g_camOff.x;
            ret->y += g_camOff.y;
            ret->z += g_camOff.z;
        }
    }
    return ret;
}

// Freecam camera rotation: only the CAMERA's rotation is replaced (the body keeps its own).
static float* (*orig_irot)(float*, void*, float) = nullptr;
__attribute__((noinline)) static float* my_irot(float* ret, void* self, float a) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    orig_irot(ret, self, a);
    if (g_freecam && g_fcLook && self && self == g_player &&
        (inRange(lr, g_rngMove, 2720) || inRange(lr, g_rngPrep, 456))) {
        // moveCameraToPlayer = the picture; EntityRenderDispatcher::prepare = the direction text/name tags face (billboard).
        ret[0] = g_fcRot[0];
        ret[1] = g_fcRot[1];
        return ret;
    }
    return ret;
}

// While freecam is on the player does not walk: the movement the player would
// have made is applied to the camera instead. Joystick/D-pad = horizontal,
// jump button = up (only upward motion is used, so gravity never pulls the
// camera down), sneak button = down (handled in my_update).
static void (*orig_lpmove)(void*, const float*) = nullptr;
static void my_lpmove(void* self, const float* d) {
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
    bool fc = g_freecam && self && self == g_player;
    if (fc && g_fcLook && entity_getRotation && entity_setRot) {
        float* rot = g_fcRot;
        float before[2] = {0.f, 0.f}, after[2] = {0.f, 0.f};
        entity_getRotation(before, self);
        orig_lpturn(self, v, b);
        entity_getRotation(after, self);
        {
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
static void installLookHook() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!g_gameLib || !orig_irot || !entity_getRotation || !entity_setRot) return;
    bool t = hookSym(g_gameLib, "_ZN11LocalPlayer15localPlayerTurnERK4Vec2b", (void*)my_lpturn, (void**)&orig_lpturn, false);
    g_fcLook = t && orig_lpturn;
}

// ---- own jump / sneak / D-pad: written into the game's move input after MoveInputHandler::tick ----
// LocalPlayer::updateAi reads (decoded): input+4 = strafe, input+8 = forward, input+0x43 = jumping; +0x4e = sneak flag.
// The signs of the two axes are NOT verified: the "Flip" switches in Touch Controls fix a mirrored D-pad.
static void (*orig_miTick)(void*, void*) = nullptr;
static void my_miTick(void* self, void* player) {
    unsigned hm = g_holdMask, dp = g_dpadMask;
    bool sneak = ((hm >> B_SNEAK) & 1u) || ((dp >> 4) & 1u);
    static bool sneakWas = false;
    g_dbgTick++;
    if (sneak || sneakWas) *((char*)self + 0x4e) = sneak ? 1 : 0;     // before: tick may use it itself
    orig_miTick(self, player);
    char* mi = (char*)self;
    if (g_client && mc_getInput && mih_getMoveTurnInput) {              // the object the game's player code really reads
        void* ih = mc_getInput(g_client);
        void* m = ih ? mih_getMoveTurnInput(ih) : nullptr;
        if ((uintptr_t)m > 0x10000) mi = (char*)m;
    }
    if (mi == (char*)self) g_dbgSame++;
    if (sneak || sneakWas) mi[0x4e] = sneak ? 1 : 0;
    sneakWas = sneak;
    float jx = g_joyX, jy = g_joyY;
    if ((dp & 0x1EFu) || jx != 0.f || jy != 0.f) {                      // D-pad (the centre cell only sneaks) or joystick
        float fx = 0.f, fy = 0.f;
        for (int k = 0; k < 9; k++) {
            if (!((dp >> k) & 1u) || k == 4) continue;
            int col = k % 3, row = k / 3;
            fx += col == 0 ? -1.f : (col == 2 ? 1.f : 0.f);
            fy += row == 0 ? 1.f : (row == 2 ? -1.f : 0.f);
        }
        if (fx > 1.f) { fx = 1.f; }
        if (fx < -1.f) { fx = -1.f; }
        if (fy > 1.f) { fy = 1.f; }
        if (fy < -1.f) { fy = -1.f; }
        if (fx != 0.f && fy != 0.f) { fx *= 0.70710678f; fy *= 0.70710678f; }
        if (jx != 0.f || jy != 0.f) { fx = jx; fy = -jy; g_dbgJoy++; }   // joystick wins, analog
        // learned signs (until learned: xa>0 = left, ya>0 = forward, like Java Edition)
        float xa = fx * (g_xaRight == 1 ? 1.f : -1.f) * (g_dpFlipX ? -1.f : 1.f);
        float ya = fy * (g_yaFwd == -1 ? -1.f : 1.f) * (g_dpFlipY ? -1.f : 1.f);
        *(float*)(mi + 4) = xa;
        *(float*)(mi + 8) = ya;
        g_cmdXa = xa; g_cmdYa = ya;
    } else { g_cmdXa = 0.f; g_cmdYa = 0.f; }
    if ((hm >> B_JUMP) & 1u) { mi[0x43] = 1; g_dbgJump++; }
    if (dp & 0x1EFu) g_dbgDpad++;
}

// In crosshair mode a tap or hold on the empty screen must only turn the camera (the game's own handlers are skipped).
static void (*orig_buildAtk)(void*) = nullptr;
static void my_buildAtk(void* self) { if (g_ctrlMode == 3 && !g_ownCall) return; orig_buildAtk(self); }
static void (*orig_destroyAtk)(void*) = nullptr;
static void my_destroyAtk(void* self) { if (g_ctrlMode == 3 && !g_ownCall) return; orig_destroyAtk(self); }

// ---- hide the game's own touch buttons (D-pad, jump, sneak ...) while one of our presets is on ----
static void (*orig_glyphRender)(void*, void*) = nullptr;
static void my_glyphRender(void* self, void* ctx) {
    if (g_ctrlMode != 0 && g_hideNative) return;
    orig_glyphRender(self, ctx);
}
static void (*orig_glyphTick)(void*, void*, void*, int) = nullptr;
static void my_glyphTick(void* self, void* q, void* res, int a) {
    if (g_ctrlMode != 0 && g_hideNative) return;                  // hidden buttons must not react to touches either
    orig_glyphTick(self, q, res, a);
}

// ---- feature hooks: skip the original function when the toggle is on ----
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

    for (int s = 0; s < 16; s++) { g_holdBtn[s] = -1; g_dpadCell[s] = -1; }
    resetButtons();
    cfgInitPath();
    cfgLoad();
    if (g_guardPath[0]) {                                  // an earlier start died while enabling the game's commands
        FILE* gf = fopen(g_guardPath, "r");
        if (gf) { fclose(gf); g_nativeCmdDisabled = true; LOG("game commands disabled (crash guard)"); }
    }

    if (g_nameGuardPath[0]) {                              // an earlier start died while looking up an item name
        FILE* ng = fopen(g_nameGuardPath, "r");
        if (ng) { fclose(ng); g_nameDisabled = true; LOG("item names disabled (crash guard)"); }
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
    hookSym(game, "_ZN15MinecraftClient30handleBuildOrAttackButtonPressEv", (void*)my_buildAtk, (void**)&orig_buildAtk, false);
    hookSym(game, "_ZN15MinecraftClient32handleDestoryOrAttackButtonPressEv", (void*)my_destroyAtk, (void**)&orig_destroyAtk, false);
    hookSym(game, "_ZN16MoveInputHandler4tickER11LocalPlayer", (void*)my_miTick, (void**)&orig_miTick, false);
    hookSym(game, "_ZNK23TouchGlyphButtonControl6renderER18InputRenderContext", (void*)my_glyphRender, (void**)&orig_glyphRender, false);
    hookSym(game, "_ZN23TouchGlyphButtonControl4tickER15InputEventQueueR17TouchPointResultsi", (void*)my_glyphTick, (void**)&orig_glyphTick, false);
    hookSym(game, "_ZN13LevelRenderer12renderCloudsEf", (void*)my_clouds, (void**)&orig_clouds, false);
    hookSym(game, "_ZN13LevelRenderer12_renderStarsEf", (void*)my_stars,  (void**)&orig_stars,  false);
    hookSym(game, "_ZNK7Weather12getRainLevelEf",       (void*)my_getRain,   (void**)&orig_getRain,   false);
    hookSym(game, "_ZNK7Weather9isRainingEv",           (void*)my_isRaining, (void**)&orig_isRaining, false);
    hookSym(game, "_ZN19ServerCommandParser14executeCommandERKSsSs", (void*)my_exec, (void**)&orig_exec, false);
    hookSym(game, "_ZNK6Entity23getInterpolatedRotationEf", (void*)my_irot, (void**)&orig_irot, false);
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
