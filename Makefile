# Makefile for building Lua
# See ../doc/readme.html for installation and customization instructions.

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Your platform. See PLATS for possible values.
PLAT= guess

CC= gcc -std=c11 -pipe
CFLAGS= -O3 -funroll-loops -fomit-frame-pointer -ffunction-sections -fdata-sections -fstrict-aliasing -g0 -DNDEBUG -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wimplicit-function-declaration -D_GNU_SOURCE

AR= ar rcu
RANLIB= ranlib
RM= rm -f
UNAME= uname

SYSCFLAGS= -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE
override CFLAGS+= $(SYSCFLAGS)
SYSLDFLAGS=
SYSLIBS=

MYCFLAGS=
MYLDFLAGS=
MYLIBS=
MYOBJS= 

# Combine flags for linker
LDFLAGS= $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS= -lm $(SYSLIBS) $(MYLIBS) -L./asmjit/build -lasmjit -lstdc++

# Special flags for compiler modules; -Os reduces code size.
CMCFLAGS= 


# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

PLATS= guess aix bsd c89 freebsd generic ios linux macosx mingw posix solaris

LUA_A=	liblua.a
CORE_O= lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o ltm.o lundump.o lvm.o lzio.o lobfuscate.o lthread.o lstruct.o lnamespace.o lbigint.o lsuper.o jit_backend.o
LIB_O= lauxlib.o lbaselib.o lcorolib.o ldblib.o liolib.o lmathlib.o loadlib.o loslib.o lstrlib.o ltablib.o lutf8lib.o linit.o json_parser.o lboolib.o lbitlib.o lptrlib.o ludatalib.o lvmlib.o lclass.o ltranslator.o lsmgrlib.o logtable.o sha256.o aes.o crc.o lthreadlib.o libhttp.o lfs.o lproclib.o lvmpro.o
BASE_O= $(CORE_O) $(LIB_O) $(MYOBJS)

LUA_T=	lxclua
LUA_O=	lua.o

LUAC_T=	luac
LUAC_O=	luac.o

LBCDUMP_T=	lbcdump
LBCDUMP_O=	lbcdump.o

ALL_O= $(BASE_O) $(LUA_O) $(LUAC_O) $(LBCDUMP_O)
ALL_T= $(LUA_A) $(LUA_T) $(LUAC_T) $(LBCDUMP_T)
ALL_A= $(LUA_A)

# Targets start here.
default: $(PLAT)

all:	$(ALL_T)

o:	$(ALL_O)

a:	$(ALL_A)

$(LUA_A): $(BASE_O)
	$(AR) $@ $(BASE_O) $(if $(findstring .dll,$(LUA_A)),$(LDFLAGS) $(LIBS))
	$(RANLIB) $@

$(LUA_T): $(LUA_O) $(LUA_A)
	g++ -o $@ $(LDFLAGS) $(LUA_O) $(LUA_A) $(LIBS)

$(LUAC_T): $(LUAC_O) $(LUA_A)
	g++ -o $@ $(LDFLAGS) $(LUAC_O) $(LUA_A) $(LIBS)

$(LBCDUMP_T): $(LBCDUMP_O)
	$(CC) -o $@ $(LDFLAGS) $(LBCDUMP_O)

$(WEBSERVER_A): $(WEBSERVER_O) $(LUA_A)
	$(CC) -shared -o $@ $(LDFLAGS) $(WEBSERVER_O) $(LUA_A) $(LIBS) -lws2_32

test:
	./$(LUA_T) -v

clean:
	$(RM) $(ALL_T) $(ALL_O)
	$(RM) lxclua.exe luac.exe lbcdump.exe lua55.dll
	$(RM) *.o *.a *.dll *.js *.wasm lxclua_standalone.html

depend:
	@$(CC) $(CFLAGS) -MM l*.c

echo:
	@echo "PLAT= $(PLAT)"
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "LDFLAGS= $(LDFLAGS)"
	@echo "LIBS= $(LIBS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"
	@echo "UNAME= $(UNAME)"

# Convenience targets for popular platforms.
ALL= all

help:
	@echo "Do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"
	@echo "See doc/readme.html for complete instructions."

guess:
	@echo Guessing `$(UNAME)`
	@$(MAKE) `$(UNAME)`

AIX aix:
	$(MAKE) $(ALL) CC="xlc" CFLAGS="-O2 -DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-ldl" SYSLDFLAGS="-brtl -bexpall"

bsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-Wl,-E"

c89:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_C89" CC="gcc -std=c89"
	@echo ''
	@echo '*** C89 does not guarantee 64-bit integers for Lua.'
	@echo '*** Make sure to compile all external Lua libraries'
	@echo '*** with LUA_USE_C89 to ensure consistency'
	@echo ''

FreeBSD NetBSD OpenBSD freebsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_READLINE -I/usr/include/edit" SYSLIBS="-Wl,-E -ledit" CC="cc"

generic: $(ALL)

ios:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_IOS"

