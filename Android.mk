LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := mcpemod
LOCAL_SRC_FILES := mod.cpp
LOCAL_LDLIBS    := -llog -ldl -L$(LOCAL_PATH) -lsubstrate
LOCAL_CPPFLAGS  := -std=c++11 -fno-rtti -fno-exceptions
include $(BUILD_SHARED_LIBRARY)
