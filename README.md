# MCPE 0.15.10 client mod (libclient.so)

Buttons: VIEW (cycle camera), FPS, ARMOR (durability), LIGHT (fullbright), FOV (70/90/110/original).

## Build
1. `git clone --depth 1 https://github.com/jmpews/Dobby third_party/Dobby`
2. Push to GitHub: the workflow in `.github/workflows/build.yml` builds `libclient.so`
   (armeabi-v7a), or build locally with Android NDK + CMake (same flags as the workflow).

## Install into the APK
1. Put `libclient.so` in `lib/armeabi-v7a/` of the apk.
2. In MainActivity's static init (smali), AFTER the line that loads "minecraftpe", add:

       const-string v0, "client"
       invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V

3. Re-sign the apk and install.

## Debugging
`adb logcat -s MCClient` shows "hooks installed" and every tap as pixel + GUI coords,
so you can adjust BX/BY/BW/BH in src/main.cpp if the buttons look misplaced.

## Not verified yet (unverified assumptions, tweak after first test)
- ScreenRenderer::fill args are (x0, y0, x1, y1) - if boxes look wrong, try (x, y, w, h).
- Multitouch::feed semantics (button, down) - the swallow logic assumes down==1 is press.
- Mob::getArmor slot order 0..3 = head, chest, legs, boots.
- LIGHT sets gamma to 15; the original value is restored when toggled off.
