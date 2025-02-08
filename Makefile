compiler.exe: main.c
	gcc main.c -o compiler.exe

.PHONY: clean targetrun targetasm targetresult

targetasm: compiler.exe test.txt
	./compiler.exe -S test.txt

targetresult: targetasm
	nasm -fwin64 out.asm
	ld out.obj -lkernel32 -luser32 -lshell32 -o result.exe
	rm -f out.obj

clean:
	rm -f compiler.exe result.exe out.asm

targetrun: targetresult
#	./compiler.exe
	./result.exe
