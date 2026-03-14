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
      /* 长字符串：直接解密 */
      // 读取字符串内容的SHA-256哈希值（完整性验证）
      uint8_t expected_content_hash[SHA256_DIGEST_SIZE];
      loadVector(S, expected_content_hash, SHA256_DIGEST_SIZE);
      
      // 读取加密数据长度
      size_t encrypted_len = loadSize(S);
      
      // 分配内存
      unsigned char *encrypted_data = (unsigned char *)luaM_malloc_(S->L, encrypted_len, 0);
      if (encrypted_data == NULL) {
        error(S, "memory allocation failed for encrypted data");
        return NULL;
      }
      
      // 读取加密数据
      loadBlock(S, encrypted_data, encrypted_len);
      
      // 创建长字符串对象
      ts = luaS_createlngstrobj(L, size);  /* create string */
      setsvalue2s(L, L->top.p, ts);  /* anchor it */
      luaD_inctop(L);
      
      // 复制加密数据到字符串
      char *str = ts->contents;
      memcpy(str, encrypted_data, size);
      
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
      luaM_free_(S->L, encrypted_data, encrypted_len);
      
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
  
  // 读取加密数据长度
  size_t encrypted_len = loadSize(S);
  
  // 分配内存
  unsigned char *encrypted_data = (unsigned char *)luaM_malloc_(S->L, encrypted_len, 0);
  if (encrypted_data == NULL) {
    error(S, "memory allocation failed for encrypted data");
    return;
  }
  
  // 读取加密数据
  loadBlock(S, encrypted_data, encrypted_len);
  
  // Allocate memory for original code
  f->code = luaM_newvectorchecked(S->L, orig_size, Instruction);
  f->sizecode = orig_size;
  
  // 从加密数据中恢复指令，并使用时间戳解密
  for (i = 0; i < (int)data_size; i++) {
    unsigned char decrypted_byte = encrypted_data[i] ^ ((char *)&S->timestamp)[i % sizeof(S->timestamp)];
    
    /* Reconstruct Instructions from LE bytes (64-bit) */
    int inst_idx = i / 8;
    int byte_idx = i % 8;
    if (byte_idx == 0) {
      f->code[inst_idx] = 0;
    }
    f->code[inst_idx] |= ((Instruction)decrypted_byte) << (byte_idx * 8);
  }
  
  // 释放内存
  luaM_free_(S->L, encrypted_data, encrypted_len);
  
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
  
  /* Read XCLUA Universal Format: Inst=8, Int=8 */
  int b1 = loadByte(S);
  int b2 = zgetc(S->Z); /* Peek/Read next byte */

  if (b1 != 8 || b2 != 8) {
    error(S, "unsupported bytecode format (standard lua format has been permanently stripped)");
  }

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
  (void)force_standard;
  checkHeader(&S);

  lu_byte nupvalues;
  nupvalues = loadByte(&S);

  cl = luaF_newLclosure(L, nupvalues);
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);

  cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);

  loadFunction(&S, cl->p, NULL);

  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  luai_verifycode(L, cl->p);

  return cl;
}

