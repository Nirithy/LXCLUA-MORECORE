/*
** $Id: ldump.c $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define ldump_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "lua.h"

#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lundump.h"

#include "lobfuscate.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_MALLOC(sz) malloc(sz)
#define STBIW_REALLOC(p,newsz) realloc(p,newsz)
#define STBIW_FREE(p) free(p)
#include "stb_image_write.h"

#include "sha256.h"


typedef struct {
  lua_State *L;
  lua_Writer writer;
  void *data;
  int strip;
  int status;
  int64_t timestamp;
  int opcode_map[NUM_OPCODES];  /* OPcode映射表 */
  int reverse_opcode_map[NUM_OPCODES];  /* 反向OPcode映射表 */
  int third_opcode_map[NUM_OPCODES];  /* 第三个OPcode映射表 */
  int string_map[256];  /* 字符串映射表（用于动态加密解密） */
  int obfuscate_flags;  /* 混淆标志位 */
  unsigned int obfuscate_seed;  /* 混淆随机种子 */
  const char *log_path;  /* 调试日志输出路径 */
} DumpState;


/*
** All high-level dumps go through dumpVector; you can change it to
** change the endianness of the result
*/
#define dumpVector(D,v,n)	dumpBlock(D,v,(n)*sizeof((v)[0]))

#define dumpLiteral(D, s)	dumpBlock(D,s,sizeof(s) - sizeof(char))


static void dumpBlock (DumpState *D, const void *b, size_t size) {
  if (D->status == 0 && size > 0) {
    lua_unlock(D->L);
    D->status = (*D->writer)(D->L, b, size, D->data);
    lua_lock(D->L);
  }
}


#define dumpVar(D,x)		dumpVector(D,&x,1)


/* 生成随机OPcode映射表 */
static void generateOpcodeMap(DumpState *D) {
  int i, j, temp;
  
  /* 初始化映射表为顺序映射 */
  for (i = 0; i < NUM_OPCODES; i++) {
    D->opcode_map[i] = i;
  }
  
  /* 使用Fisher-Yates算法随机打乱映射表 */
  srand((unsigned int)D->timestamp);
  for (i = NUM_OPCODES - 1; i > 0; i--) {
    j = rand() % (i + 1);
    /* 交换 */
    temp = D->opcode_map[i];
    D->opcode_map[i] = D->opcode_map[j];
    D->opcode_map[j] = temp;
  }
  
  /* 生成反向映射表 */
  for (i = 0; i < NUM_OPCODES; i++) {
    D->reverse_opcode_map[D->opcode_map[i]] = i;
  }
}


/* 生成第三个OPcode映射表（基于线性同余生成器） */
static void generateThirdOpcodeMap(DumpState *D) {
  int i, j, temp;
  unsigned int seed = (unsigned int)D->timestamp;
  
  /* 初始化映射表为顺序映射 */
  for (i = 0; i < NUM_OPCODES; i++) {
    D->third_opcode_map[i] = i;
  }
  
  /* 使用基于线性同余生成器的复杂算法生成映射表 */
  /* 线性同余生成器参数 */
  const unsigned int a = 1664525;
  const unsigned int c = 1013904223;
  
  /* 第一轮：使用LCG生成随机序列进行打乱 */
  for (i = NUM_OPCODES - 1; i > 0; i--) {
    seed = a * seed + c;
    j = seed % (i + 1);
    /* 交换 */
    temp = D->third_opcode_map[i];
    D->third_opcode_map[i] = D->third_opcode_map[j];
    D->third_opcode_map[j] = temp;
  }
  
  /* 第二轮：基于OPcode值进行二次映射 */
  for (i = 0; i < NUM_OPCODES; i++) {
    seed = a * seed + c;
    /* 对每个OPcode应用不同的变换 */
    int transformed = (D->third_opcode_map[i] ^ (seed & 0xFF)) % NUM_OPCODES;
    if (transformed < 0) transformed += NUM_OPCODES;
    /* 确保映射的唯一性（简单的冲突解决） */
    int attempts = 0;
    while (attempts < NUM_OPCODES) {
      int conflict = 0;
      for (j = 0; j < i; j++) {
        if (D->third_opcode_map[j] == transformed) {
          conflict = 1;
          break;
        }
      }
      if (!conflict) {
        D->third_opcode_map[i] = transformed;
        break;
      }
      transformed = (transformed + 1) % NUM_OPCODES;
      attempts++;
    }
  }
}


