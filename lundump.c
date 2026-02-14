/*
** $Id: lundump.c $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define lundump_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstring.h"
#include "ltable.h"
#include "lundump.h"
#include "lzio.h"

#include "sha256.h"
#include "lobfuscate.h"

#include "stb_image.h"


#if !defined(luai_verifycode)
#define luai_verifycode(L,f)  /* empty */
#endif

/*
** Standard Lua constants
*/
#define LUAC_INT_STD	((lua_Integer)(-0x5678))
#define LUAC_NUM_STD	cast_num(-370.5)
#define LUAC_VERSION_STD 0x55
#define LUAC_INST_STD	0x12345678


typedef struct {
  lua_State *L;
  ZIO *Z;
  const char *name;
  int64_t timestamp;  /* 动态密钥：时间戳 */
  int opcode_map[NUM_OPCODES];  /* OPcode映射表 */
  int third_opcode_map[NUM_OPCODES];  /* 第三个OPcode映射表 */
  int string_map[256];  /* 字符串映射表（用于动态加密解密） */

  /* Standard Lua compatibility fields */
  Table *h;  /* list for string reuse */
  size_t offset;  /* current position relative to beginning of dump */
  lua_Unsigned nstr;  /* number of strings in the list */
  lu_byte fixed;  /* dump is fixed in memory */
  int is_standard; /* flag to indicate standard Lua bytecode */
  int force_standard; /* flag to force standard Lua bytecode */
} LoadState;


static l_noret error (LoadState *S, const char *why) {
  luaO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
  luaD_throw(S->L, LUA_ERRSYNTAX);
}


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
#define loadVector(S,b,n)	loadBlock(S,b,(n)*sizeof((b)[0]))

static void loadBlock (LoadState *S, void *b, size_t size) {
  if (luaZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");

}


#define loadVar(S,x)		loadVector(S,&x,1)


static lu_byte loadByte (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  return cast_byte(b);
}


static int64_t loadInt64 (LoadState *S) {
  uint64_t x = 0;
  for (int i = 0; i < 8; i++) {
    x |= ((uint64_t)loadByte(S)) << (i * 8);
  }
  return (int64_t)x;
}


static int32_t loadInt32 (LoadState *S) {
  uint32_t x = 0;
  for (int i = 0; i < 4; i++) {
    x |= ((uint32_t)loadByte(S)) << (i * 8);
  }
  return (int32_t)x;
}


static double loadDouble (LoadState *S) {
  int64_t i = loadInt64(S);
  double d;
  memcpy(&d, &i, 8);
  return d;
}


static size_t loadUnsigned (LoadState *S, size_t limit) {
  size_t x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte(S);
    if (x >= limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) == 0);
  return x;
}


static size_t loadSize (LoadState *S) {
  return loadUnsigned(S, MAX_SIZET);
}


static int loadInt (LoadState *S) {
  return cast_int(loadUnsigned(S, INT_MAX));
}


static lua_Number loadNumber (LoadState *S) {
  return (lua_Number)loadDouble(S);
}


static lua_Integer loadInteger (LoadState *S) {
  return (lua_Integer)loadInt64(S);
}


