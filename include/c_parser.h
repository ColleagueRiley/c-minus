#include "stb_c_lexer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define C_ENUM(type, name) type name; enum
#define C_BIT(x) 1L << x

typedef C_ENUM(uint32_t, c_state_type) {
    c_no_state = 0,
    c_declare = C_BIT(0),
    c_define = C_BIT(1),
    c_opp = C_BIT(2),
    c_func = C_BIT(3),
    c_var = C_BIT(4),
    c_static = C_BIT(5),
    c_set = C_BIT(6),
};

#define MAX_STACK 1024
#define MAX_SYM_NAME 255
#define MAX_SYMS 1024
#define MAX_ARGS 20

typedef struct c_state {
    c_state_type type;
    stb_lexer *lexer;
    FILE* asmFile;
    size_t asmIndex;
    size_t scope;
    size_t stackLenght[MAX_STACK];

    char sym[MAX_ARGS][MAX_SYM_NAME]; /* name of the current sym */
    size_t symCount;

    stb_lexer prev;
    stb_lexer next;
} c_state;

inline void c_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asmFile);
inline void c_handle_token(c_state* state);
inline void c_handle_keyword(c_state* state, bool func);

#ifdef C_PARSER_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define STB_C_LEXER_IMPLEMENTATION
#include "stb_c_lexer.h"


typedef C_ENUM(uint8_t, c_var_state) {
    c_var_none = 0,
    c_var_stack,
    c_var_register,
    c_var_bss
};

/* only using 32 bit registers currently */
typedef C_ENUM(uint8_t, c_register) {
    c_reg_none = 0,
    c_eax, /* */
    c_ebx, /* */
    c_ecx, /* */
    c_edx, /* */
    c_esi, /* */
    c_edi, /* */
    c_esp, /* stack pointer */
    c_ebp /* base stack poitner */
};

typedef struct c_sym {
    char sym[MAX_SYM_NAME];
    c_var_state state;
    size_t scope, index;
} c_sym;

c_sym c_syms[MAX_STACK][MAX_SYMS];
size_t c_sym_count[MAX_STACK];

void c_pushSymbol(char* name, size_t index, size_t scope) {            
    c_syms[scope][c_sym_count[scope]].index = index;
    c_syms[scope][c_sym_count[scope]].scope = scope;
    strncpy(c_syms[scope][c_sym_count[scope]].sym, name, 255);
    if (scope)
        c_syms[scope][c_sym_count[scope]].state = c_var_stack;
    else
        c_syms[scope][c_sym_count[scope]].state = c_var_bss;

    c_sym_count[scope]++;
}

c_sym* c_findSymbol(char* sym, size_t scope) {
    for (size_t i = 0; i < c_sym_count[scope]; i++)
        if (strcmp(sym, c_syms[scope][i].sym) == 0)
            return &c_syms[scope][i];
    
    if (scope)
        return c_findSymbol(sym, 0);
    else { 
        fprintf(stderr, "Error: symbol not found: %s\n", sym);
        exit(1);
    }
}

void c_popSymbol(size_t scope) {
    if (scope == 0) return;
    c_sym_count[scope]--;
}

