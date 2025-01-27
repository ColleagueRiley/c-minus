#include "stb_c_lexer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define CMINUS_ENUM(type, name) type name; enum
#define CMINUS_BIT(x) 1L << x

typedef CMINUS_ENUM(uint32_t, cminus_state_type) {
    cminus_no_state = 0,
    cminus_declare = CMINUS_BIT(0),
    cminus_define = CMINUS_BIT(1),
    cminus_opp = CMINUS_BIT(2),
    cminus_func = CMINUS_BIT(3),
    cminus_var = CMINUS_BIT(4),
    cminus_static = CMINUS_BIT(5),
    cminus_set = CMINUS_BIT(6),
};

#define MAX_STACK 1024
#define MAX_SYM_NAME 255
#define MAX_SYMS 1024
#define MAX_ARGS 20

typedef struct cminus_state {
    cminus_state_type type;
    stb_lexer *lexer;
    FILE* asm_file;
    size_t asm_index;
    size_t scope;
    size_t stack_len[MAX_STACK];

    char sym[MAX_ARGS][MAX_SYM_NAME]; /* name of the current sym */
    size_t sym_count;

    stb_lexer prev;
    stb_lexer next;
} cminus_state;

inline void cminus_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asm_file);
inline void cminus_handle_token(cminus_state* state);
inline void cminus_handle_keyword(cminus_state* state, bool func);
inline void cminus_write_line(cminus_state* state, const char* format, ...);

typedef struct cminus_sym {
    char sym[MAX_SYM_NAME];
    size_t scope, index;
} cminus_sym;

inline void cminus_push_sym(char* name, size_t index, size_t scope);
inline cminus_sym* cminus_find_sym(char* sym, size_t scope);
inline void cminus_pop_sym(size_t scope);

#ifdef CMINUS_PARSER_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#define STB_C_LEXER_IMPLEMENTATION
#include "stb_c_lexer.h"

cminus_sym cminus_syms[MAX_STACK][MAX_SYMS];
size_t cminus_sym_count[MAX_STACK];

void cminus_write_line(cminus_state* state, const char* format, ...) {
    size_t max = state->asm_index + (state->scope * 4);

    for (state->asm_index; state->asm_index < max; state->asm_index++) {
        fprintf(state->asm_file, " ");
    }

    va_list args;
    va_start(args, format);
    vfprintf(state->asm_file, format, args);
    va_end(args);

    fprintf(state->asm_file, "\n");
}

void cminus_push_sym(char* name, size_t index, size_t scope) {            
    cminus_syms[scope][cminus_sym_count[scope]].index = index;
    cminus_syms[scope][cminus_sym_count[scope]].scope = scope;
    strncpy(cminus_syms[scope][cminus_sym_count[scope]].sym, name, 255);

    cminus_sym_count[scope]++;
}

cminus_sym* cminus_find_sym(char* sym, size_t scope) {
    for (size_t i = 0; i < cminus_sym_count[scope]; i++)
        if (strcmp(sym, cminus_syms[scope][i].sym) == 0)
            return &cminus_syms[scope][i];
    
    if (scope)
        return cminus_find_sym(sym, 0);
    else { 
        fprintf(stderr, "Error: symbol not found: %s\n", sym);
        exit(1);
    }
}

void cminus_pop_sym(size_t scope) {
    if (scope == 0) return;
    cminus_sym_count[scope]--;
}


int32_t cminus_load_rvalue(cminus_state* state, char* reg) {
    int32_t val = 0;
    switch (state->prev.token) {
        case CLEX_intlit:
            val = state->prev.int_number;
            if (state->scope)  cminus_write_line(state, "mov %s, %i", reg, val);
            break;
        case CLEX_id: {
            if (strcmp(state->sym[0], state->prev.string) == 0)
                break;
            
            cminus_sym* sym = cminus_find_sym(state->prev.string, state->scope);
            val =  0;
            if (state->scope && sym->scope) 
                cminus_write_line(state, "mov %s, [esp + %lu]", reg, sym->index * 4);
            else if (state->scope) {
                cminus_write_line(state, "mov %s, %s", reg, sym->sym);
            }
            else {
                fprintf(stderr, "error: global variable rvalue must be a constant\n");
                exit(1);
            }
        }
        default: break;
    }
    
    return val;
}

void cminus_load_standard(cminus_state* state) {
    cminus_write_line(state, "sys_exit:");
    state->scope = 1;
    cminus_write_line(state, "mov ebx, eax");
    cminus_write_line(state, "mov eax, 1");
    cminus_write_line(state, "int  0x80");
    state->scope = 0;
}

