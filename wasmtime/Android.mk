LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := wasmtime
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
LOCAL_MODULE_SUFFIX := .a

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_SRC_FILES := wasmtime-v45.0.0-aarch64-android-c-api/lib/libwasmtime.a
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/wasmtime-v45.0.0-aarch64-android-c-api/include
endif
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_SRC_FILES := wasmtime-v45.0.0-aarch64-android-c-api/lib/libwasmtime.a
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/wasmtime-v45.0.0-aarch64-android-c-api/include
endif
ifeq ($(TARGET_ARCH_ABI), x86_64)
    LOCAL_SRC_FILES := wasmtime-v45.0.0-x86_64-android-c-api/lib/libwasmtime.a
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/wasmtime-v45.0.0-x86_64-android-c-api/include
endif
ifeq ($(TARGET_ARCH_ABI), x86)
    LOCAL_SRC_FILES := wasmtime-v45.0.0-x86_64-android-c-api/lib/libwasmtime.a
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/wasmtime-v45.0.0-x86_64-android-c-api/include
endif

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

include $(PREBUILT_STATIC_LIBRARY)