/*
** Load a nullable string into prototype 'p'.
*/
static TString *loadStringN (LoadState *S, Proto *p) {
  lua_State *L = S->L;
  TString *ts;
  size_t size = loadSize(S);
  if (size == 0)  /* no string? */
    return NULL;
  else if (--size <= LUAI_MAXSHORTLEN) {  /* short string? */
    /* 读取该字符串专用的时间戳 */
    loadVar(S, S->timestamp);
    
    /* 读取字符串映射表（用于解密） */
    for (int i = 0; i < 256; i++) {
      S->string_map[i] = loadByte(S);
    }
    
    /* 读取并验证字符串映射表的SHA-256哈希值（完整性验证） */
    uint8_t expected_hash[SHA256_DIGEST_SIZE];
    loadVector(S, expected_hash, SHA256_DIGEST_SIZE);
    /* 计算字符串映射表的SHA-256哈希 */
    uint8_t actual_hash[SHA256_DIGEST_SIZE];
    SHA256((uint8_t *)S->string_map, 256 * sizeof(int), actual_hash);
    /* 验证哈希值 */
    if (memcmp(actual_hash, expected_hash, SHA256_DIGEST_SIZE) != 0) {
      error(S, "string map integrity verification failed");
      return NULL;
    }
    
    /* 创建反向字符串映射表 */
    int reverse_string_map[256];
    for (int i = 0; i < 256; i++) {
      reverse_string_map[S->string_map[i]] = i;
    }
    
    char buff[LUAI_MAXSHORTLEN];
    loadVector(S, buff, size);  /* load encrypted string into buffer */
    
    // 对字符串进行解密，先使用时间戳XOR解密，再使用映射表解密
    for (size_t i = 0; i < size; i++) {
      /* 先使用时间戳进行XOR解密，再使用反向映射表解密 */
      unsigned char decrypted_char = buff[i] ^ ((char *)&S->timestamp)[i % sizeof(S->timestamp)];
      buff[i] = reverse_string_map[decrypted_char];
    }
    
    ts = luaS_newlstr(L, buff, size);  /* create string */
  }
  else {  /* long string */
    /* 读取该字符串专用的时间戳 */
    loadVar(S, S->timestamp);
    
    /* 读取字符串映射表（用于解密） */
    for (int i = 0; i < 256; i++) {
      S->string_map[i] = loadByte(S);
    }
    
    /* 读取并验证字符串映射表的SHA-256哈希值（完整性验证） */
    uint8_t expected_hash[SHA256_DIGEST_SIZE];
    loadVector(S, expected_hash, SHA256_DIGEST_SIZE);
    /* 计算字符串映射表的SHA-256哈希 */
    uint8_t actual_hash[SHA256_DIGEST_SIZE];
    SHA256((uint8_t *)S->string_map, 256 * sizeof(int), actual_hash);
    /* 验证哈希值 */
    if (memcmp(actual_hash, expected_hash, SHA256_DIGEST_SIZE) != 0) {
      error(S, "string map integrity verification failed");
      return NULL;
    }
    
    /* 创建反向字符串映射表 */
    int reverse_string_map[256];
    for (int i = 0; i < 256; i++) {
      reverse_string_map[S->string_map[i]] = i;
    }
    
    if (size >= 0xFF) {
      /* 长字符串：使用图片加密 */
      // 读取字符串内容的SHA-256哈希值（完整性验证）
      uint8_t expected_content_hash[SHA256_DIGEST_SIZE];
      loadVector(S, expected_content_hash, SHA256_DIGEST_SIZE);
      
      // 读取图像尺寸
      int width = loadInt(S);
      int height = loadInt(S);
      
      // 读取PNG数据长度
      size_t png_len = loadSize(S);
      
      // 分配内存 for PNG数据
      unsigned char *png_data = (unsigned char *)luaM_malloc_(S->L, png_len, 0);
      if (png_data == NULL) {
        error(S, "memory allocation failed for PNG data");
        return NULL;
      }
      
      // 读取PNG数据
      loadBlock(S, png_data, png_len);
      
      // 解码PNG to get encrypted data
      int img_width, img_height, img_channels;
      unsigned char *image_data = stbi_load_from_memory(png_data, (int)png_len, &img_width, &img_height, &img_channels, 1);
      if (image_data == NULL) {
        luaM_free_(S->L, png_data, png_len);
        error(S, "failed to decode PNG image");
        return NULL;
      }
      
      // 检查尺寸 match
      if (img_width != width || img_height != height) {
        stbi_image_free(image_data);
        luaM_free_(S->L, png_data, png_len);
        error(S, "PNG image dimensions mismatch");
        return NULL;
      }
      
      // 创建长字符串对象
      ts = luaS_createlngstrobj(L, size);  /* create string */
      setsvalue2s(L, L->top.p, ts);  /* anchor it */
      luaD_inctop(L);
      
      // 复制解密后的数据到字符串对象
      char *str = ts->contents;
      memcpy(str, image_data, size);
      
      // 对字符串进行解密，先使用时间戳XOR解密，再使用映射表解密
      for (size_t i = 0; i < size; i++) {
        /* 先使用时间戳进行XOR解密，再使用反向映射表解密 */
        unsigned char decrypted_char = str[i] ^ ((char *)&S->timestamp)[i % sizeof(S->timestamp)];
        str[i] = reverse_string_map[decrypted_char];
      }
      
      // 验证字符串内容的SHA-256哈希值（完整性验证）
      uint8_t actual_content_hash[SHA256_DIGEST_SIZE];
      SHA256((uint8_t *)str, size, actual_content_hash);
      if (memcmp(actual_content_hash, expected_content_hash, SHA256_DIGEST_SIZE) != 0) {
        error(S, "string content integrity verification failed");
        return NULL;
      }
      
      // 释放内存
      stbi_image_free(image_data);
      luaM_free_(S->L, png_data, png_len);
      
      L->top.p--;  /* pop string */
    } else {
      /* 普通长字符串：使用映射表解密 */
      ts = luaS_createlngstrobj(L, size);  /* create string */
      setsvalue2s(L, L->top.p, ts);  /* anchor it ('loadVector' can GC) */
      luaD_inctop(L);
      loadVector(S, ts->contents, size);  /* load encrypted string directly into final place */
      
      // 对长字符串进行解密，先使用时间戳XOR解密，再使用映射表解密
      char *str = ts->contents;
      for (size_t i = 0; i < size; i++) {
        /* 先使用时间戳进行XOR解密，再使用反向映射表解密 */
        unsigned char decrypted_char = str[i] ^ ((char *)&S->timestamp)[i % sizeof(S->timestamp)];
        str[i] = reverse_string_map[decrypted_char];
      }
      
      L->top.p--;  /* pop string */
    }
  }
  luaC_objbarrier(L, p, ts);
  return ts;
}


/*
** Load a non-nullable string into prototype 'p'.
*/
static TString *loadString (LoadState *S, Proto *p) {
  TString *st = loadStringN(S, p);
  if (st == NULL)
    error(S, "bad format for constant string");
  return st;
}