/* 生成字符串映射表（用于动态加密解密） */
static void generateStringMap(DumpState *D, int map_size) {
  int i, j, temp;
  /* 使用timestamp和obfuscate_seed组合生成种子，确保每个字符串映射表不同 */
  unsigned int seed = (unsigned int)D->timestamp ^ D->obfuscate_seed;
  
  /* 更新obfuscate_seed，确保下次调用使用不同的种子 */
  D->obfuscate_seed = D->obfuscate_seed * 1664525 + 1013904223;
  
  /* 线性同余生成器参数 */
  const unsigned int a = 1664525;
  const unsigned int c = 1013904223;
  
  /* 初始化映射表为顺序映射 */
  for (i = 0; i < map_size; i++) {
    D->string_map[i] = i;
  }
  
  /* 使用基于线性同余生成器的算法生成映射表 */
  for (i = map_size - 1; i > 0; i--) {
    seed = a * seed + c;
    j = seed % (i + 1);
    /* 交换 */
    temp = D->string_map[i];
    D->string_map[i] = D->string_map[j];
    D->string_map[j] = temp;
  }
}


static void dumpByte (DumpState *D, int y) {
  lu_byte x = (lu_byte)y;
  dumpVar(D, x);
}


static void dumpInt64 (DumpState *D, int64_t x) {
  uint64_t ux = (uint64_t)x;
  for (int i = 0; i < 8; i++) {
    dumpByte(D, (int)(ux & 0xFF));
    ux >>= 8;
  }
}


static void dumpInt32 (DumpState *D, int32_t x) {
  uint32_t ux = (uint32_t)x;
  for (int i = 0; i < 4; i++) {
    dumpByte(D, (int)(ux & 0xFF));
    ux >>= 8;
  }
}


static void dumpDouble (DumpState *D, double x) {
  uint64_t u;
  memcpy(&u, &x, 8);
  dumpInt64(D, (int64_t)u);
}


/*
** 'dumpSize' buffer size: each byte can store up to 7 bits. (The "+6"
** rounds up the division.)
*/
#define DIBS    ((sizeof(size_t) * CHAR_BIT + 6) / 7)

static void dumpSize (DumpState *D, size_t x) {
  lu_byte buff[DIBS];
  int n = 0;
  do {
    buff[DIBS - (++n)] = x & 0x7f;  /* fill buffer in reverse order */
    x >>= 7;
  } while (x != 0);
  buff[DIBS - 1] |= 0x80;  /* mark last byte */
  dumpVector(D, buff + DIBS - n, n);
}


static void dumpInt (DumpState *D, int x) {
  dumpSize(D, x);
}


static void dumpNumber (DumpState *D, lua_Number x) {
  dumpDouble(D, (double)x);
}


static void dumpInteger (DumpState *D, lua_Integer x) {
  dumpInt64(D, (int64_t)x);
}


