#include <stdio.h>
#include <sys/mman.h>

extern unsigned stub_call32(unsigned, unsigned);

int real_main(void) {
	printf("%u\n", stub_call32(100, 9)); // expecting 100 + 1 + 9 + 1
	return 0;
}

__asm__(
	"call_with_stack:\n"
	"pushq %rbp\n"
	"movq %rsp, %rbp\n"
	"movq %rdi, %rsp\n"
	"call real_main\n"
	"movq %rbp, %rsp\n"
	"popq %rbp\n"
	"ret\n"
);

int call_with_stack(void *ptr);

int main() {
	void *stack = mmap(0, 0x10000,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	return call_with_stack(stack + 0x10000);
}
