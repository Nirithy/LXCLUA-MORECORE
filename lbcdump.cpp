/*
** lbcdump.c - Lua字节码查看器
** 用于调试和分析编译后的Lua字节码（无加密格式）
** DifierLine
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
** 字节码文件头部常量
*/
#define LUA_SIGNATURE   "\x1bLua"
#define LUAC_DATA       "\x19\x93\r\n\x1a\n"

/*
** 指令格式定义（64位指令）
*/
#define SIZE_OP     9
#define SIZE_A      16
#define SIZE_B      16
#define SIZE_C      16
#define SIZE_Bx     33
#define SIZE_sJ     49

#define POS_OP      0
#define POS_A       (POS_OP + SIZE_OP)
#define POS_k       (POS_A + SIZE_A)
#define POS_B       (POS_k + 1)
#define POS_C       (POS_B + SIZE_B)
#define POS_Bx      POS_k
#define POS_sJ      POS_A

#define MASK1(n,p)  ((~((~(uint64_t)0)<<(n)))<<(p))

#define GET_OPCODE(i)   ((int)(((i)>>POS_OP) & MASK1(SIZE_OP,0)))
#define GETARG_A(i)     ((int)(((i)>>POS_A) & MASK1(SIZE_A,0)))
#define GETARG_B(i)     ((int)(((i)>>POS_B) & MASK1(SIZE_B,0)))
#define GETARG_C(i)     ((int)(((i)>>POS_C) & MASK1(SIZE_C,0)))
#define GETARG_k(i)     ((int)(((i)>>POS_k) & 1))
#define GETARG_Bx(i)    ((int)(((i)>>POS_Bx) & MASK1(SIZE_Bx,0)))
#define GETARG_sBx(i)   ((int)(GETARG_Bx(i) - (((int64_t)1<<(SIZE_Bx-1))-1)))
#define GETARG_sJ(i)    ((int)(((i)>>POS_sJ) & MASK1(SIZE_sJ,0)) - ((int64_t)1<<(SIZE_sJ-1)))

#define OFFSET_sC   (((1<<SIZE_C)-1) >> 1)
#define sC2int(i)   ((i) - OFFSET_sC)

