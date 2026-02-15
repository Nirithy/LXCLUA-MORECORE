LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := asmjit
LOCAL_CFLAGS := -std=c++17 -O3 -fomit-frame-pointer \
                -ffunction-sections -fdata-sections \
                -fstrict-aliasing -g0 -DNDEBUG
LOCAL_CFLAGS += -fno-exceptions -fno-rtti -fno-unwind-tables \
                -fno-asynchronous-unwind-tables

LOCAL_SRC_FILES := \
    asmjit/asmjit/core/archtraits.cpp \
    asmjit/asmjit/core/assembler.cpp \
    asmjit/asmjit/core/builder.cpp \
    asmjit/asmjit/core/codeholder.cpp \
    asmjit/asmjit/core/codewriter.cpp \
    asmjit/asmjit/core/compiler.cpp \
    asmjit/asmjit/core/constpool.cpp \
    asmjit/asmjit/core/cpuinfo.cpp \
    asmjit/asmjit/core/emithelper.cpp \
    asmjit/asmjit/core/emitter.cpp \
    asmjit/asmjit/core/emitterutils.cpp \
    asmjit/asmjit/core/environment.cpp \
    asmjit/asmjit/core/errorhandler.cpp \
    asmjit/asmjit/core/formatter.cpp \
    asmjit/asmjit/core/func.cpp \
    asmjit/asmjit/core/funcargscontext.cpp \
    asmjit/asmjit/core/globals.cpp \
    asmjit/asmjit/core/inst.cpp \
    asmjit/asmjit/core/instdb.cpp \
    asmjit/asmjit/core/jitallocator.cpp \
    asmjit/asmjit/core/jitruntime.cpp \
    asmjit/asmjit/core/logger.cpp \
    asmjit/asmjit/core/operand.cpp \
    asmjit/asmjit/core/osutils.cpp \
    asmjit/asmjit/core/ralocal.cpp \
    asmjit/asmjit/core/rapass.cpp \
    asmjit/asmjit/core/rastack.cpp \
    asmjit/asmjit/core/string.cpp \
    asmjit/asmjit/core/target.cpp \
    asmjit/asmjit/core/type.cpp \
    asmjit/asmjit/core/virtmem.cpp \
    asmjit/asmjit/support/arena.cpp \
    asmjit/asmjit/support/arenabitset.cpp \
    asmjit/asmjit/support/arenahash.cpp \
    asmjit/asmjit/support/arenalist.cpp \
    asmjit/asmjit/support/arenatree.cpp \
    asmjit/asmjit/support/arenavector.cpp \
    asmjit/asmjit/support/support.cpp

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_SRC_FILES += \
        asmjit/asmjit/arm/a64assembler.cpp \
        asmjit/asmjit/arm/a64builder.cpp \
        asmjit/asmjit/arm/a64compiler.cpp \
        asmjit/asmjit/arm/a64emithelper.cpp \
        asmjit/asmjit/arm/a64formatter.cpp \
        asmjit/asmjit/arm/a64func.cpp \
        asmjit/asmjit/arm/a64instapi.cpp \
        asmjit/asmjit/arm/a64instdb.cpp \
        asmjit/asmjit/arm/a64operand.cpp \
        asmjit/asmjit/arm/a64rapass.cpp \
        asmjit/asmjit/arm/armformatter.cpp \
        asmjit/asmjit/ujit/unicompiler_a64.cpp \
        asmjit/asmjit/ujit/vecconsttable.cpp
endif

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_SRC_FILES += \
        asmjit/asmjit/arm/a64assembler.cpp \
        asmjit/asmjit/arm/a64builder.cpp \
        asmjit/asmjit/arm/a64compiler.cpp \
        asmjit/asmjit/arm/a64emithelper.cpp \
        asmjit/asmjit/arm/a64formatter.cpp \
        asmjit/asmjit/arm/a64func.cpp \
        asmjit/asmjit/arm/a64instapi.cpp \
        asmjit/asmjit/arm/a64instdb.cpp \
        asmjit/asmjit/arm/a64operand.cpp \
        asmjit/asmjit/arm/a64rapass.cpp \
        asmjit/asmjit/arm/armformatter.cpp \
        asmjit/asmjit/ujit/unicompiler_a64.cpp \
        asmjit/asmjit/ujit/vecconsttable.cpp