Linux linux:
	$(MAKE) $(ALL) CC="gcc -std=c11" CFLAGS="-O2 -fPIC -DNDEBUG -D_DEFAULT_SOURCE" SYSCFLAGS="-DLUA_USE_LINUX" SYSLIBS="-Wl,-E -ldl -lm -lpthread" SYSLDFLAGS="-s"
	strip --strip-unneeded $(LUA_T) $(LUAC_T) || true

termux:
	$(MAKE) $(ALL) CC="clang -std=c23" CFLAGS="-O2 -fPIC -DNDEBUG" SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_DLOPEN" SYSLIBS="-ldl -lm" SYSLDFLAGS="-Wl,--build-id -fuse-ld=lld"
	strip --strip-unneeded $(LUA_T) $(LUAC_T) || true

Darwin macos macosx:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_MACOSX -DLUA_USE_READLINE" SYSLIBS="-lreadline"

mingw:
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=lua55.dll" "LUA_T=lxclua.exe" \
	"AR=$(CC) -shared -o" "RANLIB=strip --strip-unneeded" \
	"SYSCFLAGS=-DLUA_BUILD_AS_DLL -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=$(MYLIBS)" "SYSLDFLAGS=-s -lwininet -lws2_32" \
	"MYOBJS=$(MYOBJS)" lxclua.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblua.a" "LUAC_T=luac.exe" \
	"SYSCFLAGS=-DLUA_BUILD_AS_DLL -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=$(MYLIBS)" "SYSLDFLAGS=-s -lwininet -lws2_32" \
	luac.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LBCDUMP_T=lbcdump.exe" "SYSLDFLAGS=-s -lwininet -lws2_32" lbcdump.exe

mingw-static:
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblua.a" "LUA_T=lxclua.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=$(MYLIBS)" "SYSLDFLAGS=-s -lwininet -lws2_32" \
	"MYOBJS=$(MYOBJS)" lxclua.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblua.a" "LUAC_T=luac.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=$(MYLIBS)" "SYSLDFLAGS=-s -lwininet -lws2_32" \
	luac.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LBCDUMP_T=lbcdump.exe" "SYSLDFLAGS=-s -lwininet -lws2_32" lbcdump.exe


posix:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX"

SunOS solaris:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN -D_REENTRANT" SYSLIBS="-ldl"

# WebAssembly (Emscripten)
# 需要先安装 Emscripten SDK: https://emscripten.org/docs/getting_started/downloads.html
# 使用方法: make wasm
# Emscripten 3.0.0+ 支持 C23 (底层 Clang 18+)
# Emscripten SDK 路径配置（Windows需要.bat扩展名）
EMSDK_PATH= E:/Soft/Proje/LXCLUA-NCore/emsdk/upstream/emscripten
EMCC= $(EMSDK_PATH)/emcc.bat
EMAR= $(EMSDK_PATH)/emar.bat
EMRANLIB= $(EMSDK_PATH)/emranlib.bat

wasm:
	$(MAKE) $(ALL) CC="$(EMCC) -std=c23" \
	"CFLAGS=-O3 -DNDEBUG -fno-exceptions -DLUA_32BITS=0" \
	"SYSCFLAGS=-DLUA_USE_LONGJMP -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN" \
	"SYSLIBS=" \
	"AR=$(EMAR) rcu" \
	"RANLIB=$(EMRANLIB)" \
	"LUA_T=lxclua.js" \
	"LUAC_T=luac.js" \
	"LBCDUMP_T=lbcdump.js" \
	"LDFLAGS=-sWASM=1 -sSINGLE_FILE=1 -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS -sMODULARIZE=1 -sEXPORT_NAME=LuaModule -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1 -sINVOKE_RUN=0 --closure 1"

# WASM 最小化版本（无文件系统，更小体积）
wasm-minimal:
	$(MAKE) $(ALL) CC="$(EMCC) -std=c23" \
	"CFLAGS=-Os -DNDEBUG -fno-exceptions -DLUA_32BITS=0" \
	"SYSCFLAGS=-DLUA_USE_LONGJMP -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN" \
	"SYSLIBS=" \
	"AR=$(EMAR) rcu" \
	"RANLIB=$(EMRANLIB)" \
	"LUA_T=lxclua.js" \
	"LUAC_T=luac.js" \
	"LBCDUMP_T=lbcdump.js" \
	"LDFLAGS=-sWASM=1 -sEXPORTED_RUNTIME_METHODS=ccall,cwrap -sMODULARIZE=1 -sEXPORT_NAME=LuaModule -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 --closure 1 -sINVOKE_RUN=0"

# Targets that do not create files (not all makes understand .PHONY).
.PHONY: all $(PLATS) help test clean default o a depend echo wasm wasm-minimal release mingw-release linux-release macos-release wasm-release termux-release

# 发行版打包配置
RELEASE_NAME= lxclua
RELEASE_VERSION= $(shell date +%Y%m%d_%H%M%S)
RELEASE_DIR= release
SIGNER= DifierLine

