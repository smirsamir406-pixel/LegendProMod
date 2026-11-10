// MCPE 0.15.10 (ARM32) native mod - exported symbol ভিত্তিক, কোনো hardcoded offset নেই
// Build: ndk-build (Android.mk দেখো) | Hook: Substrate (MSHookFunction)
#include <jni.h>
#include <dlfcn.h>
#include <atomic>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "MCPEMod", __VA_ARGS__)

extern "C" void MSHookFunction(void* symbol, void* replace, void** result);

// ---- game-এর টাইপগুলো (opaque) ----
struct Entity; struct Level; struct Player; struct LocalPlayer;
struct Vec3 { float x, y, z; };

// ---- ফাংশন পয়েন্টার (dlsym দিয়ে লোড হবে) ----
static void  (*Player_setPlayerGameType)(Player*, int)           = nullptr; // 0=Survival 1=Creative
static void  (*Mob_addEffect)(void*, const void*)                = nullptr;
static void  (*MobEffectInstance_ctor)(void*, int, int, int)     = nullptr; // id, duration(tick), amplifier
static void  (*Mob_removeAllEffects)(void*)                      = nullptr;
static void  (*Mob_heal)(void*, int)                             = nullptr;
static void  (*Mob_setSpeed)(void*, float)                       = nullptr;
static void  (*Mob_jumpFromGround)(void*)                        = nullptr;
static void  (*Entity_setOnFire)(void*, int)                     = nullptr;
static Level*(*Entity_getLevel)(void*)                           = nullptr;
static void  (*Level_setTime)(Level*, int)                       = nullptr;

static void (*orig_normalTick)(LocalPlayer*) = nullptr;
static LocalPlayer* gPlayer = nullptr;

// ---- Java থেকে আসা কমান্ড (UI thread) -> game thread-এ চালানো ----
enum Cmd {
    CMD_NONE = 0,
    CMD_CREATIVE, CMD_SURVIVAL,
    CMD_DAY, CMD_NIGHT,
    CMD_HEAL, CMD_CLEAR_EFFECTS, CMD_EXTINGUISH,
    CMD_JUMP_BOOST, CMD_SPEED, CMD_NIGHT_VISION, CMD_REGEN,
    CMD_RESISTANCE, CMD_HASTE, CMD_STRENGTH, CMD_FIRE_RES,
    CMD_WATER_BREATH, CMD_INVISIBLE,
    CMD_FAST_WALK_ON, CMD_FAST_WALK_OFF,
    CMD_GOD_ON, CMD_GOD_OFF,
};
static std::atomic<int> gPending(CMD_NONE);
static bool gGod = false;
static bool gFastWalk = false;

static void giveEffect(int id, int amp) {
    if (!gPlayer || !Mob_addEffect || !MobEffectInstance_ctor) return;
    alignas(8) unsigned char buf[128] = {0};          // MobEffectInstance-এর জন্য নিরাপদ বড় বাফার
    MobEffectInstance_ctor(buf, id, 20 * 60 * 30, amp); // ৩০ মিনিট
    Mob_addEffect(gPlayer, buf);
}

static void runCommand(int c) {
    if (!gPlayer) return;
    switch (c) {
        case CMD_CREATIVE:  if (Player_setPlayerGameType) Player_setPlayerGameType((Player*)gPlayer, 1); break;
        case CMD_SURVIVAL:  if (Player_setPlayerGameType) Player_setPlayerGameType((Player*)gPlayer, 0); break;
        case CMD_DAY:   { Level* l = Entity_getLevel ? Entity_getLevel(gPlayer) : nullptr; if (l && Level_setTime) Level_setTime(l, 1000);  break; }
        case CMD_NIGHT: { Level* l = Entity_getLevel ? Entity_getLevel(gPlayer) : nullptr; if (l && Level_setTime) Level_setTime(l, 14000); break; }
        case CMD_HEAL:          if (Mob_heal) Mob_heal(gPlayer, 20); break;
        case CMD_CLEAR_EFFECTS: if (Mob_removeAllEffects) Mob_removeAllEffects(gPlayer); break;
        case CMD_EXTINGUISH:    if (Entity_setOnFire) Entity_setOnFire(gPlayer, 0); break;
        case CMD_SPEED:        giveEffect(1, 3);  break;
        case CMD_HASTE:        giveEffect(3, 3);  break;
        case CMD_STRENGTH:     giveEffect(5, 3);  break;
        case CMD_JUMP_BOOST:   giveEffect(8, 4);  break;
        case CMD_REGEN:        giveEffect(10, 4); break;
        case CMD_RESISTANCE:   giveEffect(11, 4); break;
        case CMD_FIRE_RES:     giveEffect(12, 0); break;
        case CMD_WATER_BREATH: giveEffect(13, 0); break;
        case CMD_INVISIBLE:    giveEffect(14, 0); break;
        case CMD_NIGHT_VISION: giveEffect(16, 0); break;
        case CMD_FAST_WALK_ON:  gFastWalk = true;  break;
        case CMD_FAST_WALK_OFF: gFastWalk = false; if (Mob_setSpeed) Mob_setSpeed(gPlayer, 0.1f); break;
        case CMD_GOD_ON:  gGod = true;  break;
        case CMD_GOD_OFF: gGod = false; break;
    }
}

// ---- প্রতি tick-এ চলে (game thread) ----
static void my_normalTick(LocalPlayer* self) {
    orig_normalTick(self);
    gPlayer = self;

    int c = gPending.exchange(CMD_NONE);
    if (c != CMD_NONE) runCommand(c);

    if (gGod && Mob_heal)         Mob_heal(self, 20);        // সবসময় ফুল হেলথ
    if (gFastWalk && Mob_setSpeed) Mob_setSpeed(self, 0.25f); // 0.1 = সাধারণ গতি
}

template <typename T> static void bind(void* h, const char* sym, T& fn) {
    fn = (T)dlsym(h, sym);
    LOG("%s -> %p", sym, (void*)fn);
}

// ---- JNI: Java বাটন এখান দিয়ে কল করবে ----
extern "C" JNIEXPORT void JNICALL
Java_com_example_mcpemod_Native_send(JNIEnv*, jclass, jint cmd) { gPending.store(cmd); }

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    void* h = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!h) { LOG("libminecraftpe.so পাওয়া যায়নি"); return JNI_VERSION_1_4; }

    bind(h, "_ZN6Player17setPlayerGameTypeE8GameType",          Player_setPlayerGameType);
    bind(h, "_ZN3Mob9addEffectERK17MobEffectInstance",          Mob_addEffect);
    bind(h, "_ZN17MobEffectInstanceC1Eiii",                     MobEffectInstance_ctor);
    bind(h, "_ZN3Mob16removeAllEffectsEv",                      Mob_removeAllEffects);
    bind(h, "_ZN3Mob4healEi",                                   Mob_heal);
    bind(h, "_ZN3Mob8setSpeedEf",                               Mob_setSpeed);
    bind(h, "_ZN3Mob14jumpFromGroundEv",                        Mob_jumpFromGround);
    bind(h, "_ZN6Entity9setOnFireEi",                           Entity_setOnFire);
    bind(h, "_ZN6Entity8getLevelEv",                            Entity_getLevel);
    bind(h, "_ZN5Level7setTimeEi",                              Level_setTime);

    void* tick = dlsym(h, "_ZN11LocalPlayer10normalTickEv");
    if (tick) MSHookFunction(tick, (void*)my_normalTick, (void**)&orig_normalTick);
    return JNI_VERSION_1_4;
}
