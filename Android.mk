LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := mcpe01510_fps
LOCAL_SRC_FILES := ../src/fps_counter.cpp ../src/extension.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
LOCAL_CPPFLAGS := -std=c++11 -fno-exceptions -fno-rtti -fvisibility=hidden
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