/*
** 操作码名称表（需要与lopcodes.h保持同步）
*/
static const char *opcode_names[] = {
    "MOVE",         /* 0 */
    "LOADI",        /* 1 */
    "LOADF",        /* 2 */
    "LOADK",        /* 3 */
    "LOADKX",       /* 4 */
    "LOADFALSE",    /* 5 */
    "LFALSESKIP",   /* 6 */
    "LOADTRUE",     /* 7 */
    "LOADNIL",      /* 8 */
    "GETUPVAL",     /* 9 */
    "SETUPVAL",     /* 10 */
    "GETTABUP",     /* 11 */
    "GETTABLE",     /* 12 */
    "GETI",         /* 13 */
    "GETFIELD",     /* 14 */
    "SETTABUP",     /* 15 */
    "SETTABLE",     /* 16 */
    "SETI",         /* 17 */
    "SETFIELD",     /* 18 */
    "NEWTABLE",     /* 19 */
    "SELF",         /* 20 */
    "ADDI",         /* 21 */
    "ADDK",         /* 22 */
    "SUBK",         /* 23 */
    "MULK",         /* 24 */
    "MODK",         /* 25 */
    "POWK",         /* 26 */
    "DIVK",         /* 27 */
    "IDIVK",        /* 28 */
    "BANDK",        /* 29 */
    "BORK",         /* 30 */
    "BXORK",        /* 31 */
    "SHLI",         /* 32 */
    "SHRI",         /* 33 */
    "ADD",          /* 34 */
    "SUB",          /* 35 */
    "MUL",          /* 36 */
    "MOD",          /* 37 */
    "POW",          /* 38 */
    "DIV",          /* 39 */
    "IDIV",         /* 40 */
    "BAND",         /* 41 */
    "BOR",          /* 42 */
    "BXOR",         /* 43 */
    "SHL",          /* 44 */
    "SHR",          /* 45 */
    "SPACESHIP",    /* 46 */
    "MMBIN",        /* 47 */
    "MMBINI",       /* 48 */
    "MMBINK",       /* 49 */
    "UNM",          /* 50 */
    "BNOT",         /* 51 */
    "NOT",          /* 52 */
    "LEN",          /* 53 */
    "CONCAT",       /* 54 */
    "CLOSE",        /* 55 */
    "TBC",          /* 56 */
    "JMP",          /* 57 */
    "EQ",           /* 58 */
    "LT",           /* 59 */
    "LE",           /* 60 */
    "EQK",          /* 61 */
    "EQI",          /* 62 */
    "LTI",          /* 63 */
    "LEI",          /* 64 */
    "GTI",          /* 65 */
    "GEI",          /* 66 */
    "TEST",         /* 67 */
    "TESTSET",      /* 68 */
    "CALL",         /* 69 */
    "TAILCALL",     /* 70 */
    "RETURN",       /* 71 */
    "RETURN0",      /* 72 */
    "RETURN1",      /* 73 */
    "FORLOOP",      /* 74 */
    "FORPREP",      /* 75 */
    "TFORPREP",     /* 76 */
    "TFORCALL",     /* 77 */
    "TFORLOOP",     /* 78 */
    "SETLIST",      /* 79 */
    "CLOSURE",      /* 80 */
    "VARARG",       /* 81 */
    "GETVARG",      /* 82 */
    "ERRNNIL",      /* 83 */
    "VARARGPREP",   /* 84 */
    "IS",           /* 85 */
    "TESTNIL",      /* 86 */
    "NEWCLASS",     /* 87 */
    "INHERIT",      /* 88 */
    "GETSUPER",     /* 89 */
    "SETMETHOD",    /* 90 */
    "SETSTATIC",    /* 91 */
    "NEWOBJ",       /* 92 */
    "GETPROP",      /* 93 */
    "SETPROP",      /* 94 */
    "INSTANCEOF",   /* 95 */
    "IMPLEMENT",    /* 96 */
    "SETIFACEFLAG", /* 97 */
    "ADDMETHOD",    /* 98 */
    "SLICE",        /* 99 */
    "NOP",          /* 100 */
    "EXTRAARG"      /* 101 */
};

#define OPCODE_COUNT (sizeof(opcode_names) / sizeof(opcode_names[0]))

/*
** 文件读取状态
*/
typedef struct {
    unsigned char *data;
    size_t size;
    int pos;
} LoadState;

/*
** 读取单个字节
** @param S 加载状态
** @return 读取的字节，错误返回-1
*/
static int loadByte(LoadState *S) {
    if (S->pos >= (int)S->size) {
        return -1;
    }
    return S->data[S->pos++];
}

/*
** 读取数据块
** @param S 加载状态
** @param b 目标缓冲区
** @param size 读取大小
** @return 成功返回1，失败返回0
*/
static int loadBlock(LoadState *S, void *b, size_t size) {
    if (S->pos + size > S->size) {
        return 0;
    }
    memcpy(b, S->data + S->pos, size);
    S->pos += (int)size;
    return 1;
}

/*
** 读取变长无符号整数
** @param S 加载状态
** @return 读取的整数
*/
static size_t loadUnsigned(LoadState *S) {
    size_t x = 0;
    int b;
    do {
        b = loadByte(S);
        if (b < 0) return 0;
        x = (x << 7) | (b & 0x7f);
    } while ((b & 0x80) == 0);
    return x;
}

/*
** 读取整数
** @param S 加载状态
** @return 读取的整数
*/
static int loadInt(LoadState *S) {
    return (int)loadUnsigned(S);
}

/*
** 读取size_t
** @param S 加载状态
** @return 读取的size
*/
static size_t loadSize(LoadState *S) {
    return loadUnsigned(S);
}

/*
** 操作码格式类型
*/
typedef enum {
    iFMT_ABC,   /* A B C */
    iFMT_ABx,   /* A Bx */
    iFMT_AsBx,  /* A sBx */
    iFMT_Ax,    /* Ax */
    iFMT_sJ,    /* sJ */
    iFMT_AB,    /* A B (C unused) */
    iFMT_A,     /* A only */
    iFMT_NONE   /* no args */
} OpFormat;

