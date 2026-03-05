#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "llex.h"
#include "llexer_compiler.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Parser State */
typedef struct {
    lua_State *L;
    int table_idx;
    int current_idx;
    int num_tokens;
} ParserState;

/* Utility to read current token from Lua lexer table */
static void get_token(ParserState *ps, Token *t, char **val) {
    if (ps->current_idx > ps->num_tokens) {
        t->token = TK_EOS;
        *val = NULL;
        return;
    }

    lua_rawgeti(ps->L, ps->table_idx, ps->current_idx);

    lua_getfield(ps->L, -1, "token");
    t->token = lua_tointeger(ps->L, -1);
    lua_pop(ps->L, 1);

    lua_getfield(ps->L, -1, "value");
    if (lua_isstring(ps->L, -1)) {
        size_t len;
        const char *s = lua_tolstring(ps->L, -1, &len);
        *val = malloc(len + 1);
        strcpy(*val, s);
    } else {
        *val = NULL;
    }
    lua_pop(ps->L, 2); /* pop value and token table */
}

/* Very basic recursive descent to build a simple IR Tree */
static IRNode *new_node(IRNodeType type) {
    IRNode *n = malloc(sizeof(IRNode));
    memset(n, 0, sizeof(IRNode));
    n->type = type;
    return n;
}

static IRNode *parse_expr(ParserState *ps);

/* Naive expression parsing */
static IRNode *parse_expr(ParserState *ps) {
    Token t; char *val;
    get_token(ps, &t, &val);
    ps->current_idx++;

    IRNode *n = NULL;
    if (t.token == TK_NAME) {
        n = new_node(IR_EXPR_NAME);
        n->str_val = val;
    } else if (t.token == TK_INT) {
        n = new_node(IR_EXPR_LITERAL_INT);
        n->int_val = val ? atoi(val) : 0;
        if(val) free(val);
    } else if (t.token == TK_STRING) {
        n = new_node(IR_EXPR_LITERAL_STR);
        n->str_val = val;
    } else if (t.token == '(') {
        if(val) free(val);
        n = parse_expr(ps);
        get_token(ps, &t, &val);
        if (t.token == ')') ps->current_idx++;
        if(val) free(val);
    } else {
        if(val) free(val);
    }

    /* Binary Op lookup (simplified) */
    get_token(ps, &t, &val);
    if (t.token == '+' || t.token == '-' || t.token == '*' || t.token == '/' || t.token == TK_EQ || t.token == '<' || t.token == '>') {
        int op = t.token;
        ps->current_idx++;
        if(val) free(val);

        IRNode *binop = new_node(IR_EXPR_BINOP);
        binop->token = op;
        binop->children[0] = n;
        binop->children[1] = parse_expr(ps);
        return binop;
    }
    if(val) free(val);

    return n;
}