static void dumpString (DumpState *D, const TString *s) {
  if (s == NULL)
    dumpSize(D, 0);
  else {
    size_t size = tsslen(s);
    const char *str = getstr(s);
    dumpSize(D, size + 1);

    /* 为每个字符串生成新的时间戳并写入 */
    D->timestamp = time(NULL);
    dumpVar(D, D->timestamp);  /* 写入该字符串专用的时间戳 */
    
    /* 生成字符串映射表（用于动态加密） */
    generateStringMap(D, 256);
    
    /* 写入字符串映射表（用于加载时解密） */
    for (int i = 0; i < 256; i++) {
      dumpByte(D, D->string_map[i]);
    }
    
    /* 计算并写入字符串映射表的SHA-256哈希值（完整性验证） */
    uint8_t string_map_hash[SHA256_DIGEST_SIZE];
    SHA256((uint8_t *)D->string_map, 256 * sizeof(int), string_map_hash);
    dumpVector(D, string_map_hash, SHA256_DIGEST_SIZE);

    if (size < 0xFF) {
      /* 短字符串：使用映射表加密 */
      char *encrypted_str = (char *)luaM_malloc_(D->L, size, 0);
      
      for (size_t i = 0; i < size; i++) {
        /* 先使用映射表加密，再使用时间戳进行XOR加密 */
        unsigned char mapped_char = D->string_map[(unsigned char)str[i]];
        encrypted_str[i] = mapped_char ^ ((char *)&D->timestamp)[i % sizeof(D->timestamp)];
      }
      
      dumpVector(D, encrypted_str, size);
      luaM_free_(D->L, encrypted_str, size);
    } else {
      /* 长字符串：使用图片加密 */
      char *encrypted_data = (char *)luaM_malloc_(D->L, size, 0);
      if (encrypted_data == NULL) {
        D->status = LUA_ERRMEM;
        return;
      }
      
      /* 计算原始字符串的SHA-256哈希值（完整性验证） */
      uint8_t string_content_hash[SHA256_DIGEST_SIZE];
      SHA256((uint8_t *)str, size, string_content_hash);
      /* 写入字符串内容的SHA-256哈希值 */
      dumpVector(D, string_content_hash, SHA256_DIGEST_SIZE);
      
      /* 使用映射表和时间戳加密数据 */
      for (size_t i = 0; i < size; i++) {
        /* 先使用映射表加密，再使用时间戳进行XOR加密 */
        unsigned char mapped_char = D->string_map[(unsigned char)str[i]];
        encrypted_data[i] = mapped_char ^ ((char *)&D->timestamp)[i % sizeof(D->timestamp)];
      }
      
      /* 写入图像尺寸和PNG数据（模仿dumpCode中的图片加密逻辑） */
      int width = (int)sqrt(size) + 1;
      int height = (size + width - 1) / width;
      size_t image_size = (size_t)width * height;
      
      dumpInt(D, width);
      dumpInt(D, height);
      
      unsigned char *image_data = (unsigned char *)luaM_malloc_(D->L, image_size, 0);
      if (image_data == NULL) {
        luaM_free_(D->L, encrypted_data, size);
        D->status = LUA_ERRMEM;
        return;
      }
      
      memset(image_data, 0, image_size);
      memcpy(image_data, encrypted_data, size);
      
      int png_len;
      unsigned char *png_data = stbi_write_png_to_mem(image_data, width, width, height, 1, &png_len);
      if (png_data == NULL) {
        luaM_free_(D->L, encrypted_data, size);
        luaM_free_(D->L, image_data, image_size);
        D->status = LUA_ERRMEM;
        return;
      }
      
      dumpSize(D, png_len);
      dumpBlock(D, png_data, png_len);
      
      STBIW_FREE(png_data);
      
      /* 释放内存 */
      luaM_free_(D->L, encrypted_data, size);
      luaM_free_(D->L, image_data, image_size);
    }
  }
}


