LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := minecraftpe
LOCAL_SRC_FILES := libminecraftpe.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := substrate
LOCAL_SRC_FILES := libsubstrate.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := client
LOCAL_SRC_FILES := main.cpp
LOCAL_SHARED_LIBRARIES := minecraftpe substrate
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
