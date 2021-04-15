CFLAGS = -Wall -g
all: test stub

conv: conv.c elf.h

# prevent make from deleting this file
dummy: shuf32.o 

%64.o: %32.o %.flist conv
	./conv $< $*.flist $@
%32.o: %.c
	gcc -m32 -O2 -fno-pic -fno-common -fno-stack-protector -c $< -o $@

test: test.c shuf64.o
	gcc $^ -O2 -mcmodel=small -no-pie -fno-stack-protector -o $@

stub: stub.c stub.o
	gcc -no-pie -o stub stub.c stub.o

stub.o: stub.s
	nasm -f elf64 stub.s

clean:
	rm -f *.o conv test stub