endif

ifeq ($(TARGET_ARCH_ABI), x86_64)
    LOCAL_SRC_FILES += \
        asmjit/asmjit/x86/x86assembler.cpp \
        asmjit/asmjit/x86/x86builder.cpp \
        asmjit/asmjit/x86/x86compiler.cpp \
        asmjit/asmjit/x86/x86emithelper.cpp \
        asmjit/asmjit/x86/x86formatter.cpp \
        asmjit/asmjit/x86/x86func.cpp \
        asmjit/asmjit/x86/x86instapi.cpp \
        asmjit/asmjit/x86/x86instdb.cpp \
        asmjit/asmjit/x86/x86operand.cpp \
        asmjit/asmjit/x86/x86rapass.cpp \
        asmjit/asmjit/ujit/unicompiler_x86.cpp \
        asmjit/asmjit/ujit/vecconsttable.cpp
endif

ifeq ($(TARGET_ARCH_ABI), x86)
    LOCAL_SRC_FILES += \
        asmjit/asmjit/x86/x86assembler.cpp \
        asmjit/asmjit/x86/x86builder.cpp \
        asmjit/asmjit/x86/x86compiler.cpp \
        asmjit/asmjit/x86/x86emithelper.cpp \
        asmjit/asmjit/x86/x86formatter.cpp \
        asmjit/asmjit/x86/x86func.cpp \
        asmjit/asmjit/x86/x86instapi.cpp \
        asmjit/asmjit/x86/x86instdb.cpp \
        asmjit/asmjit/x86/x86operand.cpp \
        asmjit/asmjit/x86/x86rapass.cpp \
        asmjit/asmjit/ujit/unicompiler_x86.cpp \
        asmjit/asmjit/ujit/vecconsttable.cpp
endif

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/asmjit
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := lua
LOCAL_CFLAGS := -std=c23 -O3 \
                -funroll-loops -fomit-frame-pointer \
                -ffunction-sections -fdata-sections \
                -fstrict-aliasing
LOCAL_CFLAGS += -g0 -DNDEBUG

LOCAL_CFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wimplicit-function-declaration

LOCAL_CPPFLAGS := -std=c++17 -fno-rtti
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/asmjit

LOCAL_SRC_FILES := \
    aes.c\
    crc.c\
    lfs.c\
	lapi.c \
	lauxlib.c \
	lbigint.c\
	lbaselib.c \
	lboolib.c \
	lclass.c \
	lcode.c \
	lcorolib.c \
	lctype.c \
	ldblib.c \
	ldebug.c \
	ldo.c \
	ldump.c \
	lfunc.c \
	lgc.c \
	linit.c \
	liolib.c \
	llex.c \
	lmathlib.c \
	lmem.c \
	loadlib.c \
	lobject.c \
	lopcodes.c \
	loslib.c \
	lparser.c \
	lstate.c \
	lstring.c \
	lstrlib.c \
	ltable.c \
	libhttp.c\
	ltablib.c \
	ltm.c \
	lua.c \
	ltranslator.c \
	lundump.c \
	ludatalib.c \
	lutf8lib.c \
	lbitlib.c \
	lvmlib.c \
	lvm.c \
	lzio.c \
	lnamespace.c\
	lthread.c \
	lthreadlib.c \
	lproclib.c\
	lptrlib.c \
	lsmgrlib.c \
	llibc.c \
	lvmpro.c\
	logtable.c \
	json_parser.c \
	lsuper.c\
	lstruct.c \
	sha256.c \
	lobfuscate.c \
	jit_backend.cpp

LOCAL_CFLAGS += -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE

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

LOCAL_LDLIBS += -llog -lz
LOCAL_STATIC_LIBRARIES := asmjit

include $(BUILD_STATIC_LIBRARY)