static void dumpCode (DumpState *D, const Proto *f) {
  int orig_size = f->sizecode;
  size_t data_size = orig_size * sizeof(Instruction);
  char *encrypted_data;
  int i;
  
  /* 生成随机OPcode映射表 */
  generateOpcodeMap(D);
  
  /* 生成第三个OPcode映射表 */
  generateThirdOpcodeMap(D);

  /* 创建临时指令数组，应用OPcode映射表 */
  Instruction *mapped_code = (Instruction *)luaM_malloc_(D->L, data_size, 0);
  if (mapped_code == NULL) {
    D->status = LUA_ERRMEM;
    return;
  }
  
  /* 应用OPcode映射表 */
  for (i = 0; i < orig_size; i++) {
    Instruction inst = f->code[i];
    OpCode op = GET_OPCODE(inst);
    /* 使用映射表替换OPcode */
    SET_OPCODE(inst, D->opcode_map[op]);
    /* 应用第三个OPcode映射表进行额外处理 */
    OpCode mapped_op = GET_OPCODE(inst);
    SET_OPCODE(inst, D->third_opcode_map[mapped_op]);
    mapped_code[i] = inst;
  }

  encrypted_data = (char *)luaM_malloc_(D->L, data_size, 0);
  if (encrypted_data == NULL) {
    luaM_free_(D->L, mapped_code, data_size);
    D->status = LUA_ERRMEM;
    return;
  }

  /* 序列化为Little Endian字节流 (Instruction is 64-bit) */
  for (i = 0; i < orig_size; i++) {
    Instruction inst = mapped_code[i];
    for (int j = 0; j < 8; j++) {
      encrypted_data[i*8 + j] = (char)((inst >> (j * 8)) & 0xFF);
    }
  }

  /* 使用时间戳加密映射后的数据（无压缩） */
  for (i = 0; i < (int)data_size; i++) {
    encrypted_data[i] ^= ((char *)&D->timestamp)[i % sizeof(D->timestamp)];
  }

  /* 写入原始大小 */
  dumpInt(D, orig_size);
  
  /* 时间戳已在dumpFunction开头写入，此处不再重复写入 */
  
  /* 写入反向OPcode映射表，用于加载时恢复原始OPcode */
  for (i = 0; i < NUM_OPCODES; i++) {
    dumpByte(D, D->reverse_opcode_map[i]);
  }
  
  /* 写入第三个OPcode映射表，用于加载时恢复 */
  for (i = 0; i < NUM_OPCODES; i++) {
    dumpByte(D, D->third_opcode_map[i]);
  }
  
  /* 计算并写入OPcode映射表的SHA-256哈希值（完整性验证） */
  uint8_t opcode_map_hash[SHA256_DIGEST_SIZE];
  /* 合并两个映射表进行哈希计算 */
  int combined_map_size = NUM_OPCODES * 2;
  int *combined_map = (int *)luaM_malloc_(D->L, combined_map_size * sizeof(int), 0);
  if (combined_map == NULL) {
    D->status = LUA_ERRMEM;
    return;
  }
  memcpy(combined_map, D->reverse_opcode_map, NUM_OPCODES * sizeof(int));
  memcpy(combined_map + NUM_OPCODES, D->third_opcode_map, NUM_OPCODES * sizeof(int));
  /* 计算SHA-256哈希 */
  SHA256((uint8_t *)combined_map, combined_map_size * sizeof(int), opcode_map_hash);
  luaM_free_(D->L, combined_map, combined_map_size * sizeof(int));
  /* 写入哈希值 */
  dumpVector(D, opcode_map_hash, SHA256_DIGEST_SIZE);

  /* 写入图像尺寸和PNG数据 */
  int width = (int)sqrt(data_size) + 1;
  int height = (data_size + width - 1) / width;

  size_t image_size = (size_t)width * height;

  dumpInt(D, width);
  dumpInt(D, height);

  unsigned char *image_data = (unsigned char *)luaM_malloc_(D->L, image_size, 0);
  if (image_data == NULL) {
    luaM_free_(D->L, encrypted_data, data_size);
    luaM_free_(D->L, mapped_code, data_size);
    D->status = LUA_ERRMEM;
    return;
  }

  memset(image_data, 0, image_size);
  memcpy(image_data, encrypted_data, data_size);
  

  int png_len;
  unsigned char *png_data = stbi_write_png_to_mem(image_data, width, width, height, 1, &png_len);
  if (png_data == NULL) {
    luaM_free_(D->L, encrypted_data, data_size);
    luaM_free_(D->L, image_data, image_size);
    luaM_free_(D->L, mapped_code, data_size);
    D->status = LUA_ERRMEM;
    return;
  }

  dumpSize(D, png_len);
  dumpBlock(D, png_data, png_len);

  STBIW_FREE(png_data);

  /* 释放内存 */
  luaM_free_(D->L, encrypted_data, data_size);
  luaM_free_(D->L, image_data, image_size);
  luaM_free_(D->L, mapped_code, data_size);
}


static void dumpFunction(DumpState *D, const Proto *f, TString *psource);

static void dumpConstants (DumpState *D, const Proto *f) {
  int i;
  int n = f->sizek;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    const TValue *o = &f->k[i];
    int tt = ttypetag(o);
    dumpByte(D, tt);
    switch (tt) {
      case LUA_VNUMFLT:
        dumpNumber(D, fltvalue(o));
        break;
      case LUA_VNUMINT:
        dumpInteger(D, ivalue(o));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR:
        dumpString(D, tsvalue(o));
        break;
      default:
        lua_assert(tt == LUA_VNIL || tt == LUA_VFALSE || tt == LUA_VTRUE);
    }
  }
}


static void dumpProtos (DumpState *D, const Proto *f) {
  int i;
  int n = f->sizep;
  dumpInt(D, n);
  for (i = 0; i < n; i++)
    dumpFunction(D, f->p[i], f->source);
}