/*
** 获取操作码格式
** @param op 操作码
** @return 格式类型
*/
static OpFormat getOpFormat(int op) {
    switch (op) {
        /* sJ格式 - JMP */
        case 57:
            return iFMT_sJ;
        
        /* ABx格式 */
        case 1:  /* LOADI */
        case 2:  /* LOADF */
        case 3:  /* LOADK */
        case 4:  /* LOADKX */
        case 74: /* FORLOOP */
        case 75: /* FORPREP */
        case 76: /* TFORPREP */
        case 78: /* TFORLOOP */
        case 80: /* CLOSURE */
        case 83: /* ERRNNIL */
        case 87: /* NEWCLASS */
            return iFMT_ABx;
        
        /* A格式 */
        case 5:  /* LOADFALSE */
        case 6:  /* LFALSESKIP */
        case 7:  /* LOADTRUE */
        case 72: /* RETURN0 */
        case 84: /* VARARGPREP */
        case 97: /* SETIFACEFLAG */
            return iFMT_A;
        
        /* AB格式 */
        case 0:  /* MOVE */
        case 8:  /* LOADNIL */
        case 9:  /* GETUPVAL */
        case 10: /* SETUPVAL */
        case 50: /* UNM */
        case 51: /* BNOT */
        case 52: /* NOT */
        case 53: /* LEN */
        case 73: /* RETURN1 */
        case 88: /* INHERIT */
        case 96: /* IMPLEMENT */
            return iFMT_AB;
        
        /* 无参数 - NOP */
        case 100:
            return iFMT_NONE;
        
        /* Ax格式 - EXTRAARG */
        case 101:
            return iFMT_Ax;
        
        /* 默认ABC格式 */
        default:
            return iFMT_ABC;
    }
}

/*
** 打印单条指令
** @param pc 程序计数器
** @param inst 指令
*/
static void printInstruction(int pc, uint64_t inst) {
    int op = GET_OPCODE(inst);
    const char *name = (op < (int)OPCODE_COUNT) ? opcode_names[op] : "UNKNOWN";
    OpFormat fmt = getOpFormat(op);
    
    printf("%4d\t", pc);
    
    switch (fmt) {
        case iFMT_ABC: {
            int a = GETARG_A(inst);
            int b = GETARG_B(inst);
            int c = GETARG_C(inst);
            int k = GETARG_k(inst);
            if (k) {
                printf("%-12s\t%d %d %d k=%d", name, a, b, c, k);
            } else {
                printf("%-12s\t%d %d %d", name, a, b, c);
            }
            
            /* 特殊指令的额外信息 */
            if (op == 58 || op == 59 || op == 60) { /* EQ, LT, LE */
                printf("\t; if %s then skip", k ? "false" : "true");
            } else if (op >= 62 && op <= 66) { /* EQI, LTI, LEI, GTI, GEI */
                printf("\t; compare with %d", sC2int(b));
            }
            break;
        }
        case iFMT_ABx: {
            int a = GETARG_A(inst);
            int bx = GETARG_Bx(inst);
            printf("%-12s\t%d %d", name, a, bx);
            if (op == 1) { /* LOADI */
                printf("\t; R[%d] := %d", a, GETARG_sBx(inst));
            }
            break;
        }
        case iFMT_AsBx: {
            int a = GETARG_A(inst);
            int sbx = GETARG_sBx(inst);
            printf("%-12s\t%d %d", name, a, sbx);
            break;
        }
        case iFMT_Ax: {
            int ax = (int)((inst >> POS_A) & MASK1(49, 0));
            printf("%-12s\t%d", name, ax);
            break;
        }
        case iFMT_sJ: {
            int sj = GETARG_sJ(inst);
            printf("%-12s\t%d", name, sj);
            printf("\t; to %d", pc + 1 + sj);
            break;
        }
        case iFMT_AB: {
            int a = GETARG_A(inst);
            int b = GETARG_B(inst);
            printf("%-12s\t%d %d", name, a, b);
            break;
        }
        case iFMT_A: {
            int a = GETARG_A(inst);
            printf("%-12s\t%d", name, a);
            break;
        }
        case iFMT_NONE:
            printf("%-12s", name);
            break;
    }
    
    printf("\n");
}

