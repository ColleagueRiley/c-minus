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
    FILE* asmFile;
    size_t asmIndex;
    size_t scope;
    size_t stackLenght[MAX_STACK];

    char sym[MAX_ARGS][MAX_SYM_NAME]; /* name of the current sym */
    size_t symCount;

    stb_lexer prev;
    stb_lexer next;
} cminus_state;

inline void cminus_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asmFile);
inline void cminus_handle_token(cminus_state* state);
inline void cminus_handle_keyword(cminus_state* state, bool func);
inline void cminus_write_line(cminus_state* state, const char* format, ...);

typedef struct cminus_sym {
    char sym[MAX_SYM_NAME];
    size_t scope, index;
} cminus_sym;

inline void cminus_pushSymbol(char* name, size_t index, size_t scope);
inline cminus_sym* cminus_findSymbol(char* sym, size_t scope);
inline void cminus_popSymbol(size_t scope);

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
    size_t max = state->asmIndex + (state->scope * 4);

    for (state->asmIndex; state->asmIndex < max; state->asmIndex++) {
        fprintf(state->asmFile, " ");
    }

    va_list args;
    va_start(args, format);
    vfprintf(state->asmFile, format, args);
    va_end(args);

    fprintf(state->asmFile, "\n");
}

void cminus_pushSymbol(char* name, size_t index, size_t scope) {            
    cminus_syms[scope][cminus_sym_count[scope]].index = index;
    cminus_syms[scope][cminus_sym_count[scope]].scope = scope;
    strncpy(cminus_syms[scope][cminus_sym_count[scope]].sym, name, 255);

    cminus_sym_count[scope]++;
}

cminus_sym* cminus_findSymbol(char* sym, size_t scope) {
    for (size_t i = 0; i < cminus_sym_count[scope]; i++)
        if (strcmp(sym, cminus_syms[scope][i].sym) == 0)
            return &cminus_syms[scope][i];
    
    if (scope)
        return cminus_findSymbol(sym, 0);
    else { 
        fprintf(stderr, "Error: symbol not found: %s\n", sym);
        exit(1);
    }
}

void cminus_popSymbol(size_t scope) {
    if (scope == 0) return;
    cminus_sym_count[scope]--;
}


int32_t cminus_load_rValue(cminus_state* state, char* reg) {
    int32_t val = 0;
    switch (state->prev.token) {
        case CLEX_intlit:
            val = state->prev.int_number;
            if (state->scope)  cminus_write_line(state, "mov %s, %i", reg, val);
            break;
        case CLEX_id: {
            if (strcmp(state->sym[0], state->prev.string) == 0)
                break;
            
            cminus_sym* sym = cminus_findSymbol(state->prev.string, state->scope);
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

void cminus_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asmFile) {
    cminus_state state = {0};
    state.asmFile = asmFile;
    cminus_write_line(&state, "jmp _start");
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
    cminus_write_line(&state, "mov eax, 1");
    cminus_write_line(&state, "int  0x80");
    state.scope = 0;
}

void cminus_handle_keyword(cminus_state* state, bool func) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->keyword) {
        case CLEX_do: printf("do"); break;
        case CLEX_long: state->type = cminus_declare; break;
        case CLEX_const: state->type = cminus_declare;  break;
        case CLEX_signed: state->type = cminus_declare; break;
        case CLEX_static: state->type = cminus_declare | cminus_static; break;
        case CLEX_unsigned: state->type = cminus_declare; break;
        case CLEX_extern: state->type = cminus_declare; break;
        case CLEX_char: state->type = cminus_declare; break;
        case CLEX_double: state->type = cminus_declare; break;
        case CLEX_float: state->type = cminus_declare; break;
        case CLEX_short:  state->type = cminus_declare; break;
        case CLEX_int:  state->type = cminus_declare; break;
        case CLEX_void:  state->type = cminus_declare; break;
        case CLEX_case: printf("case"); break;
        case CLEX_default: printf("default"); break;
        case CLEX_break:  printf("break"); break;
        case CLEX_continue: printf("contiune"); break;
        case CLEX_return: printf("return"); break;
        case CLEX_for:  printf("for"); break;
        case CLEX_while: printf("while"); break;
        case CLEX_goto: printf("goto"); break;
        case CLEX_if:  printf("if"); break;
        case CLEX_else: printf("else"); break;
        case CLEX_sizeof: printf("sizeof"); break;
        case CLEX_switch: printf("switch"); break;
        case CLEX_struct: printf("struct"); break;
        case CLEX_enum: printf("enum"); break;
        case CLEX_union: printf("union"); break;
        case CLEX_typedef: printf("typedef"); break;
        default:
            printf("%s", lexer->string); 
            break;
    }

    if (func) state->type |= cminus_func;
}