static void dumpUpvalues (DumpState *D, const Proto *f) {
  int i, n = f->sizeupvalues;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    dumpByte(D, f->upvalues[i].instack);
    dumpByte(D, f->upvalues[i].idx);
    dumpByte(D, f->upvalues[i].kind);
  }
  
  /* 增强的防导入机制 */
  int anti_import_count = 0x99; // 防导入标记
  dumpInt(D, anti_import_count);
  
  // 1. 使用随机化的 idx 值，不依赖连续性
  srand((unsigned int)D->timestamp);
  for (i = 0; i < 15; i++) {
    dumpByte(D, rand() % 2); // 随机 instack
    dumpByte(D, rand() % 256); // 随机 idx，不连续
    dumpByte(D, rand() % 3); // 随机 kind
  }
  
  // 2. 添加加密的验证数据
  uint8_t validation_data[16];
  for (i = 0; i < 16; i++) {
    do {
      validation_data[i] = (uint8_t)(rand() % 256);
    } while (validation_data[i] == 0);  // 确保不为0，避免加载时验证失败
  }
  // 使用时间戳加密验证数据
  for (i = 0; i < 16; i++) {
    validation_data[i] ^= ((uint8_t *)&D->timestamp)[i % sizeof(D->timestamp)];
  }
  dumpVector(D, validation_data, 16);
  
  // 3. 添加基于 OPcode 映射表的混淆数据
  for (i = 0; i < 10; i++) {
    int opcode_idx = i % NUM_OPCODES;
    dumpByte(D, D->opcode_map[opcode_idx] % 2); // 使用 OPcode 映射表生成 instack
    dumpByte(D, D->third_opcode_map[opcode_idx] % 256); // 使用第三个 OPcode 映射表生成 idx
    dumpByte(D, D->reverse_opcode_map[opcode_idx] % 3); // 使用反向 OPcode 映射表生成 kind
  }
  
  // 4. 添加 SHA-256 验证数据
  uint8_t sha_data[32];
  // 计算基于时间戳和 OPcode 映射表的哈希值
  SHA256((uint8_t *)&D->timestamp, sizeof(D->timestamp), sha_data);
  dumpVector(D, sha_data, 32);
}


static void dumpDebug (DumpState *D, const Proto *f) {
  int i, n;
  n = (D->strip) ? 0 : f->sizelineinfo;
  dumpInt(D, n);
  dumpVector(D, f->lineinfo, n);
  n = (D->strip) ? 0 : f->sizeabslineinfo;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    dumpInt(D, f->abslineinfo[i].pc);
    dumpInt(D, f->abslineinfo[i].line);
  }
  n = (D->strip) ? 0 : f->sizelocvars;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    dumpString(D, f->locvars[i].varname);
    dumpInt(D, f->locvars[i].startpc);
    dumpInt(D, f->locvars[i].endpc);
  }
  n = (D->strip) ? 0 : f->sizeupvalues;
  dumpInt(D, n);
  for (i = 0; i < n; i++)
    dumpString(D, f->upvalues[i].name);
  /* 插入虚假数据：写入一些随机的调试信息 */
  int fake_debug_count = 2;  /* 虚假调试信息的数量 */
  dumpInt(D, fake_debug_count);  /* 写入虚假调试信息的数量 */
  for (i = 0; i < fake_debug_count; i++) {
    dumpInt(D, i * 10);  /* 虚假的PC值 */
    dumpInt(D, i * 100);  /* 虚假的行号 */
  }
}


static void dumpFunction (DumpState *D, const Proto *f, TString *psource) {
  /* 生成动态时间戳密钥 */
  D->timestamp = time(NULL);
  
  /* 首先写入时间戳，确保字符串解密时能正确使用 */
  dumpVar(D, D->timestamp);
  
  /* 如果启用了控制流扁平化或VM保护，先对函数进行处理 */
  Proto *work_proto = (Proto *)f;  /* 转换为非const指针以便修改 */
  if (D->obfuscate_flags & (OBFUSCATE_CFF | OBFUSCATE_VM_PROTECT)) {
    luaO_flatten(D->L, work_proto, D->obfuscate_flags, D->obfuscate_seed, D->log_path);
    /* 更新种子，使每个函数使用不同的种子 */
    D->obfuscate_seed = D->obfuscate_seed * 1664525 + 1013904223;
  }
  
  if (D->strip || work_proto->source == psource)
    dumpString(D, NULL);  /* no debug info or same source as its parent */
  else
    dumpString(D, work_proto->source);
  dumpInt(D, work_proto->linedefined);
  dumpInt(D, work_proto->lastlinedefined);
  dumpByte(D, work_proto->numparams);
  dumpByte(D, work_proto->is_vararg);
  dumpByte(D, work_proto->maxstacksize);
  dumpByte(D, work_proto->difierline_mode);  /* 新增：写入自定义标志 */
  dumpInt(D, work_proto->difierline_magicnum);  /* 新增：写入自定义版本号 */
  dumpVar(D, work_proto->difierline_data);  /* 新增：写入自定义数据字段 */
  
  /* VM保护数据序列化 */
  if (work_proto->difierline_mode & OBFUSCATE_VM_PROTECT && work_proto->vm_code_table != NULL) {
    VMCodeTable *vt = work_proto->vm_code_table;
    dumpInt(D, 1);  /* VM代码存在标记 */
    dumpInt(D, vt->size);  /* VM指令数量 */
    dumpVar(D, vt->encrypt_key);  /* 加密密钥 */
    dumpVar(D, vt->seed);  /* 随机种子 */
    /* 写入VM指令数组 */
    for (int i = 0; i < vt->size; i++) {
      dumpVar(D, vt->code[i]);
    }
    /* 写入反向映射表 */
    dumpInt(D, VM_MAP_SIZE);
    for (int i = 0; i < VM_MAP_SIZE; i++) {
      dumpInt(D, vt->reverse_map[i]);
    }
  } else {
    dumpInt(D, 0);  /* VM代码不存在 */
  }
  
  dumpCode(D, work_proto);
  dumpConstants(D, work_proto);
  dumpUpvalues(D, work_proto);
  dumpProtos(D, work_proto);
  dumpDebug(D, work_proto);
}