int32_t c_load_rValue(c_state* state, char* reg) {
    int32_t val = 0;
    switch (state->prev.token) {
        case CLEX_intlit:
            val = state->prev.int_number;
            if (state->scope)  fprintf(state->asmFile, "    mov %s, %i\n", reg, val);
            break;
        case CLEX_id: {
            if (strcmp(state->sym[0], state->prev.string) == 0)
                break;
            
            c_sym* sym = c_findSymbol(state->prev.string, state->scope);
            val =  0;
            if (state->scope && sym->scope) 
                fprintf(state->asmFile, "    mov %s, [esp + %lu]\n", reg, sym->index * 4);
            else if (state->scope) {
                fprintf(state->asmFile, "    mov %s, %s\n", reg, sym->sym);
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

void c_parse(char* file, size_t file_len, char* string_buffer, size_t string_len, FILE* asmFile) {
    c_state state = {0};
    state.asmFile = asmFile;
    fprintf(state.asmFile, "jmp _start\nsection .data\n");

    stb_lexer lex;
    stb_c_lexer_init(&lex, file, file + file_len, string_buffer, string_len);
    while (stb_c_lexer_get_token(&lex)) {
        if (lex.token == CLEX_parse_error) {
            fprintf(stderr, "stb_c_lexer.h: fatal parse error\n");
            break;
        }

        state.lexer = &lex;
        c_handle_token(&state);
        
        stb_lexer next = lex;
        stb_c_lexer_get_token(&next);
        state.next = next;
        state.prev = lex;
    }

    fprintf(state.asmFile, "\n_start:\n    call main\n    mov eax, 1\n    int  0x80\n");
}

void c_handle_keyword(c_state* state, bool func) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->keyword) {
        case CLEX_do: printf("do"); break;
        case CLEX_long: state->type = c_declare; break;
        case CLEX_const: state->type = c_declare;  break;
        case CLEX_signed: state->type = c_declare; break;
        case CLEX_static: state->type = c_declare | c_static; break;
        case CLEX_unsigned: state->type = c_declare; break;
        case CLEX_extern: state->type = c_declare; break;
        case CLEX_char: state->type = c_declare; break;
        case CLEX_double: state->type = c_declare; break;
        case CLEX_float: state->type = c_declare; break;
        case CLEX_short:  state->type = c_declare; break;
        case CLEX_int:  state->type = c_declare; break;
        case CLEX_void:  state->type = c_declare; break;
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

    if (func) state->type |= c_func;
}

void c_handle_token(c_state* state) {
    stb_lexer* lexer = state->lexer;
    switch (lexer->token) {
        case CLEX_id: 
            if (state->type & c_func) {
                memcpy(state->sym[state->symCount], lexer->string, 255);
                state->symCount++;
                break;
            }

            /* !(state->type & c_var) is checked to ensure this changes before "=" */
            if (!(state->type & c_var) && !(state->type & c_set))
                memcpy(state->sym[0], lexer->string, 255);
            break;
        case CLEX_keyword:
            c_handle_keyword(state, state->type & c_func);
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
            if ((state->type & c_declare))
                state->type |= c_var;
            else 
                state->type |= c_set;
            break;
        case '(': 
            if (state->prev.token == CLEX_id) {
                state->type |= c_func;
                state->symCount++;
            }
            break;
        case ')': 
            if (state->type & c_func && !(state->type & c_define) &&  !(state->type & c_declare)) {
                for (size_t i =  state->symCount - 1; i > 1; i--) {
                    c_sym* sym = c_findSymbol(state->sym[i], state->scope);
                    if (sym->scope)
                        fprintf(state->asmFile, "    mov [esp + %li], eax\n", sym->index* 4);
                    else
                        fprintf(state->asmFile, "    mov [%s], eax\n", sym->sym);

                    switch(i) {
                        case 0: break;
                        case 1: fprintf(state->asmFile, "    mov edx, eax\n"); break;
                        default: /* load the rest from the stack */
                            fprintf(state->asmFile, "    mov eax, \n"); break;
                            fprintf(state->asmFile, "    push eax\n", i * 4);
                            break;
                    }
                }

                fprintf(state->asmFile, "    call %s\n", state->sym[0]);

                for (size_t i = 2; i < state->symCount; i++)
                    fprintf(state->asmFile, "    pop eax\n", i * 4);
                state->symCount = 0;
            }
            break;
        case '{':
            if ((state->type & c_declare)) {
                state->type |= c_func;
                fprintf(state->asmFile, "%s:\n", state->sym);
                fprintf(state->asmFile, "    push ebp\n    mov ebp, esp\n");
            }
            state->scope++;
            state->type = 0;
            
            /* ecx, edx, stack */
            for (size_t i = 0; i < state->symCount; i++) {
                c_pushSymbol(state->sym[i], state->stackLenght[state->scope], state->scope);
                state->stackLenght[state->scope]++;

                switch(i) {
                    case 0: fprintf(state->asmFile, "    push eax\n");  break;
                    case 1: fprintf(state->asmFile, "    push edx\n"); break;
                    default: /* load the rest from the stack */
                        fprintf(state->asmFile, "    mov eax, [ebp + %li]\n    push eax\n", i * 4);
                        break;
                }
            }

            state->symCount = 0;
            break;
        case '}':
            size_t stackLength = state->stackLenght[state->scope];
            for (size_t i = 0; i < stackLength; i++) {
                fprintf(state->asmFile, "    pop eax\n");
                c_popSymbol(state->scope);
            }

            state->stackLenght[state->scope] = 0;

            fprintf(state->asmFile, "    pop ebp\n    ret\n");
            state->scope--;
            if ((state->type & c_define) && (state->type & c_func))
                state->type = 0;
            break;
        case ';': 
            if ((state->type & c_declare) && !(state->type & c_func)) {    
                int8_t val = c_load_rValue(state, "eax");
                
                size_t max = state->asmIndex + (state->scope * 4);
                for (state->asmIndex; state->asmIndex < max; state->asmIndex++) {
                    fprintf(state->asmFile, " ");
                }

                if (state->scope)
                    fprintf(state->asmFile, "push eax\n");
                else
                    fprintf(state->asmFile, "%s: dd %i\n", state->sym[0], val);
                
                c_pushSymbol(state->sym[0], state->stackLenght[state->scope], state->scope);
                state->stackLenght[state->scope]++;
            }  else if ((state->type & c_set)) {
                if (state->scope == 0) {
                    fprintf(stderr, "error: syntax error\n");
                    exit(1);
                }

                c_load_rValue(state, "eax");
                printf("%s\n", state->sym[0]);
                c_sym* sym = c_findSymbol(state->sym[0], state->scope);

                if (sym->scope)
                    fprintf(state->asmFile, "    mov [esp + %li], eax\n", sym->index* 4);
                else
                    fprintf(state->asmFile, "    mov [%s], eax\n", sym->sym);
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