static IRNode *parse_stmt(ParserState *ps) {
    Token t; char *val;
    get_token(ps, &t, &val);

        if (t.token == TK_LOCAL) {
        ps->current_idx++;
        if(val) free(val);

        get_token(ps, &t, &val);
        if (t.token == TK_FUNCTION) {
            ps->current_idx++;
            if(val) free(val);

            get_token(ps, &t, &val);
            IRNode *n = new_node(IR_STMT_FUNCTION);
            n->str_val = val;
            ps->current_idx++;

            get_token(ps, &t, &val);
            if (t.token == '(') ps->current_idx++;
            if(val) free(val);

            /* parse params - simple 1 param for now */
            get_token(ps, &t, &val);
            if (t.token == TK_NAME) {
                n->children[0] = new_node(IR_EXPR_NAME);
                n->children[0]->str_val = val;
                ps->current_idx++;
            } else {
                if(val) free(val);
            }

            get_token(ps, &t, &val);
            if (t.token == ')') ps->current_idx++;
            if(val) free(val);

            /* parse body */
            IRNode *body_head = NULL, *body_tail = NULL;
            while (ps->current_idx <= ps->num_tokens) {
                get_token(ps, &t, &val);
                if (t.token == TK_END) {
                    if(val) free(val);
                    ps->current_idx++;
                    break;
                }
                if(val) free(val);
                IRNode *s = parse_stmt(ps);
                if (s) {
                    if (!body_head) body_head = body_tail = s;
                    else { body_tail->next = s; body_tail = s; }
                }
            }
            n->children[1] = body_head;
            return n;
        }

        /* Not a function, just a local variable */
        IRNode *n = new_node(IR_STMT_LOCAL);
        n->str_val = val;
        ps->current_idx++;

        get_token(ps, &t, &val);
        if (t.token == '=') {
            ps->current_idx++;
            if(val) free(val);
            n->children[0] = parse_expr(ps);
        } else {
            if(val) free(val);
        }
        return n;
    }
 else if (t.token == TK_IF) {
        ps->current_idx++;
        if(val) free(val);

        IRNode *n = new_node(IR_STMT_IF);
        n->children[0] = parse_expr(ps); /* Cond */

        get_token(ps, &t, &val);
        if (t.token == TK_THEN) ps->current_idx++;
        if(val) free(val);

                /* Parse body */
        IRNode *body_head = NULL, *body_tail = NULL;
        IRNode *else_head = NULL, *else_tail = NULL;
        int in_else = 0;

        while (ps->current_idx <= ps->num_tokens) {
            get_token(ps, &t, &val);
            if (t.token == TK_END) {
                if(val) free(val);
                ps->current_idx++;
                break;
            } else if (t.token == TK_ELSE) {
                in_else = 1;
                ps->current_idx++;
                if(val) free(val);
                continue;
            }
            if(val) free(val);
            IRNode *s = parse_stmt(ps);
            if (s) {
                if (in_else) {
                    if (!else_head) else_head = else_tail = s;
                    else { else_tail->next = s; else_tail = s; }
                } else {
                    if (!body_head) body_head = body_tail = s;
                    else { body_tail->next = s; body_tail = s; }
                }
            } else {
                ps->current_idx++; /* fallback skip */
            }
        }
        n->children[1] = body_head;
        n->children[2] = else_head;


        return n;
    } else if (t.token == TK_RETURN) {
        ps->current_idx++;
        if(val) free(val);
        IRNode *n = new_node(IR_STMT_RETURN);
        n->children[0] = parse_expr(ps);
        return n;
    } else if (t.token == TK_NAME) {
        /* Could be assignment or call */
        IRNode *lhs = parse_expr(ps);
        get_token(ps, &t, &val);
        if (t.token == '=') {
            ps->current_idx++;
            if(val) free(val);
            IRNode *n = new_node(IR_STMT_ASSIGN);
            n->children[0] = lhs;
            n->children[1] = parse_expr(ps);
            return n;
        } else if (t.token == '(') {
            ps->current_idx++;
            if(val) free(val);
            IRNode *n = new_node(IR_STMT_CALL);
            n->children[0] = lhs;
            n->children[1] = parse_expr(ps);

            get_token(ps, &t, &val);
            if (t.token == ')') ps->current_idx++;
            if(val) free(val);
            return n;
        }
        if(val) free(val);
    }

    /* Fallback skip */
    ps->current_idx++;
    if(val) free(val);
    return NULL;
}

static IRNode *parse_chunk(ParserState *ps) {
    IRNode *head = NULL, *tail = NULL;
    while (ps->current_idx <= ps->num_tokens) {
        IRNode *s = parse_stmt(ps);
        if (s) {
            if (!head) head = tail = s;
            else { tail->next = s; tail = s; }
        }
    }
    return head;
}

/* Semantic / Scope Analysis */
typedef struct Scope {
    char *locals[100];
    int count;
    struct Scope *parent;
} Scope;

static void analyze_scope(IRNode *node, Scope *s) {
    if (!node) return;

    if (node->type == IR_STMT_LOCAL) {
        if (s->count < 100 && node->str_val) {
            s->locals[s->count++] = node->str_val;
        }
        analyze_scope(node->children[0], s);
        } else if (node->type == IR_EXPR_NAME) {
        /* Check if variable exists in scope chain */
        int found = 0;
        Scope *curr = s;
        while (curr && !found) {
            for (int i = 0; i < curr->count; i++) {
                if (curr->locals[i] && node->str_val && strcmp(curr->locals[i], node->str_val) == 0) {
                    found = 1;
                    break;
                }
            }
            curr = curr->parent;
        }
        /* Optionally warn/error on implicit global if `found == 0` */
    } else {
        /* Recursively analyze children */
        Scope child_scope;
        child_scope.count = 0;
        child_scope.parent = s;

        for (int i = 0; i < 4; i++) {
            if (node->children[i]) {
                if (node->type == IR_STMT_IF && i == 1) {
                    /* Body gets a new scope */
                    analyze_scope(node->children[i], &child_scope);
                } else {
                    analyze_scope(node->children[i], s);
                }
            }
        }
    }

    analyze_scope(node->next, s);
}

