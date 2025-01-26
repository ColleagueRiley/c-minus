CC = gcc
SOURCE = ./source/*.c
LIBS = -I./include

OUTPUT = cminus
EXAMPLE = test.c

$(OUTPUT): $(SOURCE) include/*.h
	$(CC) $^ $(LIBS) -o $@

all: $(OUTPUT)

debug: $(OUTPUT)
	./$(OUTPUT) $(EXAMPLE)
	make clean

debugAsm: $(OUTPUT)
	./$(OUTPUT) -S $(EXAMPLE)
	make debugAsm

debugAsm: $(OUTPUT)
	nasm -f elf out.asm -o out.o
	ld -m elf_i386 out.o -o out
	#make clean

clean:
	rm -f *.o *.exe $(OUTPUT)

wipe:
	rm -f *.o *.exe $(OUTPUT) *.asm out *.s