void cminus_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asm_file) {
    cminus_state state = {0};
    state.asm_file = asm_file;
    cminus_write_line(&state, "jmp _start");
    cminus_load_standard(&state);
    cminus_write_line(&state, "section .data");

    stb_lexer lex;
    stb_c_lexer_init(&lex, file, file + file_len, string_buffer, string_len);
    while (stb_c_lexer_get_token(&lex)) {
        if (lex.token == CLEX_parse_error) {
            fprintf(stderr, "stb_c_lexer.h: fatal parse error\n");
            break;
        }

        state.lexer = &lex;
        cminus_handle_token(&state);
        
        stb_lexer next = lex;
        stb_c_lexer_get_token(&next);
        state.next = next;
        state.prev = lex;
    }

    cminus_write_line(&state, "\n_start:");
    state.scope = 1;
    cminus_write_line(&state, "call main");
    cminus_write_line(&state, "mov eax, 0");
    cminus_write_line(&state, "call sys_exit");
    state.scope = 0;
}

void cminus_handle_keyword(cminus_state* state, bool func) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->keyword) {
        /* ctypes */
        case CLEX_long: case CLEX_const: case CLEX_signed: case CLEX_static: case CLEX_unsigned: case CLEX_extern: case CLEX_char: case CLEX_double: case CLEX_float: case CLEX_short: case CLEX_int: case CLEX_void:
            state->type = cminus_declare; break;
        case CLEX_do: printf("warning: ignored token: do\n"); break;
        case CLEX_case: printf("warning: ignored token: case\n"); break;
        case CLEX_default: printf("warning: ignored token: default\n"); break;
        case CLEX_break:  printf("warning: ignored token: break\n"); break;
        case CLEX_continue: printf("warning: ignored token: contiune\n"); break;
        case CLEX_return: printf("warning: ignored token: return\n"); break;
        case CLEX_for:  printf("warning: ignored token: for\n"); break;
        case CLEX_while: printf("warning: ignored token: while\n"); break;
        case CLEX_goto: printf("warning: ignored token: goto\n"); break;
        case CLEX_if:  printf("warning: ignored token: if\n"); break;
        case CLEX_else: printf("warning: ignored token: else\n"); break;
        case CLEX_sizeof: printf("warning: ignored token: sizeof\n"); break;
        case CLEX_switch: printf("warning: ignored token: switch\n"); break;
        case CLEX_struct: printf("warning: ignored token: struct\n"); break;
        case CLEX_enum: printf("warning: ignored token: enum\n"); break;
        case CLEX_union: printf("warning: ignored token: union\n"); break;
        case CLEX_typedef: printf("warning: ignored token: typedef\n"); break;
        default: printf("unhandled & warning: ignored token: %s\n", lexer->string);  break;
    }

    if (func) state->type |= cminus_func;
}