static void loadCode (LoadState *S, Proto *f) {
  int orig_size = loadInt(S);
  size_t data_size = orig_size * sizeof(Instruction);
  int i;

  /* 时间戳已在loadFunction开头读取，此处不再重复读取 */
  
  // Read OPcode映射表
  for (i = 0; i < NUM_OPCODES; i++) {
    S->opcode_map[i] = loadByte(S);
  }
  
  // Read third OPcode映射表
  for (i = 0; i < NUM_OPCODES; i++) {
    S->third_opcode_map[i] = loadByte(S);
  }
  
  // 读取并验证OPcode映射表的SHA-256哈希值（完整性验证）
  uint8_t expected_hash[SHA256_DIGEST_SIZE];
  loadVector(S, expected_hash, SHA256_DIGEST_SIZE);
  /* 合并两个映射表进行哈希计算 */
  int combined_map_size = NUM_OPCODES * 2;
  int *combined_map = (int *)luaM_malloc_(S->L, combined_map_size * sizeof(int), 0);
  if (combined_map == NULL) {
    error(S, "memory allocation failed for combined map");
    return;
  }
  memcpy(combined_map, S->opcode_map, NUM_OPCODES * sizeof(int));
  memcpy(combined_map + NUM_OPCODES, S->third_opcode_map, NUM_OPCODES * sizeof(int));
  /* 计算SHA-256哈希 */
  uint8_t actual_hash[SHA256_DIGEST_SIZE];
  SHA256((uint8_t *)combined_map, combined_map_size * sizeof(int), actual_hash);
  luaM_free_(S->L, combined_map, combined_map_size * sizeof(int));
  /* 验证哈希值 */
  if (memcmp(actual_hash, expected_hash, SHA256_DIGEST_SIZE) != 0) {
    error(S, "OPcode map integrity verification failed");
    return;
  }
  
  // Read image dimensions
  int width = loadInt(S);
  int height = loadInt(S);
  
  // Read PNG data length
  size_t png_len = loadSize(S);
  
  // Allocate memory for PNG data
  unsigned char *png_data = (unsigned char *)luaM_malloc_(S->L, png_len, 0);
  if (png_data == NULL) {
    error(S, "memory allocation failed for PNG data");
    return;
  }
  
  // Read PNG data
  loadBlock(S, png_data, png_len);
  
  // Decode PNG to get encrypted data
  int img_width, img_height, img_channels;
  unsigned char *image_data = stbi_load_from_memory(png_data, (int)png_len, &img_width, &img_height, &img_channels, 1);
  if (image_data == NULL) {
    luaM_free_(S->L, png_data, png_len);
    error(S, "failed to decode PNG image");
    return;
  }
  
  // Check dimensions match
  if (img_width != width || img_height != height) {
    stbi_image_free(image_data);
    luaM_free_(S->L, png_data, png_len);
    error(S, "PNG image dimensions mismatch");
    return;
  }
  
  // Allocate memory for original code
  f->code = luaM_newvectorchecked(S->L, orig_size, Instruction);
  f->sizecode = orig_size;
  
  // Decrypt data using XOR with timestamp as password (no decompression)
  /* Decrypt in-place in image_data buffer first */
  for (i = 0; i < (int)data_size; i++) {
    image_data[i] ^= ((char *)&S->timestamp)[i % sizeof(S->timestamp)];
  }

  /* Reconstruct Instructions from LE bytes (64-bit) */
  for (i = 0; i < orig_size; i++) {
    uint64_t inst = 0;
    for (int j = 0; j < 8; j++) {
      inst |= ((uint64_t)image_data[i*8 + j]) << (j * 8);
    }
    f->code[i] = (Instruction)inst;
  }
  
  // Free image and PNG data
  stbi_image_free(image_data);
  luaM_free_(S->L, png_data, png_len);
  
  // 应用反向OPcode映射，恢复原始OPcode
  // 首先创建第三个OPcode映射表的反向映射
  int reverse_third_opcode_map[NUM_OPCODES];
  for (i = 0; i < NUM_OPCODES; i++) {
    reverse_third_opcode_map[S->third_opcode_map[i]] = i;
  }
  
  // 然后应用反向映射恢复原始OPcode
  for (i = 0; i < orig_size; i++) {
    Instruction inst = f->code[i];
    OpCode op = GET_OPCODE(inst);
    /* 首先使用第三个OPcode映射表的反向映射恢复 */
    SET_OPCODE(inst, reverse_third_opcode_map[op]);
    /* 然后使用原始映射表恢复 */
    op = GET_OPCODE(inst);
    SET_OPCODE(inst, S->opcode_map[op]);
    f->code[i] = inst;
  }
}


static void loadFunction(LoadState *S, Proto *f, TString *psource);


static void loadConstants (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->k = luaM_newvectorchecked(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = loadByte(S);
    switch (t) {
      case LUA_VNIL:
        setnilvalue(o);
        break;
      case LUA_VFALSE:
        setbfvalue(o);
        break;
      case LUA_VTRUE:
        setbtvalue(o);
        break;
      case LUA_VNUMFLT:
        setfltvalue(o, loadNumber(S));
        break;
      case LUA_VNUMINT:
        setivalue(o, loadInteger(S));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR:
        setsvalue2n(S->L, o, loadString(S, f));
        break;
      default: lua_assert(0);
    }
  }
}


