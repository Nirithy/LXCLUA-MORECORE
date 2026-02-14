LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := lua
LOCAL_CFLAGS := -std=c23 -O3 \
                -funroll-loops -fomit-frame-pointer \
                -ffunction-sections -fdata-sections \
                -fstrict-aliasing
LOCAL_CFLAGS += -g0 -DNDEBUG

# 极致性能构建配置
LOCAL_CFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wimplicit-function-declaration



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
	ljit.c\
	json_parser.c \
	lstruct.c \
	sha256.c \
	lobfuscate.c

LOCAL_CFLAGS += -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE

# 针对不同 ABI 设置架构优化
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


# 添加缺失的库依赖
LOCAL_LDLIBS += -llog -lz

include $(BUILD_STATIC_LIBRARY) 