void cminus_handle_token(cminus_state* state) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->token) {
        case CLEX_id: 
            if (state->type & cminus_func) {
                memcpy(state->sym[state->sym_count], lexer->string, 255);
                state->sym_count++;
                break;
            }

            /* !(state->type & cminus_var) is checked to ensure this changes before "=" */
            if (!(state->type & cminus_var) && !(state->type & cminus_set))
                memcpy(state->sym[0], lexer->string, 255);
            break;
        case CLEX_keyword:
            cminus_handle_keyword(state, state->type & cminus_func);
            break;
        case CLEX_eq: printf("warning: ignored token: ==\n"); break;
        case CLEX_noteq: printf("warning: ignored token: !=\n"); break;
        case CLEX_lesseq: printf("warning: ignored token: <=\n"); break;
        case CLEX_greatereq: printf("warning: ignored token: >=\n"); break;
        case CLEX_andand: printf("warning: ignored token: &&\n"); break;
        case CLEX_oror: printf("warning: ignored token: ||\n"); break;
        case CLEX_shl: printf("warning: ignored token: <<\n"); break;
        case CLEX_shr: printf("warning: ignored token: >>\n"); break;
        case CLEX_plusplus: printf("warning: ignored token: ++\n"); break;
        case CLEX_minusminus: printf("warning: ignored token: --\n"); break;
        case CLEX_arrow: printf("warning: ignored token: ->\n"); break;
        case CLEX_andeq: printf("warning: ignored token: &=\n"); break;
        case CLEX_oreq: printf("warning: ignored token: |=\n"); break;
        case CLEX_xoreq: printf("warning: ignored token: ^=\n"); break;
        case CLEX_pluseq: printf("warning: ignored token: +=\n"); break;
        case CLEX_minuseq: printf("warning: ignored token: -=\n"); break;
        case CLEX_muleq: printf("warning: ignored token: *=\n"); break;
        case CLEX_diveq: printf("warning: ignored token: /=\n"); break;
        case CLEX_modeq: printf("warning: ignored token: %%=\n"); break;
        case CLEX_shleq: printf("warning: ignored token: <<=\n"); break;
        case CLEX_shreq: printf("warning: ignored token: >>=\n"); break;
        case CLEX_eqarrow: printf("warning: ignored token: =>\n"); break;
        case CLEX_dqstring: printf("warning: ignored token: \"%s\"\n", lexer->string); break;
        case CLEX_sqstring: printf("warning: ignored token: '\"%s\"'\n", lexer->string); break;
        case CLEX_charlit:
        case CLEX_intlit:
        case CLEX_floatlit:
            if (state->type & cminus_func) {
                size_t size = (lexer->where_lastchar - lexer->where_firstchar) + 1;
                memcpy(state->sym[state->sym_count], lexer->where_firstchar, size);
                state->sym_count++;
                break;
            }
            if (state->type == CLEX_floatlit)
                printf("warning: ignored token: %f\n", lexer->real_number);
            break;
        case '=':
            if ((state->type & cminus_declare))
                state->type |= cminus_var;
            else 
                state->type |= cminus_set;
            break;
        case '(': 
            if (state->prev.token == CLEX_id) {
                state->type |= cminus_func;
                state->sym_count++;
            }
            break;
        case ')': 
            if (state->type & cminus_func && !(state->type & cminus_define) &&  !(state->type & cminus_declare)) {
                if (state->sym_count > 1) {
                    cminus_write_line(state, "");
                    cminus_write_line(state, "; load args for call");
                }

                for (size_t i = state->sym_count - 1; i > 0; i--) {
                    if ((state->sym[i][0] >= '0' && state->sym[i][0] <= '9') || state->sym[i][0] == '\'')
                        cminus_write_line(state, "mov eax, %s", state->sym[i]);
                    else {
                        cminus_sym* sym = cminus_find_sym(state->sym[i], state->scope);   
                        if (sym->scope)
                            cminus_write_line(state, "mov eax, [esp + %li]", sym->index* 4);
                        else
                            cminus_write_line(state, "mov eax, [%s]", sym->sym);
                    }
                    
                    switch(i - 1) {
                        case 0: break; /* eax == eax*/
                        case 1: cminus_write_line(state, "mov edx, eax"); break;
                        default:
                            cminus_write_line(state, "push eax");
                            break;
                    }
                }

                cminus_write_line(state, "call %s", state->sym[0]);
                for (size_t i = 3; i < state->sym_count; i++)
                    cminus_write_line(state, "pop eax");
                state->sym_count = 0;
            }
            break;
        case '{':
            if ((state->type & cminus_declare)) {
                state->type |= cminus_func;
                cminus_write_line(state, "%s:", (char*)state->sym);
                state->scope++;
                cminus_write_line(state, "; load stack frame");
                cminus_write_line(state, "push ebp");
                cminus_write_line(state, "mov ebp, esp");
            } else state->scope++;

            state->type = 0;
            if (state->sym_count > 1) {
                cminus_write_line(state, "");
                cminus_write_line(state, "; load args into this stack frame");
            }

            for (size_t i = 1; i < state->sym_count; i++) {
                cminus_push_sym(state->sym[i], state->stack_len[state->scope], state->scope);
                state->stack_len[state->scope]++;
                switch(i) {
                    case 0: cminus_write_line(state, "push eax");  break;
                    case 1: cminus_write_line(state, "push edx"); break;
                    default: /* load the rest from the stack */
                        cminus_write_line(state, "mov eax, [ebp - %li]", (i - 1) * 4);
                        cminus_write_line(state, "push eax");
                        break;
                }
            }
            cminus_write_line(state, "");

            state->sym_count = 0;
            break;
        case '}':
            cminus_write_line(state, "");
            cminus_write_line(state, "; clear stack frame");
            size_t stackLength = state->stack_len[state->scope];
            for (size_t i = 0; i < stackLength; i++) {
                cminus_write_line(state, "pop eax");
                cminus_pop_sym(state->scope);
            }

            state->stack_len[state->scope] = 0;
            
            cminus_write_line(state, "");
            cminus_write_line(state, "; reset stack frame");
            cminus_write_line(state, "pop ebp");
            cminus_write_line(state, "ret");
            state->scope--;
            if ((state->type & cminus_define) && (state->type & cminus_func))
                state->type = 0;
            break;
        case ';': 
            if ((state->type & cminus_declare) && !(state->type & cminus_func)) {    
                int8_t val = cminus_load_rvalue(state, "eax");

                if (state->scope)
                    cminus_write_line(state, "push eax");
                else
                    cminus_write_line(state, "%s: dd %i", state->sym[0], val);
                
                cminus_push_sym(state->sym[0], state->stack_len[state->scope], state->scope);
                state->stack_len[state->scope]++;
            }  else if ((state->type & cminus_set)) {
                if (state->scope == 0) {
                    fprintf(stderr, "error: syntax error\n");
                    exit(1);
                }

                cminus_load_rvalue(state, "eax");
                printf("%s\n", state->sym[0]);
                cminus_sym* sym = cminus_find_sym(state->sym[0], state->scope);

                if (sym->scope)
                    cminus_write_line(state, "mov [esp + %li], eax", sym->index* 4);
                else
                    cminus_write_line(state, "mov [%s], eax", sym->sym);
            }

            state->type = 0; 
            break;
        case CLEX_eof: break;
        default:
            if (!(lexer->token >= 0 && lexer->token < 256))
                printf("<<<UNKNOWN TOKEN %ld >>>\n", lexer->token);
            break;
    }
}
#endif