/*
** 跳过字符串
** @param S 加载状态
*/
static void skipString(LoadState *S) {
    size_t size = loadSize(S);
    if (size > 0) {
        S->pos += (int)(size - 1);
    }
}

/*
** 解析并显示函数的字节码
** @param S 加载状态
** @param func_name 函数名称
** @param depth 递归深度
*/
static void dumpFunction(LoadState *S, const char *func_name, int depth);

/*
** 跳过常量表
** @param S 加载状态
*/
static void skipConstants(LoadState *S) {
    int n = loadInt(S);
    printf("  常量数量: %d\n", n);
    
    /* 常量类型定义 (与lobject.h同步):
    ** LUA_VNIL = 0, LUA_VFALSE = 1, LUA_VTRUE = 17
    ** LUA_VNUMINT = 3, LUA_VNUMFLT = 19
    ** LUA_VSHRSTR = 4, LUA_VLNGSTR = 20
    */
    for (int i = 0; i < n; i++) {
        int t = loadByte(S);
        switch (t) {
            case 0:  /* LUA_VNIL */
            case 1:  /* LUA_VFALSE */
            case 17: /* LUA_VTRUE */
                break;
            case 19: /* LUA_VNUMFLT */
                S->pos += 8; /* double */
                break;
            case 3:  /* LUA_VNUMINT */
                S->pos += 8; /* int64 */
                break;
            case 4:  /* LUA_VSHRSTR */
            case 20: /* LUA_VLNGSTR */
                skipString(S);
                break;
            default:
                printf("  警告: 未知常量类型 %d at pos %d\n", t, S->pos);
                break;
        }
    }
}

/*
** 跳过upvalues
** @param S 加载状态
*/
static void skipUpvalues(LoadState *S) {
    int n = loadInt(S);
    printf("  Upvalues数量: %d\n", n);
    S->pos += n * 3; /* 每个upvalue 3字节 (instack, idx, kind) */
}

/*
** 处理子函数原型
** @param S 加载状态
** @param depth 递归深度
*/
static void loadProtos(LoadState *S, int depth) {
    int n = loadInt(S);
    printf("  子函数数量: %d\n", n);
    for (int i = 0; i < n; i++) {
        char name[64];
        snprintf(name, sizeof(name), "子函数#%d", i);
        dumpFunction(S, name, depth + 1);
    }
}

/*
** 跳过调试信息
** @param S 加载状态
*/
static void skipDebug(LoadState *S) {
    int n, i;
    
    /* lineinfo */
    n = loadInt(S);
    S->pos += n;
    
    /* abslineinfo */
    n = loadInt(S);
    for (i = 0; i < n; i++) {
        loadInt(S); /* pc */
        loadInt(S); /* line */
    }
    
    /* locvars */
    n = loadInt(S);
    for (i = 0; i < n; i++) {
        skipString(S); /* varname */
        loadInt(S); /* startpc */
        loadInt(S); /* endpc */
    }
    
    /* upvalue names */
    n = loadInt(S);
    for (i = 0; i < n; i++) {
        skipString(S);
    }
}

