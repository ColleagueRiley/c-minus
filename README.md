# C-Minus
![alt text](https://github.com/ColleagueRiley/c-minus/blob/main/logo.png?raw=true)

An independent C-like language compiler experiment for i386.

The purpose of this program is to have a better idea of how programming languages work under the hood. This compiler is not a real C compiler. The compiler is not, and probably will never be, compliant with the C standard. For the most part, performance and flexibility has been sacrficied for quick solutions.

Currently this compiler targets linux, as it uses linux syscalls. 

# stb_c_lexer.h
C-Minus uses a modified version of `stb_c_lexer.h` for lexing C, this allows me to focus on parsing the C tokens directly to assembly. Modified aspects are labled.

# current restrictions
* Only 32bit variables are supported
* the compiler mostly uses the `eax` register. 
* missing functionality (see TODO)

* max number of nested stacks: 1024
* max symbol length: 255
* max number of symbols (per stack): 1024
* max number of function arguments: 20

# supported features
* declarations
```c
int func();
int a;
```

* definitions
```c
void func() {

}

int a = 5;
int b = 5;
a = 10;
b = 9
```

* scopes
```c
int a;
int b;
void func() {
    int a = b;
}
```