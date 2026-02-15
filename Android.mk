LOCAL_PATH := $(call my-dir)

################################################################################
# Module: libasmjit (Static Library)
################################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := libasmjit
LOCAL_CPP_EXTENSION := .cpp

# Include all C++ source files under asmjit/src
# User must clone asmjit into the asmjit directory first!
# e.g., git clone https://github.com/asmjit/asmjit.git
LOCAL_SRC_FILES := $(call all-cpp-files-under, asmjit/src)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/asmjit/src
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/asmjit/src

# C++ specific flags
LOCAL_CXXFLAGS := -std=c++17 -fexceptions -frtti
# Preprocessor definitions (shared)
LOCAL_CPPFLAGS := -DASMJIT_STATIC -DNDEBUG -DASMJIT_NO_FOREIGN

include $(BUILD_STATIC_LIBRARY)


################################################################################
# Module: liblua (Static Library - Core + Extensions)
################################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := liblua

# Core Lua and Extensions sources
LOCAL_SRC_FILES := \
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c \
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c \
    ltm.c lundump.c lvm.c lzio.c \
    lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c \
    loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c \
    lbitlib.c \
    lobfuscate.c lthread.c lstruct.c lnamespace.c lbigint.c lsuper.c \
    json_parser.c lboolib.c lptrlib.c ludatalib.c lvmlib.c lclass.c \
    ltranslator.c lsmgrlib.c logtable.c sha256.c aes.c crc.c lthreadlib.c \
    libhttp.c lfs.c lproclib.c lvmpro.c \
    jit_backend.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
                    $(LOCAL_PATH)/asmjit/src

# C-specific flags (Performance & Compatibility)
LOCAL_CFLAGS := -std=c11 -O3 \
                -funroll-loops -fomit-frame-pointer \
                -ffunction-sections -fdata-sections \
                -fstrict-aliasing \
                -g0 \
                -DLUA_USE_LINUX -DLUA_COMPAT_5_3 -D_GNU_SOURCE \
                -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE

# Disable exceptions/unwind for C to match Makefile
LOCAL_CFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables

# C++ specific flags (for jit_backend.cpp)
LOCAL_CXXFLAGS := -std=c++17 -fexceptions -frtti

# Preprocessor definitions (shared)
LOCAL_CPPFLAGS := -DASMJIT_STATIC -DNDEBUG

# Link against AsmJit
LOCAL_STATIC_LIBRARIES := libasmjit

# Architecture specific optimizations
ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS += -march=armv8-a
endif
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -march=armv7-a
endif
ifeq ($(TARGET_ARCH_ABI), x86_64)
    LOCAL_CFLAGS += -march=x86-64
endif
ifeq ($(TARGET_ARCH_ABI), x86)
    LOCAL_CFLAGS += -march=i686
endif

include $(BUILD_STATIC_LIBRARY)


################################################################################
# Module: lxclua (Executable - Lua Interpreter)
################################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := lxclua
LOCAL_SRC_FILES := lua.c

LOCAL_CFLAGS := -std=c11 -O3 -DNDEBUG -DLUA_USE_LINUX
LOCAL_STATIC_LIBRARIES := liblua libasmjit
LOCAL_LDLIBS := -llog -ldl -lm -lz

include $(BUILD_EXECUTABLE)


################################################################################
# Module: luac (Executable - Lua Compiler)
################################################################################
include $(CLEAR_VARS)

LOCAL_MODULE := luac
LOCAL_SRC_FILES := luac.c

LOCAL_CFLAGS := -std=c11 -O3 -DNDEBUG -DLUA_USE_LINUX
LOCAL_STATIC_LIBRARIES := liblua libasmjit
LOCAL_LDLIBS := -llog -ldl -lm -lz

include $(BUILD_EXECUTABLE)
