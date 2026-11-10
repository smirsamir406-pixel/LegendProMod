# MCPE 0.15.10 FPS module (ARMv7)

This is an FPS measurement module for the uploaded MCPE 0.15.10 APK's `armeabi-v7a` ABI. It is designed to be loaded by a compatible native mod/extension loader in the same process as `libminecraftpe.so`.

## Build

Use an Android NDK with `armeabi-v7a` support:

```sh
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
```

Output: `libs/armeabi-v7a/libmcpe01510_fps.so`.

## Required integration point

The target APK contains `lib/armeabi-v7a/libminecraftpe.so` (ELF32 ARM). The module checks that this library is already loaded, but **does not automatically hook the game renderer**. To obtain real in-game FPS, the mod loader or a source-level integration must call `mcpe_fps_on_frame()` exactly once after each successfully presented frame. Read the live value with `mcpe_fps_get_current()` or the exported `mcpe_fps_extension_get_fps()`.

There is no safe, verified renderer callback/signature in the supplied binary metadata alone. Do not call guessed C++ symbols: C++ mangled names do not provide complete function prototypes, object layouts, or thread/lifetime guarantees. This module therefore avoids patching `libminecraftpe.so` or guessing offsets. It will compile as a companion `.so`, but it will not show an overlay or report meaningful FPS until connected to a verified frame callback and a UI renderer.

## Loader contract

After `libminecraftpe.so` has loaded, the loader should resolve and call:

```cpp
int (*init)() = (int (*)()) dlsym(module, "mcpe_fps_extension_init");
```

Then call `mcpe_fps_on_frame()` from the verified present-frame callback. The exported C API is declared in `include/mcpe_fps.h`.

## Compatibility notes

- Target ABI: `armeabi-v7a` (32-bit ARM), matching the uploaded APK.
- Target game: MCPE 0.15.10 binary supplied in this conversation.
- Requires a native extension loader or an APK integration that loads this module in-process. Merely copying the `.so` into an APK does not load it.
- Build not executed here because an Android NDK toolchain is not available in this environment.
