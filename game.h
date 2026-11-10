// Resolved game functions for MCPE 0.15.10 (armeabi-v7a, libminecraftpe.so).
// All symbols are exported in .dynsym, so dlsym finds them by mangled name.
#pragma once
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

struct Color { float r, g, b, a; };

// MCPE 0.15.10 uses the old libstdc++ (gnustl) COW std::string: the object is a
// single pointer to the char data, with a {length, capacity, refcount} header
// right before it. We fake one so we don't depend on any STL ABI.
// refcount is huge, so the game never frees it.
struct FakeStr {
    size_t len, cap;
    int ref;
    char data[96];
    char* ptr;
    explicit FakeStr(const char* s) {
        size_t n = strlen(s);
        if (n > 94) n = 94;
        memcpy(data, s, n);
        data[n] = 0;
        len = n; cap = n; ref = 0x3fffffff;
        ptr = data;
    }
    const void* ref_() const { return &ptr; }   // acts as `const std::string&`
};

struct Game {
    // MinecraftClient
    void  (*client_update)(void*);
    void* (*client_getOptions)(void*);
    void* (*client_getLocalPlayer)(void*);
    void* (*client_getFont)(void*);
    void  (*client_toggleThirdPerson)(void*);
    // Options
    void  (*opt_setGamma)(void*, float);
    float (*opt_getGamma)(void*);
    void  (*opt_setFov)(void*, float);
    float (*opt_getFov)(void*);
    // Items / armor
    void* (*mob_getArmor)(void*, int);
    bool  (*item_isNull)(void*);
    int   (*item_getMaxDamage)(void*);
    int   (*item_getDamageValue)(void*);
    // Rendering
    void* (*sr_singleton)();
    void  (*sr_fill)(void*, int, int, int, int, const Color*);
    void  (*sr_drawString)(void*, void*, const void*, int, int, const Color*);
    float* invGuiScale;
    // Hook targets
    void* t_update;
    void* t_screenRender;
    void* t_multitouchFeed;

    bool load(void* h) {
        #define S(var, name) var = (decltype(var))dlsym(h, name); if (!(var)) return false;
        S(client_getOptions,       "_ZN15MinecraftClient10getOptionsEv")
        S(client_getLocalPlayer,   "_ZN15MinecraftClient14getLocalPlayerEv")
        S(client_getFont,          "_ZNK15MinecraftClient7getFontEv")
        S(client_toggleThirdPerson,"_ZN15MinecraftClient22_toggleThirdPersonViewEv")
        S(opt_setGamma,            "_ZN7Options8setGammaEf")
        S(opt_getGamma,            "_ZNK7Options8getGammaEv")
        S(opt_setFov,              "_ZN7Options14setFieldOfViewEf")
        S(opt_getFov,              "_ZNK7Options14getFieldOfViewEv")
        S(mob_getArmor,            "_ZNK3Mob8getArmorE9ArmorSlot")
        S(item_isNull,             "_ZNK12ItemInstance6isNullEv")
        S(item_getMaxDamage,       "_ZNK12ItemInstance12getMaxDamageEv")
        S(item_getDamageValue,     "_ZNK12ItemInstance14getDamageValueEv")
        S(sr_singleton,            "_ZN14ScreenRenderer9singletonEv")
        S(sr_fill,                 "_ZNK14ScreenRenderer4fillEiiiiRK5Color")
        S(sr_drawString,           "_ZN14ScreenRenderer10drawStringEP4FontRKSsiiRK5Color")
        S(invGuiScale,             "_ZN7GuiData11InvGuiScaleE")
        S(t_update,                "_ZN15MinecraftClient6updateEv")
        S(t_screenRender,          "_ZN16InGamePlayScreen6renderER13ScreenContext")
        S(t_multitouchFeed,        "_ZN10Multitouch4feedEccssi")
        #undef S
        return true;
    }
};