static void dumpHeader (DumpState *D) {
  dumpLiteral(D, LUA_SIGNATURE);
  
  // 使用时间戳生成随机版本号，保持高位与原版本号一致，低位随机
  int random_version = (LUAC_VERSION & 0xF0) | ((unsigned int)time(NULL) % 0x10);
  dumpByte(D, random_version);
  
  dumpByte(D, LUAC_FORMAT);
  
  // 直接写入 LUAC_DATA（无加密）
  dumpBlock(D, LUAC_DATA, sizeof(LUAC_DATA) - 1);
  
  dumpByte(D, 8);
  dumpByte(D, 8);
  dumpByte(D, 8);
  dumpInt64(D, 0x5678);
  dumpDouble(D, 370.5);
}


/*
** dump Lua function as precompiled chunk
*/
int luaU_dump(lua_State *L, const Proto *f, lua_Writer w, void *data,
              int strip) {
  DumpState D;
  D.L = L;
  D.writer = w;
  D.data = data;
  D.strip = strip;
  D.status = 0;
  D.timestamp = 0;  /* 初始化为0，让dumpFunction设置 */
  D.obfuscate_flags = 0;  /* 默认不启用混淆 */
  D.obfuscate_seed = 0;
  D.log_path = NULL;  /* 不输出日志 */
  dumpHeader(&D);
  dumpByte(&D, f->sizeupvalues);
  dumpFunction(&D, f, NULL);
  return D.status;
}


/*
** dump Lua function as precompiled chunk with control flow flattening
** 带控制流扁平化的字节码导出函数
** 
** @param L Lua状态
** @param f 函数原型
** @param w 写入器函数
** @param data 写入器数据
** @param strip 是否剥离调试信息
** @param obfuscate_flags 混淆标志位（参见lobfuscate.h中的OBFUSCATE_*常量）
** @param seed 随机种子（0表示使用时间作为种子）
** @param log_path 调试日志输出路径（NULL表示不输出日志）
** @return 成功返回0，失败返回错误码
**
** 混淆标志位说明：
** - OBFUSCATE_NONE (0): 不进行混淆
** - OBFUSCATE_CFF (1): 启用控制流扁平化
** - OBFUSCATE_BLOCK_SHUFFLE (2): 随机打乱基本块顺序
** - OBFUSCATE_BOGUS_BLOCKS (4): 插入虚假基本块
** - OBFUSCATE_STATE_ENCODE (8): 状态值编码混淆
**
** 使用示例：
** luaU_dump_obfuscated(L, f, w, data, 1, OBFUSCATE_CFF | OBFUSCATE_BLOCK_SHUFFLE, 0, "cff.log");
*/
int luaU_dump_obfuscated(lua_State *L, const Proto *f, lua_Writer w, void *data,
                         int strip, int obfuscate_flags, unsigned int seed,
                         const char *log_path) {
  DumpState D;
  D.L = L;
  D.writer = w;
  D.data = data;
  D.strip = strip;
  D.status = 0;
  D.timestamp = 0;  /* 初始化为0，让dumpFunction设置 */
  D.obfuscate_flags = obfuscate_flags;
  D.obfuscate_seed = (seed != 0) ? seed : (unsigned int)time(NULL);
  D.log_path = log_path;
  dumpHeader(&D);
  dumpByte(&D, f->sizeupvalues);
  dumpFunction(&D, f, NULL);
  return D.status;
}