/* Control Flow Graph Builder */
static BasicBlock *new_block(CFG *cfg) {
    BasicBlock *b = malloc(sizeof(BasicBlock));
    memset(b, 0, sizeof(BasicBlock));
    b->id = cfg->next_block_id++;
    return b;
}

static BasicBlock *build_cfg(CFG *cfg, IRNode *stmt, BasicBlock *current) {
    while (stmt) {
        if (stmt->type == IR_STMT_IF) {
            /* If condition ends the block */
            IRNode *cond = stmt->children[0];
            IRNode *body = stmt->children[1];

            BasicBlock *true_block = new_block(cfg);
            BasicBlock *end_block = new_block(cfg);

            current->next_true = true_block;
            current->next_false = end_block; /* Simplification: no else block */

            /* Build true branch */
            BasicBlock *true_end = build_cfg(cfg, body, true_block);
            if (true_end) {
                true_end->next = end_block;
            }

            current = end_block;
        } else {
            /* Add statement to current block */
            IRNode *clone = malloc(sizeof(IRNode));
            *clone = *stmt;
            clone->next = NULL;

            if (!current->stmts) {
                current->stmts = clone;
            } else {
                IRNode *tail = current->stmts;
                while (tail->next) tail = tail->next;
                tail->next = clone;
            }

            if (stmt->type == IR_STMT_RETURN) {
                current->next = NULL;
                break;
            }
        }
        stmt = stmt->next;
    }
    return current;
}

/* Control Flow Flattening (CFF) */
/* This converts the CFG back into a single flat AST state machine */
static IRNode *transform_cff(CFG *cfg, BasicBlock *entry) {
    if (!entry) return NULL;

    IRNode *state_var = new_node(IR_STMT_LOCAL);
    state_var->str_val = strdup("__cff_state");

    IRNode *init_val = new_node(IR_EXPR_LITERAL_INT);
    init_val->int_val = entry->id;
    state_var->children[0] = init_val;

    IRNode *while_loop = new_node(IR_STMT_WHILE);
    while_loop->children[0] = new_node(IR_EXPR_LITERAL_BOOL);
    while_loop->children[0]->int_val = 1; /* while true */

    IRNode *head = state_var;
    head->next = while_loop;

    IRNode *switch_head = NULL;
    IRNode *switch_tail = NULL;

    BasicBlock *curr = entry;
    while (curr) {
        IRNode *if_stmt = new_node(IR_STMT_IF);

        IRNode *cond = new_node(IR_EXPR_BINOP);
        cond->token = TK_EQ;
        cond->children[0] = new_node(IR_EXPR_NAME);
        cond->children[0]->str_val = strdup("__cff_state");
        cond->children[1] = new_node(IR_EXPR_LITERAL_INT);
        cond->children[1]->int_val = curr->id;

        if_stmt->children[0] = cond;

        IRNode *block_body = curr->stmts;
        IRNode *last_stmt = NULL;
        if (block_body) {
            IRNode *tmp = block_body;
            while (tmp->next) tmp = tmp->next;
            last_stmt = tmp;
        }

        /* State transition */
        if (!last_stmt || last_stmt->type != IR_STMT_RETURN) {
            IRNode *next_state = new_node(IR_STMT_ASSIGN);
            next_state->children[0] = new_node(IR_EXPR_NAME);
            next_state->children[0]->str_val = strdup("__cff_state");
            next_state->children[1] = new_node(IR_EXPR_LITERAL_INT);
            next_state->children[1]->int_val = curr->next ? curr->next->id : -1;

            if (last_stmt) last_stmt->next = next_state;
            else block_body = next_state;
            next_state->next = new_node(IR_STMT_BREAK);
        }

        if_stmt->children[1] = block_body;

        if (!switch_head) switch_head = switch_tail = if_stmt;
        else { switch_tail->next = if_stmt; switch_tail = if_stmt; }

        curr = curr->next;
    }

        IRNode *if_break = new_node(IR_STMT_IF);
    IRNode *cond_break = new_node(IR_EXPR_BINOP);
    cond_break->token = TK_EQ;
    cond_break->children[0] = new_node(IR_EXPR_NAME);
    cond_break->children[0]->str_val = strdup("__cff_state");
    cond_break->children[1] = new_node(IR_EXPR_LITERAL_INT);
    cond_break->children[1]->int_val = -1;
    if_break->children[0] = cond_break;

    IRNode *brk_stmt = new_node(IR_STMT_CALL);
    brk_stmt = new_node(IR_EXPR_NAME);
    brk_stmt->str_val = strdup("break");
    if_break->children[1] = brk_stmt;

    if (!switch_head) switch_head = switch_tail = if_break;
    else { switch_tail->next = if_break; switch_tail = if_break; }

    while_loop->children[1] = switch_head;

    return head;
}

