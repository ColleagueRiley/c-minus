#define C_PARSER_IMPLEMENTATION
#include <c_parser.h>

typedef C_ENUM(uint32_t, programArgs) {
    c_asmOnly = C_BIT(0),
};

char string_buffer[0x10000];

int main(int argc, char **argv) {
    programArgs args = 0;
    if (argc < 1) {
        fprintf(stderr, "%s: fatal error: no input files\n", argv[0]);  
        return 1;  
    }

    for (size_t index = 1; index < argc; index++) {
        if (argv[index][0] == '-') {
            if (strcmp(argv[index], "-S") == 0)
                args |= c_asmOnly;
            continue;
        }

        FILE* file = fopen(argv[index], "rb");
        fseek(file, 0L, SEEK_END);
        size_t size = ftell(file);
        char* text = (char*)malloc(size);
        fseek(file, 0L, SEEK_SET);

        if (fread(text, 1, size, file) == 0) {
            fprintf(stderr, "Error opening file: %s\n", argv[index]);
            free(text);
            fclose(file);
            return 1;
        }

        fclose(file);

        FILE* output = fopen("out.asm", "w+");
        c_parse(text, size, string_buffer, sizeof(string_buffer), output);
        fclose(output);
        
        if (args & c_asmOnly)
            continue;
        
        argv[index][strlen(argv[index]) - 1] = 'o';
        sprintf(string_buffer, "nasm -f elf out.asm -o %s", argv[index]);
        system(string_buffer);
        printf("%s\n", string_buffer);
    }
    
    if (args & c_asmOnly)
        return 0;

    memcpy(string_buffer, "ld -m elf_i386 ", 16);
    size_t i = 15;
    for (size_t index = 1; index < argc; index++) {
        size_t size = strlen(argv[index]);
        strcpy(&string_buffer[i], argv[index]);
        printf("%s\n", argv[index]);
        i += size;
    }
    
    #ifdef _WIN32
    memcpy(string_buffer + i, " -o a.exe", 9);
    #else
    memcpy(string_buffer + i, " -o a.out", 9);
    #endif
    i += 9;

    string_buffer[i] = '\0';
    #ifdef _WIN32
    system(string_buffer);
    #else
    system(string_buffer);
    #endif
    return 0;
}