static void loadProtos (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->p = luaM_newvectorchecked(S->L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = luaF_newproto(S->L);
    luaC_objbarrier(S->L, f, f->p[i]);
    loadFunction(S, f->p[i], f->source);
  }
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
static void loadUpvalues (LoadState *S, Proto *f) {
  int i, n;
  n = loadInt(S);
  f->upvalues = luaM_newvectorchecked(S->L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)  /* make array valid for GC */
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {  /* following calls can raise errors */
    f->upvalues[i].instack = loadByte(S);
    f->upvalues[i].idx = loadByte(S);
    f->upvalues[i].kind = loadByte(S);
  }
  
  /* 增强的防导入验证机制 */
  int anti_import_count = loadInt(S);
  if (anti_import_count == 0x99) {  /* 检测防导入标记 */
    // 1. 读取并验证随机化的 upvalue 数据
    for (i = 0; i < 15; i++) {
      loadByte(S);  /* 读取随机的 instack */
      loadByte(S);  /* 读取随机的 idx */
      loadByte(S);  /* 读取随机的 kind */
    }
    
    // 2. 读取并验证加密的验证数据
    uint8_t validation_data[16];
    loadVector(S, validation_data, 16);
    
    // 使用时间戳解密验证数据
    uint8_t decrypted_validation[16];
    for (i = 0; i < 16; i++) {
      decrypted_validation[i] = validation_data[i] ^ ((uint8_t *)&S->timestamp)[i % sizeof(S->timestamp)];
    }
    
    // 验证数据完整性（检查是否有全零数据）
    int valid = 1;
    for (i = 0; i < 16; i++) {
      if (decrypted_validation[i] == 0) {
        valid = 0;
        break;
      }
    }
    if (!valid) {
      error(S, "invalid upvalue validation data");
    }
    
    // 3. 读取并验证基于 OPcode 映射表的混淆数据
    for (i = 0; i < 10; i++) {
      loadByte(S);  /* 读取基于 OPcode 映射表的 instack */
      loadByte(S);  /* 读取基于第三个 OPcode 映射表的 idx */
      loadByte(S);  /* 读取基于反向 OPcode 映射表的 kind */
    }
    
    // 4. 读取并验证 SHA-256 验证数据
    uint8_t sha_data[32];
    loadVector(S, sha_data, 32);
    
    // 计算基于时间戳的哈希值进行验证
    uint8_t expected_sha[32];
    SHA256((uint8_t *)&S->timestamp, sizeof(S->timestamp), expected_sha);
    
    // 验证 SHA-256 数据
    if (memcmp(sha_data, expected_sha, 32) != 0) {
      error(S, "invalid upvalue SHA-256 validation data");
    }
  } else if (anti_import_count > 0x70) {  /* 兼容旧的防导入标记 */
    /* 跳过特殊的upvalue数据 */
    /* 跳过第一轮：10个upvalue信息 */
    for (i = 0; i < 10; i++) {
      loadByte(S);  /* 跳过instack */
      loadByte(S);  /* 跳过idx */
      loadByte(S);  /* 跳过kind */
    }
    /* 跳过第二轮：5个upvalue信息 */
    for (i = 0; i < 5; i++) {
      loadByte(S);  /* 跳过instack */
      loadByte(S);  /* 跳过idx */
      loadByte(S);  /* 跳过kind */
    }
    /* 跳过第三轮：3个upvalue信息 */
    for (i = 0; i < 3; i++) {
      loadByte(S);  /* 跳过instack */
      loadByte(S);  /* 跳过idx */
      loadByte(S);  /* 跳过kind */
    }
  } else if (anti_import_count > 0) {  /* 处理旧的虚假数据 */
    /* 跳过虚假数据 */
    for (i = 0; i < anti_import_count; i++) {
      loadByte(S);  /* 跳过instack */
      loadByte(S);  /* 跳过idx */
      loadByte(S);  /* 跳过kind */
    }
  }
}


static void loadDebug (LoadState *S, Proto *f) {
  int i, n;
  n = loadInt(S);
  f->lineinfo = luaM_newvectorchecked(S->L, n, ls_byte);
  f->sizelineinfo = n;
  loadVector(S, f->lineinfo, n);
  n = loadInt(S);
  f->abslineinfo = luaM_newvectorchecked(S->L, n, AbsLineInfo);
  f->sizeabslineinfo = n;
  for (i = 0; i < n; i++) {
    f->abslineinfo[i].pc = loadInt(S);
    f->abslineinfo[i].line = loadInt(S);
  }
  n = loadInt(S);
  f->locvars = luaM_newvectorchecked(S->L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    f->locvars[i].varname = loadStringN(S, f);
    f->locvars[i].startpc = loadInt(S);
    f->locvars[i].endpc = loadInt(S);
  }
  n = loadInt(S);
  if (n != 0)  /* does it have debug information? */
    n = f->sizeupvalues;  /* must be this many */
  for (i = 0; i < n; i++)
    f->upvalues[i].name = loadStringN(S, f);
  /* 跳过虚假数据：跳过我们在dumpDebug函数中添加的虚假调试信息 */
  int fake_debug_count = loadInt(S);  /* 读取虚假调试信息的数量 */
  for (i = 0; i < fake_debug_count; i++) {
    loadInt(S);  /* 跳过虚假的PC值 */
    loadInt(S);  /* 跳过虚假的行号 */
  }
}


static void loadFunction (LoadState *S, Proto *f, TString *psource) {
  /* 首先读取时间戳，确保字符串解密时能正确使用 */
  loadVar(S, S->timestamp);
  
  f->numparams = loadByte(S);
  f->is_vararg = loadByte(S);
  f->maxstacksize = loadByte(S);
  f->difierline_mode = loadInt(S);  /* 新增：读取自定义标志 */

  f->difierline_pad = loadInt(S); /* Padding */

  f->linedefined = loadInt(S);
  f->lastlinedefined = loadInt(S);

  f->source = loadStringN(S, f);
  if (f->source == NULL)  /* no source in dump? */
    f->source = psource;  /* reuse parent's source */

  f->difierline_magicnum = loadInt(S);  /* 新增：读取自定义版本号 */
  loadVar(S, f->difierline_data);  /* 新增：读取自定义数据字段 */
  
  /* VM保护数据反序列化 */
  int has_vm_code = loadInt(S);
  if (has_vm_code) {
    int vm_size = loadInt(S);
    uint64_t encrypt_key;
    unsigned int seed;
    loadVar(S, encrypt_key);
    loadVar(S, seed);
    
    /* 分配VM指令数组 */
    VMInstruction *vm_code = luaM_newvector(S->L, vm_size, VMInstruction);
    for (int i = 0; i < vm_size; i++) {
      loadVar(S, vm_code[i]);
    }
    
    /* 读取反向映射表 */
    int map_size = loadInt(S);
    int *reverse_map = luaM_newvector(S->L, map_size, int);
    for (int i = 0; i < map_size; i++) {
      reverse_map[i] = loadInt(S) - 1;
    }
    
    /* 注册VM代码到全局表 */
    luaO_registerVMCode(S->L, f, vm_code, vm_size, encrypt_key, reverse_map, seed);
    
    /* 释放临时数组（已被registerVMCode复制） */
    luaM_freearray(S->L, vm_code, vm_size);
    luaM_freearray(S->L, reverse_map, map_size);
  }
  
  loadCode(S, f);
  loadConstants(S, f);
  loadUpvalues(S, f);
  loadProtos(S, f);
  loadDebug(S, f);
}


/*
** Standard Lua loading functions
*/

#define loadVector_Standard(S,b,n)	loadBlock_Standard(S,b,(n)*sizeof((b)[0]))

static void loadBlock_Standard (LoadState *S, void *b, size_t size) {
  if (luaZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");
  S->offset += size;
}

static void loadAlign_Standard (LoadState *S, unsigned align) {
  unsigned padding = align - cast_uint(S->offset % align);
  if (padding < align) {  /* (padding == align) means no padding */
    lua_Integer paddingContent;
    loadBlock_Standard(S, &paddingContent, padding);
    lua_assert(S->offset % align == 0);
  }
}

#define getaddr_Standard(S,n,t)	cast(t *, getaddr_Standard_(S,cast_sizet(n) * sizeof(t)))

static const void *getaddr_Standard_ (LoadState *S, size_t size) {
  const void *block = luaZ_getaddr(S->Z, size);
  S->offset += size;
  if (block == NULL)
    error(S, "truncated fixed buffer");
  return block;
}

#define loadVar_Standard(S,x)		loadVector_Standard(S,&x,1)

static lu_byte loadByte_Standard (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  S->offset++;
  return cast_byte(b);
}

static lua_Unsigned loadVarint_Standard (LoadState *S, lua_Unsigned limit) {
  lua_Unsigned x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte_Standard(S);
    if (x > limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) != 0);
  return x;
}

static size_t loadSize_Standard (LoadState *S) {
  return cast_sizet(loadVarint_Standard(S, MAX_SIZET));
}

static int loadInt_Standard (LoadState *S) {
  return cast_int(loadVarint_Standard(S, cast_sizet(INT_MAX)));
}

static lua_Number loadNumber_Standard (LoadState *S) {
  lua_Number x;
  loadVar_Standard(S, x);
  return x;
}

static lua_Integer loadInteger_Standard (LoadState *S) {
  lua_Unsigned cx = loadVarint_Standard(S, ((lua_Unsigned)0 - 1));
  /* decode unsigned to signed */
  if ((cx & 1) != 0)
    return l_castU2S(~(cx >> 1));
  else
    return l_castU2S(cx >> 1);
}

static void loadString_Standard (LoadState *S, Proto *p, TString **sl) {
  lua_State *L = S->L;
  TString *ts;
  TValue sv;
  size_t size = loadSize_Standard(S);
  if (size == 0) {  /* previously saved string? */
    lua_Unsigned idx = loadVarint_Standard(S, ((lua_Unsigned)0 - 1));  /* get its index */
    const TValue *stv;
    if (idx == 0) {  /* no string? */
      lua_assert(*sl == NULL);  /* must be prefilled */
      return;
    }
    stv = luaH_getint(S->h, l_castU2S(idx));
    if (novariant(rawtt(stv)) != LUA_TSTRING)
      error(S, "invalid string index");
    *sl = ts = tsvalue(stv);  /* get its value */
    luaC_objbarrier(L, p, ts);
    return;  /* do not save it again */
  }
  else if ((size -= 1) <= LUAI_MAXSHORTLEN) {  /* short string? */
    char buff[LUAI_MAXSHORTLEN + 1];  /* extra space for '\0' */
    loadVector_Standard(S, buff, size + 1);  /* load string into buffer */
    *sl = ts = luaS_newlstr(L, buff, size);  /* create string */
    luaC_objbarrier(L, p, ts);
  }
  else if (S->fixed) {  /* for a fixed buffer, use a fixed string */
    const char *s = getaddr_Standard(S, size + 1, char);  /* get content address */
    *sl = ts = luaS_newextlstr(L, s, size, NULL, NULL);
    luaC_objbarrier(L, p, ts);
  }
  else {  /* create internal copy */
    *sl = ts = luaS_createlngstrobj(L, size);  /* create string */
    luaC_objbarrier(L, p, ts);
    loadVector_Standard(S, ts->contents, size + 1);  /* load directly in final place */
  }
  /* add string to list of saved strings */
  S->nstr++;
  setsvalue(L, &sv, ts);
  luaH_setint(L, S->h, l_castU2S(S->nstr), &sv);
  luaC_objbarrierback(L, obj2gco(S->h), ts);
}

static void loadFunction_Standard(LoadState *S, Proto *f);

/*
** Standard Lua 5.4 Instruction Macros
*/
#define STD_SIZE_OP		7
#define STD_SIZE_A		8
#define STD_SIZE_B		8
#define STD_SIZE_C		8
#define STD_SIZE_Bx		17
#define STD_SIZE_Ax		25
#define STD_SIZE_sJ		25

#define STD_POS_OP		0
#define STD_POS_A		(STD_POS_OP + STD_SIZE_OP)
#define STD_POS_k		(STD_POS_A + STD_SIZE_A)
#define STD_POS_B		(STD_POS_k + 1)
#define STD_POS_C		(STD_POS_B + STD_SIZE_B)
#define STD_POS_Bx		STD_POS_k
#define STD_POS_Ax		STD_POS_A
#define STD_POS_sJ		STD_POS_A

#define STD_MAXARG_Bx   ((1<<STD_SIZE_Bx)-1)
#define STD_OFFSET_sBx  (STD_MAXARG_Bx>>1)
#define STD_MAXARG_sJ   ((1<<STD_SIZE_sJ)-1)
#define STD_OFFSET_sJ   (STD_MAXARG_sJ>>1)

#define STD_GET_OPCODE(i)	((i) & ((1<<STD_SIZE_OP)-1))
#define STD_GETARG_A(i)		(((i)>>STD_POS_A) & ((1<<STD_SIZE_A)-1))
#define STD_GETARG_B(i)		(((i)>>STD_POS_B) & ((1<<STD_SIZE_B)-1))
#define STD_GETARG_C(i)		(((i)>>STD_POS_C) & ((1<<STD_SIZE_C)-1))
#define STD_GETARG_k(i)		(((i)>>STD_POS_k) & 1)
#define STD_GETARG_Bx(i)	(((i)>>STD_POS_Bx) & ((1<<STD_SIZE_Bx)-1))
#define STD_GETARG_Ax(i)	(((i)>>STD_POS_Ax) & ((1<<STD_SIZE_Ax)-1))
#define STD_GETARG_sBx(i)	(STD_GETARG_Bx(i) - STD_OFFSET_sBx)
#define STD_GETARG_sJ(i)	(STD_GETARG_Ax(i) - STD_OFFSET_sJ)

/* ivABC (Standard 5.5) macros */
#define STD_SIZE_vB 6
#define STD_SIZE_vC 10
#define STD_POS_vB  (STD_POS_k + 1)
#define STD_POS_vC  (STD_POS_vB + STD_SIZE_vB)
#define STD_GETARG_vB(i)	(((i)>>STD_POS_vB) & ((1<<STD_SIZE_vB)-1))
#define STD_GETARG_vC(i)	(((i)>>STD_POS_vC) & ((1<<STD_SIZE_vC)-1))

static Instruction transcodeInstruction(unsigned int inst32, Instruction *code, int i, unsigned int *code32) {
  int op = STD_GET_OPCODE(inst32);
  int xop = op;

  /* Map Standard OpCode to XCLUA OpCode */
  if (op <= 47) { /* 0-47 (OP_SHR) */
    xop = op;
  } else if (op >= 48 && op <= 85) { /* OP_MMBIN (48) - OP_VARARGPREP (85) */
    xop = op + 1;
  } else if (op == 86) { /* OP_EXTRAARG (86) */
    xop = OP_EXTRAARG; /* 103 */
  } else {
    /* Unknown opcode, keep as is */
    xop = op;
  }

  Instruction inst = 0;
  enum OpMode mode = getOpMode(xop);

  switch (mode) {
    case iABC: {
      int a = STD_GETARG_A(inst32);
      int b = STD_GETARG_B(inst32);
      int c = STD_GETARG_C(inst32);
      int k = STD_GETARG_k(inst32);
      inst = CREATE_ABCk(xop, a, b, c, k);
      break;
    }
    case ivABC: {
      /* Extract using Standard ivABC macros */
      int a = STD_GETARG_A(inst32);
      int vb = STD_GETARG_vB(inst32);
      int vc = STD_GETARG_vC(inst32);
      int k = STD_GETARG_k(inst32);
      inst = CREATE_vABCk(xop, a, vb, vc, k);
      break;
    }
    case iABx: {
      int a = STD_GETARG_A(inst32);
      int bx = STD_GETARG_Bx(inst32);
      inst = CREATE_ABx(xop, a, bx);
      break;
    }
    case iAsBx: {
      int a = STD_GETARG_A(inst32);
      int sbx = STD_GETARG_sBx(inst32);
      inst = CREATE_ABx(xop, a, cast_uint(sbx + OFFSET_sBx));
      break;
    }
    case iAx: {
      int ax = STD_GETARG_Ax(inst32);

      /* Handle OP_EXTRAARG fixup for OP_SETLIST/OP_NEWTABLE */
      if (xop == OP_EXTRAARG && i > 0) {
        Instruction prev_inst = code[i-1];
        OpCode prev_op = GET_OPCODE(prev_inst);
        if (prev_op == OP_SETLIST || prev_op == OP_NEWTABLE) {
           /* Recover previous vC */
           int prev_vc = GETARG_vC(prev_inst);
           /* Calculate full value: Standard (Ax << 10) | vC */
           /* Note: Standard vC is 10 bits. */
           unsigned int prev_inst32 = code32[i-1];
           int prev_vc_std = STD_GETARG_vC(prev_inst32);
           unsigned long long full_val = ((unsigned long long)ax << 10) | prev_vc_std;

           /* Split for XCLUA: Ax' = full_val >> 20, vC' = full_val & 0xFFFFF */
           int new_vc = (int)(full_val & 0xFFFFF);
           int new_ax = (int)(full_val >> 20);

           /* Update previous instruction */
           SETARG_vC(code[i-1], new_vc);

           /* Update current Ax */
           ax = new_ax;
        }
      }

      inst = CREATE_Ax(xop, ax);
      break;
    }
    case isJ: {
      int sj = STD_GETARG_sJ(inst32);
      inst = CREATE_sJ(xop, sj, 0);
      break;
    }
  }
  return inst;
}

static void loadCode_Standard (LoadState *S, Proto *f) {
  int n = loadInt_Standard(S);
  loadAlign_Standard(S, sizeof(unsigned int)); /* Align to 4 bytes */

  f->code = luaM_newvectorchecked(S->L, n, Instruction);
  f->sizecode = n;

  unsigned int *code32 = luaM_newvector(S->L, n, unsigned int);
  loadVector_Standard(S, code32, n);

  for (int i = 0; i < n; i++) {
    f->code[i] = transcodeInstruction(code32[i], f->code, i, code32);
  }

  luaM_freearray(S->L, code32, n);
}

static void loadConstants_Standard (LoadState *S, Proto *f) {
  int i;
  int n = loadInt_Standard(S);
  f->k = luaM_newvectorchecked(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = loadByte_Standard(S);
    switch (t) {
      case LUA_VNIL:
        setnilvalue(o);
        break;
      case LUA_VFALSE:
        setbfvalue(o);
        break;
      case LUA_VTRUE:
        setbtvalue(o);
        break;
      case LUA_VNUMFLT:
        setfltvalue(o, loadNumber_Standard(S));
        break;
      case LUA_VNUMINT:
        setivalue(o, loadInteger_Standard(S));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR: {
        lua_assert(f->source == NULL);
        loadString_Standard(S, f, &f->source);  /* use 'source' to anchor string */
        if (f->source == NULL)
          error(S, "bad format for constant string");
        setsvalue2n(S->L, o, f->source);  /* save it in the right place */
        f->source = NULL;
        break;
      }
      default: error(S, "invalid constant");
    }
  }
}

static void loadProtos_Standard (LoadState *S, Proto *f) {
  int i;
  int n = loadInt_Standard(S);
  f->p = luaM_newvectorchecked(S->L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = luaF_newproto(S->L);
    luaC_objbarrier(S->L, f, f->p[i]);
    loadFunction_Standard(S, f->p[i]);
  }
}

static void loadUpvalues_Standard (LoadState *S, Proto *f) {
  int i;
  int n = loadInt_Standard(S);
  f->upvalues = luaM_newvectorchecked(S->L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)  /* make array valid for GC */
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {  /* following calls can raise errors */
    f->upvalues[i].instack = loadByte_Standard(S);
    f->upvalues[i].idx = loadByte_Standard(S);
    f->upvalues[i].kind = loadByte_Standard(S);
  }
}

static void loadDebug_Standard (LoadState *S, Proto *f) {
  int i;
  int n = loadInt_Standard(S);
  if (S->fixed) {
    f->lineinfo = getaddr_Standard(S, n, ls_byte);
    f->sizelineinfo = n;
  }
  else {
    f->lineinfo = luaM_newvectorchecked(S->L, n, ls_byte);
    f->sizelineinfo = n;
    loadVector_Standard(S, f->lineinfo, n);
  }
  n = loadInt_Standard(S);
  if (n > 0) {
    loadAlign_Standard(S, sizeof(int));
    if (S->fixed) {
      f->abslineinfo = getaddr_Standard(S, n, AbsLineInfo);
      f->sizeabslineinfo = n;
    }
    else {
      f->abslineinfo = luaM_newvectorchecked(S->L, n, AbsLineInfo);
      f->sizeabslineinfo = n;
      loadVector_Standard(S, f->abslineinfo, n);
    }
  }
  n = loadInt_Standard(S);
  f->locvars = luaM_newvectorchecked(S->L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    loadString_Standard(S, f, &f->locvars[i].varname);
    f->locvars[i].startpc = loadInt_Standard(S);
    f->locvars[i].endpc = loadInt_Standard(S);
  }
  n = loadInt_Standard(S);
  if (n != 0)  /* does it have debug information? */
    n = f->sizeupvalues;  /* must be this many */
  for (i = 0; i < n; i++)
    loadString_Standard(S, f, &f->upvalues[i].name);
}

static void loadFunction_Standard (LoadState *S, Proto *f) {
  f->linedefined = loadInt_Standard(S);
  f->lastlinedefined = loadInt_Standard(S);
  f->numparams = loadByte_Standard(S);
  /* get only the meaningful flags */
  f->flag = cast_byte(loadByte_Standard(S) & ~PF_FIXED);
  if (S->fixed)
    f->flag |= PF_FIXED;  /* signal that code is fixed */

  /* XCLUA specific: sync is_vararg field from flag */
  f->is_vararg = (f->flag & (PF_VAHID | PF_VATAB)) != 0;

  f->maxstacksize = loadByte_Standard(S);
  loadCode_Standard(S, f);
  loadConstants_Standard(S, f);
  loadUpvalues_Standard(S, f);
  loadProtos_Standard(S, f);
  loadString_Standard(S, f, &f->source);
  loadDebug_Standard(S, f);
}

static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  loadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}