/* Bogus Control Flow Insertion */
static void insert_bogus_branches(IRNode *node) {
    if (!node) return;

    if (node->type == IR_STMT_ASSIGN || node->type == IR_STMT_CALL) {
        if (rand() % 10 < 3) { /* 30% chance to insert bogus branch */
            IRNode *bogus_if = new_node(IR_STMT_IF);

            /* math.random() > 0.5 */
            IRNode *cond = new_node(IR_EXPR_BINOP);
            cond->token = '>';

            IRNode *call = new_node(IR_EXPR_CALL);
            call->children[0] = new_node(IR_EXPR_NAME);
            call->children[0]->str_val = strdup("math.random");

            cond->children[0] = call;
            cond->children[1] = new_node(IR_EXPR_LITERAL_FLT);
            cond->children[1]->flt_val = 0.5;

            bogus_if->children[0] = cond;

            /* Clone statement for true branch, do nothing for false */
            IRNode *clone = malloc(sizeof(IRNode));
            *clone = *node;
            clone->next = NULL;

            bogus_if->children[1] = clone;

            /* Replace current node with bogus if */
            *node = *bogus_if;
            free(bogus_if);
            return;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (node->children[i]) insert_bogus_branches(node->children[i]);
    }
    insert_bogus_branches(node->next);
}

/* String Encryption */
static void encrypt_strings(IRNode *node) {
    if (!node) return;

    if (node->type == IR_EXPR_LITERAL_STR) {
        if (node->str_val) {
            /* Simple XOR encryption */
            size_t len = strlen(node->str_val);
            char *enc = malloc(len + 1);
            for (size_t i = 0; i < len; i++) {
                enc[i] = node->str_val[i] ^ 0x42;
            }
            enc[len] = '\0';

            IRNode *call = new_node(IR_EXPR_CALL);
            call->children[0] = new_node(IR_EXPR_NAME);
            call->children[0]->str_val = strdup("__decrypt");

            call->children[1] = new_node(IR_EXPR_LITERAL_STR);
            call->children[1]->str_val = enc;

            *node = *call;
            free(call);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (node->children[i]) encrypt_strings(node->children[i]);
        }
    }
    encrypt_strings(node->next);
}

/* Code Generation */
static void generate_code(IRNode *node, luaL_Buffer *B, int indent) {
    if (!node) return;

    char spaces[100];
    memset(spaces, ' ', indent);
    spaces[indent] = '\0';

    if (node->type == IR_STMT_LOCAL) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "local ");
        luaL_addstring(B, node->str_val);
        if (node->children[0]) {
            luaL_addstring(B, " = ");
            generate_code(node->children[0], B, 0);
        }
        luaL_addstring(B, "\n");
    } else if (node->type == IR_STMT_ASSIGN) {
        luaL_addstring(B, spaces);
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, " = ");
        generate_code(node->children[1], B, 0);
        luaL_addstring(B, "\n");
    } else if (node->type == IR_STMT_CALL) {
        luaL_addstring(B, spaces);
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, "(");
        generate_code(node->children[1], B, 0);
        luaL_addstring(B, ")\n");
        } else if (node->type == IR_STMT_IF) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "if ");
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, " then\n");
        generate_code(node->children[1], B, indent + 2);
        if (node->children[2]) {
            luaL_addstring(B, spaces);
            luaL_addstring(B, "else\n");
            generate_code(node->children[2], B, indent + 2);
        }
        luaL_addstring(B, spaces);
        luaL_addstring(B, "end\n");
    }
 else if (node->type == IR_STMT_WHILE) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "while ");
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, " do\n");
        generate_code(node->children[1], B, indent + 2);
        luaL_addstring(B, spaces);
        luaL_addstring(B, "end\n");
    } else if (node->type == IR_STMT_FUNCTION) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "local function ");
        luaL_addstring(B, node->str_val);
        luaL_addstring(B, "(");
        if (node->children[0]) generate_code(node->children[0], B, 0);
        luaL_addstring(B, ")\n");
        generate_code(node->children[1], B, indent + 2);
        luaL_addstring(B, spaces);
        luaL_addstring(B, "end\n");
    } else if (node->type == IR_EXPR_UNOP) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%c", (char)node->token);
        luaL_addstring(B, buf);
        generate_code(node->children[0], B, 0);
    } else if (node->type == IR_STMT_BREAK) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "break\n");
    } else if (node->type == IR_STMT_LIST) {
        generate_code(node->children[0], B, indent);
        /* Check if it's the arg list vs block list */
        if (node->children[0] && node->children[0]->type == IR_EXPR_NAME) luaL_addstring(B, ", ");
        else luaL_addstring(B, "\n");
        generate_code(node->children[1], B, indent);
    } else if (node->type == IR_STMT_RETURN) {
        luaL_addstring(B, spaces);
        luaL_addstring(B, "return ");
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, "\n");
    } else if (node->type == IR_EXPR_NAME) {
        if (strcmp(node->str_val, "break") == 0) luaL_addstring(B, "break\n");
        else luaL_addstring(B, node->str_val);
    } else if (node->type == IR_EXPR_LITERAL_INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", node->int_val);
        luaL_addstring(B, buf);
    } else if (node->type == IR_EXPR_LITERAL_FLT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%f", node->flt_val);
        luaL_addstring(B, buf);
    } else if (node->type == IR_EXPR_LITERAL_STR) {
        luaL_addstring(B, "\"");
        luaL_addstring(B, node->str_val);
        luaL_addstring(B, "\"");
    } else if (node->type == IR_EXPR_LITERAL_BOOL) {
        luaL_addstring(B, node->int_val ? "true" : "false");
    } else if (node->type == IR_EXPR_BINOP) {
        generate_code(node->children[0], B, 0);
        char buf[8];
        if (node->token == TK_EQ) strcpy(buf, " == ");
        else snprintf(buf, sizeof(buf), " %c ", node->token);
        luaL_addstring(B, buf);
        generate_code(node->children[1], B, 0);
    } else if (node->type == IR_EXPR_CALL) {
        generate_code(node->children[0], B, 0);
        luaL_addstring(B, "(");
        generate_code(node->children[1], B, 0);
        luaL_addstring(B, ")");
    }

    if (node->next && indent > 0) {
        generate_code(node->next, B, indent);
    } else if (node->next) {
        luaL_addstring(B, "\n");
        generate_code(node->next, B, 0);
    }
}

