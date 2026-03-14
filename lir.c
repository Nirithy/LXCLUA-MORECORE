/*
** $Id: lir.c $
** SSA Intermediate Representation (IR) Implementation for LXCLUA-NCore
*/

#define lir_c
#define LUA_CORE

#include "lprefix.h"
#include "lir.h"
#include "lmem.h"
#include <string.h>

#define INITIAL_BLOCKS 16
#define INITIAL_EDGES 4

LIRFunc *lir_new_func(lua_State *L, Proto *p) {
    LIRFunc *f = luaM_new(L, LIRFunc);
    if (!f) return NULL;

    f->L = L;
    f->p = p;
    f->num_blocks = 0;
    f->capacity_blocks = INITIAL_BLOCKS;
    f->blocks = luaM_newvector(L, f->capacity_blocks, LIRBlock*);
    f->entry = NULL;
    f->exit = NULL;
    f->next_vreg = 0;
    f->next_block_id = 0;
    f->opt_mode = LIR_OPT_NONE;

    return f;
}

static void lir_free_block(lua_State *L, LIRBlock *b) {
    if (!b) return;

    /* Free Instructions */
    LIRInst *curr = b->first;
    while (curr) {
        LIRInst *next = curr->next;
        luaM_free(L, curr);
        curr = next;
    }

    /* Free Edge Arrays */
    if (b->preds) luaM_freearray(L, b->preds, b->capacity_preds);
    if (b->succs) luaM_freearray(L, b->succs, b->capacity_succs);

    luaM_free(L, b);
}

void lir_free_func(LIRFunc *f) {
    if (!f) return;
    lua_State *L = f->L;

    for (int i = 0; i < f->num_blocks; i++) {
        lir_free_block(L, f->blocks[i]);
    }

    if (f->blocks) {
        luaM_freearray(L, f->blocks, f->capacity_blocks);
    }

    luaM_free(L, f);
}

LIRBlock *lir_new_block(LIRFunc *f) {
    lua_State *L = f->L;
    LIRBlock *b = luaM_new(L, LIRBlock);

    b->id = f->next_block_id++;
    b->first = NULL;
    b->last = NULL;

    b->num_preds = 0;
    b->capacity_preds = INITIAL_EDGES;
    b->preds = luaM_newvector(L, b->capacity_preds, LIRBlock*);

    b->num_succs = 0;
    b->capacity_succs = INITIAL_EDGES;
    b->succs = luaM_newvector(L, b->capacity_succs, LIRBlock*);

    if (f->num_blocks >= f->capacity_blocks) {
        int new_cap = f->capacity_blocks * 2;
        luaM_reallocvector(L, f->blocks, f->capacity_blocks, new_cap, LIRBlock*);
        f->capacity_blocks = new_cap;
    }

    f->blocks[f->num_blocks++] = b;

    return b;
}

void lir_add_edge(LIRFunc *f, LIRBlock *from, LIRBlock *to) {
    if (!from || !to) return;
    lua_State *L = f->L;

    /* Add to successors of `from` */
    if (from->num_succs >= from->capacity_succs) {
        int new_cap = from->capacity_succs * 2;
        luaM_reallocvector(L, from->succs, from->capacity_succs, new_cap, LIRBlock*);
        from->capacity_succs = new_cap;
    }
    from->succs[from->num_succs++] = to;

    /* Add to predecessors of `to` */
    if (to->num_preds >= to->capacity_preds) {
        int new_cap = to->capacity_preds * 2;
        luaM_reallocvector(L, to->preds, to->capacity_preds, new_cap, LIRBlock*);
        to->capacity_preds = new_cap;
    }
    to->preds[to->num_preds++] = from;
}

LIRInst *lir_emit(LIRFunc *f, LIRBlock *b, LIROpCode op) {
    if (!b) return NULL;
    lua_State *L = f->L;

    LIRInst *inst = luaM_new(L, LIRInst);
    memset(inst, 0, sizeof(LIRInst));
    inst->op = op;
    inst->num_args = 0;

    /* Append to block */
    inst->prev = b->last;
    inst->next = NULL;

    if (b->last) {
        b->last->next = inst;
    } else {
        b->first = inst;
    }
    b->last = inst;

    return inst;
}