# Windows MinGW 发行版 (使用tar，MSYS2自带)
mingw-release: mingw
	@echo "Creating Windows release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Windows x64 (MinGW)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua.exe luac.exe lbcdump.exe lua55.dll $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-windows-x64-$(RELEASE_VERSION).zip -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-windows-x64-$(RELEASE_VERSION).zip"

# Linux 发行版
linux-release: linux
	@echo "Creating Linux release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Linux x64" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luac lbcdump $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-linux-x64-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-linux-x64-$(RELEASE_VERSION).tar.gz"

# macOS 发行版
macos-release: macosx
	@echo "Creating macOS release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: macOS (Darwin)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luac lbcdump $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-macos-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-macos-$(RELEASE_VERSION).tar.gz"

# Termux/Android 发行版
termux-release: termux
	@echo "Creating Termux release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Android (Termux)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luac lbcdump $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-termux-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-termux-$(RELEASE_VERSION).tar.gz"

# WebAssembly 发行版
wasm-release: wasm
	@echo "Creating WASM release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: WebAssembly" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua.js luac.js lbcdump.js $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-wasm-$(RELEASE_VERSION).zip -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-wasm-$(RELEASE_VERSION).zip"

# 通用发行版打包
release:
	@echo "Creating release package..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp $(LUA_T) $(LUAC_T) $(LBCDUMP_T) $(RELEASE_DIR)/ 2>/dev/null || true
	@cp $(LUA_A) $(RELEASE_DIR)/ 2>/dev/null || true
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/
	@tar -caf $(RELEASE_NAME)-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-$(RELEASE_VERSION).tar.gz"

# Compiler modules may use special flags.
llex.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c llex.c

lparser.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c lparser.c

lcode.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c lcode.c

# DO NOT DELETE

lapi.o: lapi.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lstring.h \
 ltable.h lundump.h lvm.h
lauxlib.o: lauxlib.c lprefix.h lua.h luaconf.h lauxlib.h llimits.h
lbaselib.o: lbaselib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lcode.o: lcode.c lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lgc.h lstring.h ltable.h lvm.h lopnames.h
lcorolib.o: lcorolib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lctype.o: lctype.c lprefix.h lctype.h lua.h luaconf.h llimits.h
ldblib.o: ldblib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
ldebug.o: ldebug.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lcode.h llex.h lopcodes.h lparser.h \
 ldebug.h ldo.h lfunc.h lstring.h lgc.h ltable.h lvm.h
ldo.o: ldo.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lparser.h lstring.h ltable.h lundump.h lvm.h
ldump.o: ldump.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lgc.h ltable.h lundump.h
lfunc.o: lfunc.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h
lgc.o: lgc.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h lstring.h ltable.h
linit.o: linit.c lprefix.h lua.h luaconf.h lualib.h lauxlib.h llimits.h
lfs.o: lfs.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h
liolib.o: liolib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
llex.o: llex.c lprefix.h lua.h luaconf.h lctype.h llimits.h ldebug.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lgc.h llex.h lparser.h \
 lstring.h ltable.h
lmathlib.o: lmathlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lmem.o: lmem.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h
loadlib.o: loadlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lobject.o: lobject.c lprefix.h lua.h luaconf.h lctype.h llimits.h \
 ldebug.h lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h \
 lvm.h
lopcodes.o: lopcodes.c lprefix.h lopcodes.h llimits.h lua.h luaconf.h \
 lobject.h
loslib.o: loslib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
lparser.o: lparser.c lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lfunc.h lstring.h lgc.h ltable.h
lstate.o: lstate.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h llex.h \
 lstring.h ltable.h
lstring.o: lstring.c lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h
lstrlib.o: lstrlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltable.o: ltable.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
ltablib.o: ltablib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltm.o: ltm.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
lua.o: lua.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
luac.o: luac.c lprefix.h lua.h luaconf.h lauxlib.h lapi.h llimits.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldebug.h lopcodes.h lopnames.h \
 lundump.h
lundump.o: lundump.c lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lstring.h lgc.h \
 ltable.h lundump.h
lutf8lib.o: lutf8lib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lvm.o: lvm.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lstring.h ltable.h lvm.h ljumptab.h
lzio.o: lzio.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h

# (end of Makefile)

setup_asmjit:
	@if [ ! -d "asmjit" ]; then \
		echo "Cloning asmjit..."; \
		git clone https://github.com/asmjit/asmjit.git; \
	fi
	@if [ ! -d "asmjit/build" ]; then \
		echo "Building asmjit..."; \
		mkdir -p asmjit/build; \
		cd asmjit/build && cmake .. -DASMJIT_STATIC=ON -DASMJIT_TEST=OFF && make -j4; \
	fi

jit_backend.o: setup_asmjit
	g++ -O3 -fomit-frame-pointer -g0 -DNDEBUG -D_GNU_SOURCE -I./asmjit -c jit_backend.cpp