/* Inject decryption stub as AST nodes */
static void inject_decrypt_stub(IRNode **ast) {
    IRNode *func = new_node(IR_STMT_FUNCTION);
    func->str_val = strdup("__decrypt");

    IRNode *param = new_node(IR_EXPR_NAME);
    param->str_val = strdup("s");
    func->children[0] = param;

    IRNode *body_list = new_node(IR_STMT_LIST);

    IRNode *loc_res = new_node(IR_STMT_LOCAL);
    loc_res->str_val = strdup("res");
    loc_res->children[0] = new_node(IR_EXPR_LITERAL_STR);
    loc_res->children[0]->str_val = strdup("");
    body_list->children[0] = loc_res;

    IRNode *loc_i = new_node(IR_STMT_LOCAL);
    loc_i->str_val = strdup("i");
    loc_i->children[0] = new_node(IR_EXPR_LITERAL_INT);
    loc_i->children[0]->int_val = 1;
    loc_res->next = loc_i;

    IRNode *while_loop = new_node(IR_STMT_WHILE);

    IRNode *cond = new_node(IR_EXPR_BINOP);
    cond->token = '<';
    cond->children[0] = new_node(IR_EXPR_NAME);
    cond->children[0]->str_val = strdup("i");

    IRNode *len_s = new_node(IR_EXPR_UNOP);
    len_s->token = '#';
    len_s->children[0] = new_node(IR_EXPR_NAME);
    len_s->children[0]->str_val = strdup("s");
    cond->children[1] = len_s;

    while_loop->children[0] = cond;
    loc_i->next = while_loop;

    IRNode *assign_res = new_node(IR_STMT_ASSIGN);
    assign_res->children[0] = new_node(IR_EXPR_NAME);
    assign_res->children[0]->str_val = strdup("res");

    IRNode *concat = new_node(IR_EXPR_BINOP);
    concat->token = '+'; /* Actually just emit string manually, since + is used for concat in basic AST test if TK_CONCAT fails */
    concat->children[0] = new_node(IR_EXPR_NAME);
    concat->children[0]->str_val = strdup("res");

    IRNode *char_call = new_node(IR_EXPR_CALL);
    char_call->children[0] = new_node(IR_EXPR_NAME);
    char_call->children[0]->str_val = strdup("string.char");

    IRNode *bxor = new_node(IR_EXPR_BINOP);
    bxor->token = '~';

    IRNode *byte_call = new_node(IR_EXPR_CALL);
    byte_call->children[0] = new_node(IR_EXPR_NAME);
    byte_call->children[0]->str_val = strdup("string.byte");

    IRNode *byte_args = new_node(IR_STMT_LIST);
    byte_args->children[0] = new_node(IR_EXPR_NAME);
    byte_args->children[0]->str_val = strdup("s");
    byte_args->children[1] = new_node(IR_EXPR_NAME);
    byte_args->children[1]->str_val = strdup("i");
    byte_call->children[1] = byte_args;

    bxor->children[0] = byte_call;
    bxor->children[1] = new_node(IR_EXPR_LITERAL_INT);
    bxor->children[1]->int_val = 0x42;

    char_call->children[1] = bxor;
    concat->children[1] = char_call;

    assign_res->children[1] = concat;

    IRNode *inc_i = new_node(IR_STMT_ASSIGN);
    inc_i->children[0] = new_node(IR_EXPR_NAME);
    inc_i->children[0]->str_val = strdup("i");

    IRNode *add_i = new_node(IR_EXPR_BINOP);
    add_i->token = '+';
    add_i->children[0] = new_node(IR_EXPR_NAME);
    add_i->children[0]->str_val = strdup("i");
    add_i->children[1] = new_node(IR_EXPR_LITERAL_INT);
    add_i->children[1]->int_val = 1;
    inc_i->children[1] = add_i;

    assign_res->next = inc_i;
    while_loop->children[1] = assign_res;

    IRNode *ret = new_node(IR_STMT_RETURN);
    ret->children[0] = new_node(IR_EXPR_NAME);
    ret->children[0]->str_val = strdup("res");
    while_loop->next = ret;

    func->children[1] = body_list;
    func->next = *ast;
    *ast = func;
}