static void fchecksize (LoadState *S, size_t size, const char *tname) {
  if (loadByte(S) != size)
    error(S, luaO_pushfstring(S->L, "%s size mismatch", tname));
}


#define checksize(S,t)	fchecksize(S,sizeof(t),#t)

static void checkHeader (LoadState *S) {
  /* skip 1st char (already read and checked) */
  checkliteral(S, &LUA_SIGNATURE[1], "not a binary chunk");
  
  lu_byte version = loadByte(S);
  lu_byte format = loadByte(S);
  
  if (format != LUAC_FORMAT)
    error(S, "format mismatch");
  
  /* check LUAC_DATA */
  const char *original_data = LUAC_DATA;
  size_t data_len = sizeof(LUAC_DATA) - 1;
  char *read_data = (char *)luaM_malloc_(S->L, data_len, 0);
  
  loadVector(S, read_data, data_len);
  
  if (memcmp(read_data, original_data, data_len) != 0) {
    luaM_free_(S->L, read_data, data_len);
    error(S, "corrupted chunk");
  }
  luaM_free_(S->L, read_data, data_len);
  
  /* Detect Standard Lua vs XCLUA */
  int b1 = loadByte(S);
  int b2 = zgetc(S->Z); /* Peek/Read next byte */

  /* XCLUA Universal Format: Inst=8, Int=8 */
  if (!S->force_standard && b1 == 8 && b2 == 8) {
    S->is_standard = 0;

    /* Continue verifying XCLUA header */
    /* b1 (Instruction size) verified by detection */
    /* b2 (lua_Integer size) verified by detection */
    /* Note: zgetc consumed b2, so we skip checksize(S, lua_Integer) reading */

    if (loadByte(S) != 8) /* Check lua_Number size (fixed to 8) */
      error(S, "float size mismatch");

    if (loadInt64(S) != 0x5678)
      error(S, "integer format mismatch");

    if (loadDouble(S) != 370.5)
      error(S, "float format mismatch");

  } else {
    S->is_standard = 1;
    S->offset = 14; /* Update offset: Sig(4)+Ver(1)+Fmt(1)+Data(6)+b1(1)+b2(1) = 14 */

    if (version != LUAC_VERSION_STD)
      error(S, "version mismatch");

    if (b1 != sizeof(int))
      error(S, "int size mismatch");

    /* Check LUAC_INT (int) */
    /* We read b2 (1st byte). Read remaining sizeof(int)-1 bytes */
    int i_val;
    unsigned char *p = (unsigned char *)&i_val;
    p[0] = b2;
    loadVector_Standard(S, p+1, sizeof(int)-1);
    if (i_val != (int)LUAC_INT_STD)
      error(S, "int format mismatch");

    /* Check Instruction */
    if (loadByte_Standard(S) != sizeof(unsigned int))
      error(S, "instruction size mismatch");
    unsigned int inst;
    loadVar_Standard(S, inst);
    if (inst != LUAC_INST_STD)
      error(S, "instruction format mismatch");

    /* Check lua_Integer */
    if (loadByte_Standard(S) != sizeof(lua_Integer))
      error(S, "lua_Integer size mismatch");
    lua_Integer li;
    loadVar_Standard(S, li);
    if (li != LUAC_INT_STD)
      error(S, "lua_Integer format mismatch");

    /* Check lua_Number */
    if (loadByte_Standard(S) != sizeof(lua_Number))
      error(S, "lua_Number size mismatch");
    lua_Number ln;
    loadVar_Standard(S, ln);
    if (ln != LUAC_NUM_STD)
      error(S, "lua_Number format mismatch");
  }
}


/*
** Load precompiled chunk.
*/
LClosure *luaU_undump(lua_State *L, ZIO *Z, const char *name, int force_standard) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    S.name = name + 1;
  else if (*name == LUA_SIGNATURE[0])
    S.name = "binary string";
  else
    S.name = name;
  S.L = L;
  S.Z = Z;
  S.offset = 1;
  S.force_standard = force_standard;
  checkHeader(&S);

  lu_byte nupvalues;
  if (S.is_standard) {
      nupvalues = loadByte_Standard(&S);
  } else {
      nupvalues = loadByte(&S);
  }

  cl = luaF_newLclosure(L, nupvalues);
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);

  if (S.is_standard) {
      S.h = luaH_new(L);
      S.nstr = 0;
      S.fixed = 0;
      sethvalue2s(L, L->top.p, S.h);
      luaD_inctop(L);
  }

  cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);

  if (S.is_standard) {
      loadFunction_Standard(&S, cl->p);
  } else {
      loadFunction(&S, cl->p, NULL);
  }

  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  luai_verifycode(L, cl->p);

  if (S.is_standard) {
      L->top.p--; /* pop table */
  }

  return cl;
}