/*
** 解析并显示函数的字节码
** @param S 加载状态
** @param func_name 函数名称
** @param depth 递归深度
*/
static void dumpFunction(LoadState *S, const char *func_name, int depth) {
    /* 缩进 */
    char indent[64] = "";
    for (int i = 0; i < depth * 2 && i < 60; i++) {
        indent[i] = ' ';
    }
    indent[depth * 2 < 60 ? depth * 2 : 60] = '\0';
    
    printf("\n%s=== 函数: %s ===\n", indent, func_name);
    
    /* 读取源文件名 */
    skipString(S);
    
    /* 读取函数信息 */
    int linedefined = loadInt(S);
    int lastlinedefined = loadInt(S);
    int numparams = loadByte(S);
    int is_vararg = loadByte(S);
    int maxstacksize = loadByte(S);
    
    printf("%s行范围: %d - %d\n", indent, linedefined, lastlinedefined);
    printf("%s参数数量: %d, 可变参数: %d, 栈大小: %d\n", 
           indent, numparams, is_vararg, maxstacksize);
    
    /* 读取指令 */
    int code_size = loadInt(S);
    printf("%s指令数量: %d\n", indent, code_size);
    
    if (code_size > 0 && S->pos + code_size * 8 <= (int)S->size) {
        printf("\n%sPC\tOpcode\t\tArguments\n", indent);
        printf("%s--\t------\t\t---------\n", indent);
        
        for (int i = 0; i < code_size; i++) {
            uint64_t inst = 0;
            loadBlock(S, &inst, sizeof(inst));
            printf("%s", indent);
            printInstruction(i, inst);
        }
    } else {
        printf("%s错误: 指令数据不完整\n", indent);
        S->pos += code_size * 8;
    }
    
    /* 跳过其他段 */
    skipConstants(S);
    skipUpvalues(S);
    loadProtos(S, depth);
    skipDebug(S);
}

/*
** 从字节码文件读取并显示指令
** @param filename 字节码文件路径
*/
static void dumpBytecodeFile(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "错误: 无法打开文件 '%s'\n", filename);
        return;
    }
    
    /* 读取整个文件 */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        fprintf(stderr, "错误: 内存分配失败\n");
        fclose(f);
        return;
    }
    
    if (fread(data, 1, size, f) != (size_t)size) {
        fprintf(stderr, "错误: 读取文件失败\n");
        free(data);
        fclose(f);
        return;
    }
    fclose(f);
    
    printf("=== 字节码文件: %s ===\n", filename);
    printf("文件大小: %ld 字节\n", size);
    
    /* 初始化加载状态 */
    LoadState S;
    S.data = data;
    S.size = size;
    S.pos = 0;
    
    /* 检查签名 */
    if (size < 4 || memcmp(data, LUA_SIGNATURE, 4) != 0) {
        fprintf(stderr, "错误: 不是有效的Lua字节码文件\n");
        free(data);
        return;
    }
    S.pos = 4;
    
    /* 读取头部 */
    int version = loadByte(&S);
    int format = loadByte(&S);
    printf("\n=== 头部信息 ===\n");
    printf("签名: \\x1bLua (OK)\n");
    printf("版本: 0x%02X (Lua %d.%d)\n", version, (version >> 4), (version & 0x0F));
    printf("格式: 0x%02X\n", format);
    
    /* 跳过 LUAC_DATA */
    S.pos += sizeof(LUAC_DATA) - 1;
    
    /* 读取类型大小 */
    int inst_size = loadByte(&S);
    int int_size = loadByte(&S);
    int num_size = loadByte(&S);
    printf("指令大小: %d 字节\n", inst_size);
    printf("整数大小: %d 字节\n", int_size);
    printf("浮点数大小: %d 字节\n", num_size);
    
    /* 跳过测试整数和浮点数 */
    S.pos += int_size + num_size;
    
    /* 读取upvalues数量 */
    int sizeupvalues = loadByte(&S);
    printf("主函数Upvalues: %d\n", sizeupvalues);
    
    /* 解析主函数 */
    dumpFunction(&S, "main", 0);
    
    free(data);
    printf("\n=== 解析完成 ===\n");
}

/*
** 主函数
*/
int main(int argc, char *argv[]) {
    printf("===========================================\n");
    printf("  Lua 字节码查看器 (lbcdump)\n");
    printf("  DifierLine - 用于调试CFF混淆\n");
    printf("===========================================\n");
    
    if (argc < 2) {
        printf("\n用法: %s <bytecode_file.luac>\n", argv[0]);
        printf("\n示例:\n");
        printf("  %s test.luac\n", argv[0]);
        return 1;
    }
    
    dumpBytecodeFile(argv[1]);
    
    return 0;
}