void cminus_handle_token(cminus_state* state) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->token) {
        case CLEX_id: 
            if (state->type & cminus_func) {
                memcpy(state->sym[state->symCount], lexer->string, 255);
                state->symCount++;
                break;
            }

            /* !(state->type & cminus_var) is checked to ensure this changes before "=" */
            if (!(state->type & cminus_var) && !(state->type & cminus_set))
                memcpy(state->sym[0], lexer->string, 255);
            break;
        case CLEX_keyword:
            cminus_handle_keyword(state, state->type & cminus_func);
            break;
        case CLEX_eq: printf("=="); break;
        case CLEX_noteq: printf("!="); break;
        case CLEX_lesseq: printf("<="); break;
        case CLEX_greatereq: printf(">="); break;
        case CLEX_andand: printf("&&"); break;
        case CLEX_oror: printf("||"); break;
        case CLEX_shl: printf("<<"); break;
        case CLEX_shr: printf(">>"); break;
        case CLEX_plusplus: printf("++"); break;
        case CLEX_minusminus: printf("--"); break;
        case CLEX_arrow: printf("->"); break;
        case CLEX_andeq: printf("&="); break;
        case CLEX_oreq: printf("|="); break;
        case CLEX_xoreq: printf("^="); break;
        case CLEX_pluseq: printf("+="); break;
        case CLEX_minuseq: printf("-="); break;
        case CLEX_muleq: printf("*="); break;
        case CLEX_diveq: printf("/="); break;
        case CLEX_modeq: printf("%%="); break;
        case CLEX_shleq: break;
        case CLEX_shreq: break;
        case CLEX_eqarrow: break;
        case CLEX_dqstring: break;
        case CLEX_sqstring: break;
        case CLEX_charlit: break;
        #if defined(STB__clex_int_as_double) && !defined(STB__CLEX_use_stdlib)
        case CLEX_intlit: printf("#%g", lexer->real_number); break;
        #else
        case CLEX_intlit: break;
        #endif
        case CLEX_floatlit: break;
        case '=':
            if ((state->type & cminus_declare))
                state->type |= cminus_var;
            else 
                state->type |= cminus_set;
            break;
        case '(': 
            if (state->prev.token == CLEX_id) {
                state->type |= cminus_func;
                state->symCount++;
            }
            break;
        case ')': 
            if (state->type & cminus_func && !(state->type & cminus_define) &&  !(state->type & cminus_declare)) {
                for (size_t i =  state->symCount - 1; i > 1; i--) {
                    cminus_sym* sym = cminus_findSymbol(state->sym[i], state->scope);
                    if (sym->scope)
                        cminus_write_line(state, "mov [esp + %li], eax", sym->index* 4);
                    else
                        cminus_write_line(state, "mov [%s], eax", sym->sym);

                    switch(i) {
                        case 0: break;
                        case 1: cminus_write_line(state, "mov edx, eax"); break;
                        default: /* load the rest from the stack */
                            cminus_write_line(state, "mov eax, "); break;
                            cminus_write_line(state, "push eax");
                            break;
                    }
                }

                cminus_write_line(state, "call %s", state->sym[0]);

                for (size_t i = 2; i < state->symCount; i++)
                    cminus_write_line(state, "pop eax");
                state->symCount = 0;
            }
            break;
        case '{':
            if ((state->type & cminus_declare)) {
                state->type |= cminus_func;
                cminus_write_line(state, "%s:", (char*)state->sym);
                state->scope++;
                cminus_write_line(state, "push ebp");
                cminus_write_line(state, "mov ebp, esp");
            } else state->scope++;

            state->type = 0;
            
            /* ecx, edx, stack */
            for (size_t i = 0; i < state->symCount; i++) {
                cminus_pushSymbol(state->sym[i], state->stackLenght[state->scope], state->scope);
                state->stackLenght[state->scope]++;

                switch(i) {
                    case 0: cminus_write_line(state, "push eax");  break;
                    case 1: cminus_write_line(state, "push edx"); break;
                    default: /* load the rest from the stack */
                        cminus_write_line(state, "mov eax, [ebp + %li]", i * 4);
                        cminus_write_line(state, "push eax");
                        break;
                }
            }

            state->symCount = 0;
            break;
        case '}':
            size_t stackLength = state->stackLenght[state->scope];
            for (size_t i = 0; i < stackLength; i++) {
                cminus_write_line(state, "pop eax");
                cminus_popSymbol(state->scope);
            }

            state->stackLenght[state->scope] = 0;

            cminus_write_line(state, "pop ebp");
            cminus_write_line(state, "ret");
            state->scope--;
            if ((state->type & cminus_define) && (state->type & cminus_func))
                state->type = 0;
            break;
        case ';': 
            if ((state->type & cminus_declare) && !(state->type & cminus_func)) {    
                int8_t val = cminus_load_rValue(state, "eax");

                if (state->scope)
                    cminus_write_line(state, "push eax");
                else
                    cminus_write_line(state, "%s: dd %i", state->sym[0], val);
                
                cminus_pushSymbol(state->sym[0], state->stackLenght[state->scope], state->scope);
                state->stackLenght[state->scope]++;
            }  else if ((state->type & cminus_set)) {
                if (state->scope == 0) {
                    fprintf(stderr, "error: syntax error\n");
                    exit(1);
                }

                cminus_load_rValue(state, "eax");
                printf("%s\n", state->sym[0]);
                cminus_sym* sym = cminus_findSymbol(state->sym[0], state->scope);

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