/* API Entry Point */
LUAMOD_API int lua_lexer_obfuscate(lua_State *L, const char *code, int cff, int bogus, int str_enc) {
    /* 1. First run llexer to get tokens */
    lua_getglobal(L, "require");
    lua_pushstring(L, "lexer");
    lua_call(L, 1, 1);

    lua_pushstring(L, "lex");
    lua_gettable(L, -2);
    lua_pushstring(L, code);
    lua_call(L, 1, 1);

    if (!lua_istable(L, -1)) {
        luaL_error(L, "lexer returned non-table");
    }

    ParserState ps;
    ps.L = L;
    ps.table_idx = lua_gettop(L);
    ps.current_idx = 1;
    ps.num_tokens = luaL_len(L, ps.table_idx);

    /* 2. Parse into AST */
    IRNode *ast = parse_chunk(&ps);

    /* 3. Semantic Analysis */
    Scope global_scope;
    global_scope.count = 0;
    global_scope.parent = NULL;
    analyze_scope(ast, &global_scope);

    /* 4. Obfuscation Passes */
    if (str_enc) {
        encrypt_strings(ast);
        inject_decrypt_stub(&ast);
    }

    if (bogus) {
        insert_bogus_branches(ast);
    }

    if (cff) {
        CFG cfg;
        cfg.next_block_id = 1;
        BasicBlock *entry = new_block(&cfg);
        build_cfg(&cfg, ast, entry);
        ast = transform_cff(&cfg, entry);
    }

    /* 5. Code Generation */
    luaL_Buffer b;
    luaL_buffinit(L, &b);



    generate_code(ast, &b, 0);
    luaL_pushresult(&b);

    return 1;
}
