// Author: Paweł Anikiel 2021
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "elf.h"


// general utils

typedef struct Str Str;
struct Str {
	char *ptr;
	u32 size;
};

void error(char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

void append(Str *str, void *ptr, int size) {
	if (!size) return;
	char *re = realloc(str->ptr, str->size + size);
	if (!re) error("out of memory");
	memcpy(re + str->size, ptr, size);
	str->ptr = re;
	str->size += size;
}

int read_file(Str *str, char *name, int null_terminate) {
	FILE *fp;
	char *ptr;
	int size;

	null_terminate = !!null_terminate;
	fp = fopen(name, "rb");
	if (!fp) return 0;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	ptr = malloc(size + null_terminate);
	if (!ptr) error("out of memory");

	fseek(fp, 0, SEEK_SET);
	fread(ptr, 1, size, fp);
	if (null_terminate) ptr[size] = 0;
	str->ptr = ptr;
	str->size = size + null_terminate;
	fclose(fp);
	return 1;
}

int write_file(Str *str, char *name) {
	FILE *fp;

	fp = fopen(name, "wb");
	if (!fp) return 0;

	fwrite(str->ptr, 1, str->size, fp);
	fclose(fp);
	return 1;
}



// flist handling and name lookup

typedef struct Sig Sig;
struct Sig {
	int arg_cnt;
	int ret_type;
	int arg_type[6];
};

enum {
	TYPE_INVALID = -1,
	TYPE_VOID,
	TYPE_INT,
	TYPE_UINT,
	TYPE_LONG,
	TYPE_ULONG,
	TYPE_LONGLONG,
	TYPE_ULONGLONG,
	TYPE_PTR,
	TYPE_CNT,
};

char *type_name[] = {
	"void",
	"int",
	"uint",
	"long",
	"ulong",
	"longlong",
	"ulonglong",
	"ptr",
};

#define TYPE_ISLL(t) ((t) == TYPE_LONGLONG || (t) == TYPE_ULONGLONG)

typedef struct Fn Fn;
struct Fn {
	char *name;
	Sig sig;
};

#define MAX_FN_CNT 1023

Fn in_fns[MAX_FN_CNT + 1];

Sig *find_fn(char *name) {
	int i;
	for (i = 0; in_fns[i].name; i++) {
		if (strcmp(name, in_fns[i].name) == 0)
			return &in_fns[i].sig;
	}
	return 0;
}

int is_ws(int c) {
	return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

int is_nonws(int c) {
	return c && c != ' ' && c != '\n' && c != '\t' && c != '\r';
}

char *next_line(char *text, char **line) {
	while (is_ws(*text))
		text++;
	if (!*text) return 0;
	*line = text;
	text = strchr(text, '\n');
	if (text) *(text++) = 0;
	return text;
}

char *next_word(char *text, char **word) {
	while (is_ws(*text))
		text++;
	if (!*text) return 0;
	*word = text;
	while (is_nonws(*text))
		text++;
	if (*text) *(text++) = 0;
	return text;
}

char *next_type(char *text, int *type) {
	char *word;
	int i;
	if ((text = next_word(text, &word))) {
		*type = TYPE_INVALID;
		for (i = 0; i < TYPE_CNT; i++) {
			if (strcmp(word, type_name[i]) == 0)
				*type = i;
		}
	}
	return text;
}

void parse_line(char *line, Fn *fn) {
	char *word;
	int type;
	int arg_cnt = 0;

	line = next_word(line, &word);
	fn->name = word;

	if (!(line = next_type(line, &type)))
		error("flist: expected type");
	if (type == TYPE_INVALID)
		error("flist: invalid type");
	fn->sig.ret_type = type;

	while ((line = next_type(line, &type))) {
		if (type == TYPE_INVALID || type == TYPE_VOID)
			error("flist: invalid type");
		if (arg_cnt == 6)
			error("flist: too many args");
		fn->sig.arg_type[arg_cnt++] = type;
	}
	fn->sig.arg_cnt = arg_cnt;
}

void parse_flist_file(Str *str) {
	char *text, *line;
	int fn_cnt = 0;

	text = str->ptr;
	while ((text = next_line(text, &line))) {
		if (fn_cnt >= MAX_FN_CNT)
			error("flist: too many functions");
		parse_line(line, &in_fns[fn_cnt++]);
	}
}



// stub generating

/*
the stubs are written in a position-independent way, so there
is no need for relocations apart from the necessary ones.
*/

#define AX 0
#define CX 1
#define DX 2
#define BX 3
#define SP 4
#define BP 5
#define SI 6
#define DI 7

#define REX 0x40
#define W 8
#define R 4
#define X 2
#define B 1

#define MODRM(mod, reg, rm) (((mod) & 3) << 6 | ((reg) & 7) << 3 | ((rm) & 7))

/*
converts arguments between 32 and 64 bit calling
conventions, by movs between stack and registers.
0x89 - mov reg->mem
0x8b - mov mem->reg
0x63 - mov mem->reg sign extend
*/
void make_stub_conv_args(Str *str, Sig *sig, int offset, int mode) {
	u8 cc_reg[] = { DI, SI, DX, CX, 8, 9 };
	int i;

	for (i = 0; i < sig->arg_cnt; i++) {
		u8 mov[] = { REX, 0x89, MODRM(1, cc_reg[i], SP), 0x24, offset };
		int mov_start = 1, mov_size = 4;

		if (TYPE_ISLL(sig->arg_type[i]))
			{ mov[0] |= W; mov_start = 0; mov_size = 5; }
		if (cc_reg[i] & 8)
			{ mov[0] |= R; mov_start = 0; mov_size = 5; }

		if (mode) {
			if (sig->arg_type[i] == TYPE_LONG)
				{ mov[0] |= W; mov[1] = 0x63; mov_start = 0; mov_size = 5; }
			else
				mov[1] = 0x8b;
		}

		append(str, mov + mov_start, mov_size);
		offset += TYPE_ISLL(sig->arg_type[i]) ? 8 : 4;
	}
}

void make_stub_conv_args_to_32(Str *str, Sig *sig, int offset) {
	make_stub_conv_args(str, sig, offset, 0);
}

void make_stub_conv_args_to_64(Str *str, Sig *sig, int offset) {
	make_stub_conv_args(str, sig, offset, 1);
}

u8 stub_push_regs_32[] = {
	0x57,                   // push    edi
	0x56,                   // push    esi
};

u8 stub_push_regs_64[] = {
	0x53,                   // push    rbx
	0x55,                   // push    rbp
	0x41, 0x54,             // push    r12
	0x41, 0x55,             // push    r13
	0x41, 0x56,             // push    r14
	0x41, 0x57,             // push    r15
};

u8 stub_switch_to_32[] = {
	0x8d, 0x0d, 0x0e, 0x00, // lea     [rel <end of this block>]
	0x00, 0x00,
	0x89, 0x0c, 0x24,       // mov     [rsp], ecx
	0xc7, 0x44, 0x24, 0x04, // mov     dword [rsp+4], 0x23
	0x23, 0x00, 0x00, 0x00,
	0xff, 0x2c, 0x24,       // jmp far [rsp]
};

u8 stub_switch_to_64[] = {
	0xe8, 0x00, 0x00, 0x00, // call    <next instr>
	0x00,
	0x83, 0x04, 0x24, 0x0f, // add     dword [esp], <offset to end of this block>
	0xc7, 0x44, 0x24, 0x04, // mov     dword [esp+4], 0x33
	0x33, 0x00, 0x00, 0x00,
	0xff, 0x2c, 0x24,       // jmp far [esp]
};

u8 stub_pre_call_32[] = {
	0x83, 0xc4, 0x08,       // add     esp, 8
	0x6a, 0x2b,             // push    0x2b
	0x1f,                   // pop     ds
	0x6a, 0x2b,             // push    0x2b
	0x07,                   // pop     es
};

u8 stub_conv_ret_to_32[] = {
	0x48, 0x89, 0xc2,       // mov     rdx, rax
	0x48, 0xc1, 0xea, 0x20, // shr     rdx, 32
};

u8 stub_conv_ret_to_64[] = {
	0x48, 0xc1, 0xe2, 0x20, // shl     rdx, 32
	0x48, 0x09, 0xd0,       // or      rax, rdx
};

u8 stub_pop_regs_32[] = {
	0x5e,                   // pop     esi
	0x5f,                   // pop     edi
	0xc3,                   // ret
};

u8 stub_pop_regs_64[] = {
	0x41, 0x5f,             // pop     r15
	0x41, 0x5e,             // pop     r14
	0x41, 0x5d,             // pop     r13
	0x41, 0x5c,             // pop     r12
	0x5d,                   // pop     rbp
	0x5b,                   // pop     rbx
	0xc3,                   // ret
};

void make_stub_global(Str *str, Sig *sig, int *rel_pos) {
	int args_size = 0;
	int i;

	for (i = 0; i < sig->arg_cnt; i++)
		args_size += TYPE_ISLL(sig->arg_type[i]) ? 8 : 4;
	args_size += (8 - args_size) & 0xf;

	append(str, stub_push_regs_64, sizeof(stub_push_regs_64));
	{
		u8 instr[] = { 0x83, 0xec, args_size + 8 };    // sub     esp, ...
		append(str, instr, sizeof(instr));
	}
	make_stub_conv_args_to_32(str, sig, 8);
	append(str, stub_switch_to_32, sizeof(stub_switch_to_32));
	append(str, stub_pre_call_32, sizeof(stub_pre_call_32));
	{
		u8 instr[] = { 0xe8, 0x00, 0x00, 0x00, 0x00 }; // call    ??
		*rel_pos = str->size + 1;
		append(str, instr, sizeof(instr));
	}
	if (sig->ret_type != TYPE_VOID) {
		u8 instr[] = { 0x89, 0xc1 };                   // mov     ecx, eax
		append(str, instr, sizeof(instr));
	}
	append(str, stub_switch_to_64, sizeof(stub_switch_to_64));
	if (sig->ret_type != TYPE_VOID) {
		u8 instr[] = { 0x89, 0xc8 };                   // mov     eax, ecx
		append(str, instr, sizeof(instr));
	}
	if (TYPE_ISLL(sig->ret_type)) {
		append(str, stub_conv_ret_to_64, sizeof(stub_conv_ret_to_64));
	}
	else if (sig->ret_type == TYPE_LONG) {
		u8 instr[] = { 0x48, 0x63, 0xc0 };             // movsxd  rax, eax
		append(str, instr, sizeof(instr));
	}
	{
		u8 instr[] = { 0x83, 0xc4, args_size + 4 };    // add     esp, ...
		append(str, instr, sizeof(instr));
	}

	append(str, stub_pop_regs_64, sizeof(stub_pop_regs_64));
}

void make_stub_extern(Str *str, Sig *sig, int *rel_pos) {
	append(str, stub_push_regs_32, sizeof(stub_push_regs_32));
	{
		u8 instr[] = { 0x83, 0xec, 0x04 };             // sub     esp, 4
		append(str, instr, sizeof(instr));
	}
	append(str, stub_switch_to_64, sizeof(stub_switch_to_64));
	{
		u8 instr[] = { 0x83, 0xc4, 0x04 };             // add     esp, 4
		append(str, instr, sizeof(instr));
	}
	make_stub_conv_args_to_64(str, sig, 16);
	{
		u8 instr[] = { 0xe8, 0x00, 0x00, 0x00, 0x00 }; // call    ??
		*rel_pos = str->size + 1;
		append(str, instr, sizeof(instr));
	}
	if (TYPE_ISLL(sig->ret_type))
		append(str, stub_conv_ret_to_32, sizeof(stub_conv_ret_to_32));
	{
		u8 instr[] = { 0x83, 0xec, 0x04 };             // sub     esp, 4
		append(str, instr, sizeof(instr));
	}
	append(str, stub_switch_to_32, sizeof(stub_switch_to_32));
	{
		u8 instr[] = { 0x83, 0xc4, 0x08 };             // add     esp, 4
		append(str, instr, sizeof(instr));
	}
	append(str, stub_pop_regs_32, sizeof(stub_pop_regs_32));
}



// elf converting

/* 
these are the steps for converting the elf file:
	* remove all the SHT_NOTE sections
	* for every extern symbol present in the flist file,
		we generate a stub, make a local copy of the symbol,
		point it to the stub, and generate a relocation
		from the stub into the extern symbol.
	* for every global symbol which isn't extern and is
		present in flist, we generate a stub, point the
		global symbol to that stub, make a local copy of
		the symbol, generate a relocation from the stub to
		that local symbol, and point the local symbol to where
		the global symbol has pointed to before.
	* for every relocation to a global symbol we have
		generated a stub for, we repoint that relocation
		to the local version of that symbol.
	* other section headers are converted normally, and their
		sections are copied unaltered (even string tables).
this is difficult, because all the pointers in the
elf file change when we add and remove section headers,
symbols, and relocation entries. to combat this, we're
assuming that those pointers make no cycles, and just convert
everything in the right order, while keeping a bunch of
arrays that track where things have moved.
*/

Str in_file;
Str flist_file;
Str out_file;

Ehdr32 in_ehdr;
Ehdr64 out_ehdr;

// section headers go here
Str out_shdr_tbl;
// actual section data goes here
Str out_sections;

// indices of converted section headers
u16 *new_shdr_idx;
// indices of the local copies of symbols
u16 *copied_sym_idx;
u32 copied_sym_idx_cnt;
// indices of the converted symbols (just an offset)
u16 new_sym_idx_off;

#define SHN_ISREAL(idx) ((idx) && (idx) < SHN_LORESERVE)

void conv_sym_global(Sym32 *in_sym, int idx, Sig *sig, Str *stubs,
Sym64 *out_sym, Sym64 *out_loc_sym, Rela64 *out_rela) {
	int stub_offset;
	int rela_offset;
	
	stub_offset = stubs->size;
	make_stub_global(stubs, sig, &rela_offset);

	out_loc_sym->name_idx = in_sym->name_idx;
	out_loc_sym->info = ST_INFO(STB_LOCAL, STT_FUNC);
	out_loc_sym->other = 0;
	out_loc_sym->shdr_idx = new_shdr_idx[in_sym->shdr_idx];
	out_loc_sym->val = in_sym->val;
	out_loc_sym->size = in_sym->size;

	out_rela->offset = rela_offset;
	out_rela->info = R64_INFO(copied_sym_idx[idx], R_X86_64_PC32);
	out_rela->addend = -4;

	out_sym->name_idx = in_sym->name_idx;
	out_sym->info = ST_INFO(STB_GLOBAL, STT_FUNC);
	out_sym->other = 0;
	out_sym->shdr_idx = out_shdr_tbl.size / sizeof(Shdr64);
	out_sym->val = stub_offset;
	out_sym->size = stubs->size - stub_offset;
}

void conv_sym_extern(Sym32 *in_sym, int idx, Sig *sig, Str *stubs,
Sym64 *out_sym, Sym64 *out_loc_sym, Rela64 *out_rela) {
	int stub_offset;
	int rela_offset;

	stub_offset = stubs->size;
	make_stub_extern(stubs, sig, &rela_offset);

	out_loc_sym->name_idx = in_sym->name_idx;
	out_loc_sym->info = ST_INFO(STB_LOCAL, STT_FUNC);
	out_loc_sym->other = 0;
	out_loc_sym->shdr_idx = out_shdr_tbl.size / sizeof(Shdr64);
	out_loc_sym->val = stub_offset;
	out_loc_sym->size = stubs->size - stub_offset;

	out_rela->offset = rela_offset;
	out_rela->info = R64_INFO(idx + new_sym_idx_off, R_X86_64_PC32);
	out_rela->addend = -4;

	out_sym->name_idx = in_sym->name_idx;
	out_sym->info = ST_INFO(STB_GLOBAL, STT_FUNC);
	out_sym->other = 0;
	out_sym->shdr_idx = 0;
	out_sym->val = 0;
	out_sym->size = 0;
}

void conv_sym_other(Sym32 *in_sym, Sym64 *out_sym) {
	out_sym->name_idx = in_sym->name_idx;
	out_sym->info = in_sym->info;
	out_sym->other = 0;
	if (SHN_ISREAL(in_sym->shdr_idx))
		out_sym->shdr_idx = new_shdr_idx[in_sym->shdr_idx];
	else
		out_sym->shdr_idx = in_sym->shdr_idx;
	out_sym->val = in_sym->val;
	out_sym->size = in_sym->size;
}

void conv_symtab(Shdr32 *in_shdr, Shdr64 *out_shdr) {
	int i, cnt;
	char *in_shdr_tbl;
	char *in_sym_tbl;
	char *in_str_tbl;
	Str stubs = { 0 };
	Str sym_tbl = { 0 };
	Str loc_sym_tbl = { 0 };
	Str rela_tbl = { 0 };
	
	cnt = in_shdr->size / sizeof(Sym32);
	if (copied_sym_idx)
		error("multiple symbol tables");
	copied_sym_idx = calloc(cnt, sizeof(u16));
	if (!copied_sym_idx)
		error("out of memory");
	copied_sym_idx_cnt = cnt;

	in_shdr_tbl = in_file.ptr + in_ehdr.shdr_pos;
	in_sym_tbl = in_file.ptr + in_shdr->pos;
	{
		Shdr32 shdr;
		memcpy(&shdr, in_shdr_tbl + in_shdr->link * sizeof(shdr), sizeof(shdr));
		in_str_tbl = in_file.ptr + shdr.pos;
	}

	{
		Sym64 sym = { 0 };
		append(&loc_sym_tbl, &sym, sizeof(sym));
	}
	new_sym_idx_off = 1;

	// first, count how many symbols we'll have to generate stubs for
	for (i = 0; i < cnt; i++) {
		Sym32 in_sym;
		char *name;
		memcpy(&in_sym, in_sym_tbl + i * sizeof(in_sym), sizeof(in_sym));
		name = in_str_tbl + in_sym.name_idx;
		if (find_fn(name)) {
			if (!in_sym.shdr_idx ||
			(in_sym.info == ST_INFO(STB_GLOBAL, STT_FUNC) &&
			SHN_ISREAL(in_sym.shdr_idx)))
				copied_sym_idx[i] = new_sym_idx_off++;
		}
	}

	for (i = 0; i < cnt; i++) {
		Sym32 in_sym;
		char *name;
		Sig *sig = 0;
		Sym64 out_sym;
		Sym64 out_loc_sym;
		Rela64 out_rela;

		memcpy(&in_sym, in_sym_tbl + i * sizeof(in_sym), sizeof(in_sym));
		name = in_str_tbl + in_sym.name_idx;
		sig = find_fn(name);

		if (in_sym.info == ST_INFO(STB_GLOBAL, STT_FUNC) &&
		SHN_ISREAL(in_sym.shdr_idx) && sig) {
			conv_sym_global(&in_sym, i, sig, &stubs, &out_sym, &out_loc_sym, &out_rela);
			append(&loc_sym_tbl, &out_loc_sym, sizeof(out_loc_sym));
			append(&rela_tbl, &out_rela, sizeof(out_rela));
		}
		else if (!in_sym.shdr_idx && sig) {
			conv_sym_extern(&in_sym, i, sig, &stubs, &out_sym, &out_loc_sym, &out_rela);
			append(&loc_sym_tbl, &out_loc_sym, sizeof(out_loc_sym));
			append(&rela_tbl, &out_rela, sizeof(out_rela));
		}
		else {
			conv_sym_other(&in_sym, &out_sym);
		}
		append(&sym_tbl, &out_sym, sizeof(out_sym));
	}

	out_shdr->name_idx = in_shdr->name_idx;
	out_shdr->type = SHT_SYMTAB;
	out_shdr->flags = in_shdr->flags;
	out_shdr->addr = 0;
	out_shdr->pos = sizeof(Ehdr64) + out_sections.size;
	out_shdr->size = loc_sym_tbl.size + sym_tbl.size;
	out_shdr->link = new_shdr_idx[in_shdr->link];
	out_shdr->info = in_shdr->info + new_sym_idx_off;
	out_shdr->align = 8;
	out_shdr->ent_size = sizeof(Sym64);

	append(&out_sections, loc_sym_tbl.ptr, loc_sym_tbl.size);
	append(&out_sections, sym_tbl.ptr, sym_tbl.size);
	
	{
		Shdr64 shdr;

		shdr.name_idx = 0;
		shdr.type = SHT_PROGBITS;
		shdr.flags = SHF_ALLOC | SHF_EXECINSTR;
		shdr.addr = 0;
		shdr.pos = sizeof(Ehdr64) + out_sections.size;
		shdr.size = stubs.size;
		shdr.link = 0;
		shdr.info = 0;
		shdr.align = 0;
		shdr.ent_size = 0;
		append(&out_shdr_tbl, &shdr, sizeof(shdr));
		append(&out_sections, stubs.ptr, stubs.size);

		shdr.name_idx = 0;
		shdr.type = SHT_RELA;
		shdr.flags = 0;
		shdr.addr = 0;
		shdr.pos = sizeof(Ehdr64) + out_sections.size;
		shdr.size = rela_tbl.size;
		shdr.link = out_shdr_tbl.size / sizeof(Shdr64) + 1;
		shdr.info = out_shdr_tbl.size / sizeof(Shdr64) - 1;
		shdr.align = 8;
		shdr.ent_size = sizeof(Rela64);
		append(&out_shdr_tbl, &shdr, sizeof(shdr));
		append(&out_sections, rela_tbl.ptr, rela_tbl.size);
	}
	
	free(stubs.ptr);
	free(sym_tbl.ptr);
	free(loc_sym_tbl.ptr);
	free(rela_tbl.ptr);
}

u64 r_info_to_64(u32 info) {
	u32 sym  = R32_SYM(info);
	u32 type = R32_TYPE(info);
	if (sym >= copied_sym_idx_cnt)
		error("index out of range");
	if (copied_sym_idx[sym])
		sym = copied_sym_idx[sym];
	else
		sym += new_sym_idx_off;
	switch (type) {
		case R_386_32:
			type = R_X86_64_32; break;
		case R_386_PC32:
		case R_386_PLT32:
			type = R_X86_64_PC32; break;
		default:
			error("unsupported relocation");
	}
	return R64_INFO(sym, type);
}

void conv_rel(Shdr32 *in_shdr, Shdr64 *out_shdr) {
	int i, cnt;
	char *rel_tbl;

	cnt = in_shdr->size / sizeof(Rel32);
	rel_tbl = in_file.ptr + in_shdr->pos;

	out_shdr->name_idx = in_shdr->name_idx;
	out_shdr->type = SHT_RELA;
	out_shdr->flags = in_shdr->flags;
	out_shdr->addr = 0;
	out_shdr->pos = sizeof(Ehdr64) + out_sections.size;
	out_shdr->size = cnt * sizeof(Rela64);
	out_shdr->link = new_shdr_idx[in_shdr->link];
	out_shdr->info = new_shdr_idx[in_shdr->info];
	out_shdr->align = 8;
	out_shdr->ent_size = sizeof(Rela64);

	for (i = 0; i < cnt; i++) {
		Rel32 in_rel;
		Rela64 out_rela;
		memcpy(&in_rel, rel_tbl + i * sizeof(Rel32), sizeof(Rel32));
		out_rela.offset = in_rel.offset;
		out_rela.info = r_info_to_64(in_rel.info);
		out_rela.addend = 0;
		append(&out_sections, &out_rela, sizeof(Rela64));
	}
}

void conv_other(Shdr32 *in_shdr, Shdr64 *out_shdr) {
	out_shdr->name_idx = in_shdr->name_idx;
	out_shdr->type = in_shdr->type;
	out_shdr->flags = in_shdr->flags;
	out_shdr->addr = 0;
	out_shdr->pos = sizeof(Ehdr64) + out_sections.size;
	out_shdr->size = in_shdr->size;
	out_shdr->link = 0;
	out_shdr->info = in_shdr->info;
	out_shdr->align = in_shdr->align;
	out_shdr->ent_size = in_shdr->ent_size;
	append(&out_sections, in_file.ptr + in_shdr->pos, in_shdr->size);
}

void check_shdr_idx(u32 idx) {
	if (idx >= in_ehdr.shdr_cnt)
		error("index out of range");
}

void conv_shdr(int idx);
void conv_symtab_refs(Shdr32 *shdr) {
	int i;
	Sym32 sym;
	for (i = 0; i < shdr->size; i += sizeof(Sym32)) {
		memcpy(&sym, in_file.ptr + shdr->pos + i, sizeof(Sym32));
		if (!SHN_ISREAL(sym.shdr_idx))
			continue;
		check_shdr_idx(sym.shdr_idx);
		if (!new_shdr_idx[sym.shdr_idx])
			conv_shdr(sym.shdr_idx);
	}
}

void conv_shdr(int idx) {
	Shdr32 in_shdr;
	Shdr64 out_shdr;

	if (new_shdr_idx[idx]) return;
	memcpy(&in_shdr,
		in_file.ptr + in_ehdr.shdr_pos + idx * sizeof(in_shdr),
		sizeof(in_shdr));

	switch (in_shdr.type) {
		case 0:
			memset(&out_shdr, 0, sizeof(Shdr64));
			break;
		case SHT_SYMTAB:
			check_shdr_idx(in_shdr.link);
			if (in_shdr.link && !new_shdr_idx[in_shdr.link])
				conv_shdr(in_shdr.link);
			conv_symtab_refs(&in_shdr);
			conv_symtab(&in_shdr, &out_shdr);
			break;
		case SHT_NOTE:
			return;
		case SHT_REL:
			check_shdr_idx(in_shdr.link);
			check_shdr_idx(in_shdr.info);
			if (in_shdr.link && !new_shdr_idx[in_shdr.link])
				conv_shdr(in_shdr.link);
			if (in_shdr.info && !new_shdr_idx[in_shdr.info])
				conv_shdr(in_shdr.info);
			conv_rel(&in_shdr, &out_shdr);
			break;
		default:
			conv_other(&in_shdr, &out_shdr);
	}
	new_shdr_idx[idx] = out_shdr_tbl.size / sizeof(Shdr64);
	append(&out_shdr_tbl, &out_shdr, sizeof(Shdr64));
}

void conv_ehdr(void) {
	memcpy(out_ehdr.ident, ELFMAG, 4);
	out_ehdr.ident[EI_CLASS] = CLASS_64;
	out_ehdr.ident[EI_DATA] = DATA_LE;
	out_ehdr.ident[EI_VERSION] = 1;
	out_ehdr.type = ET_REL;
	out_ehdr.arch = EM_X86_64;
	out_ehdr.ver = 1;
	out_ehdr.entry = 0;
	out_ehdr.phdr_pos = 0;
	out_ehdr.shdr_pos = sizeof(Ehdr64) + out_sections.size;
	out_ehdr.flags = 0;
	out_ehdr.ehdr_size = sizeof(Ehdr64);
	out_ehdr.phdr_size = 0;
	out_ehdr.phdr_cnt = 0;
	out_ehdr.shdr_size = sizeof(Shdr64);
	out_ehdr.shdr_cnt = out_shdr_tbl.size / sizeof(Shdr64);
	out_ehdr.shdr_str_tbl_idx = new_shdr_idx[in_ehdr.shdr_str_tbl_idx];
}

int check_range(u32 pos, u32 ent_size, u32 cnt) {
	return pos < in_file.size && (u64) ent_size * cnt <= in_file.size - pos;
}

int copy_and_check_ehdr(void) {
	int i;

	memcpy(&in_ehdr, in_file.ptr, sizeof(in_ehdr));
	if (memcmp(in_ehdr.ident, ELFMAG, 4) != 0)
		return 0;
	if (in_ehdr.ident[EI_CLASS] != CLASS_32)
		return 0;
	if (in_ehdr.ident[EI_DATA] != DATA_LE)
		return 0;
	if (in_ehdr.type != ET_REL)
		return 0;
	if (in_ehdr.arch != EM_386)
		return 0;
	if (in_ehdr.shdr_str_tbl_idx >= in_ehdr.shdr_cnt)
		return 0;
	if (!check_range(in_ehdr.shdr_pos, sizeof(Shdr32), in_ehdr.shdr_cnt))
		return 0;
	for (i = 0; i < in_ehdr.shdr_cnt; i++) {
		Shdr32 shdr;
		memcpy(&shdr, in_file.ptr + in_ehdr.shdr_pos + i * sizeof(shdr), sizeof(shdr));
		if (!check_range(shdr.pos, shdr.size, 1))
			return 0;
	}
	return 1;
}



int main(int argc, char **argv) {
	int i;

	if (argc != 4)
		error("usage: %s <in ET_REL> <flist> <out ET_REL>", argv[0]);

	if (!read_file(&in_file, argv[1], 0))
		error("%s: can't open", argv[1]);
	if (!copy_and_check_ehdr())
		error("%s: bad file", argv[1]);
	
	if (!read_file(&flist_file, argv[2], 1))
		error("%s: can't open", argv[2]);
	parse_flist_file(&flist_file);

	new_shdr_idx = calloc(in_ehdr.shdr_cnt, sizeof(u16));
	if (!new_shdr_idx)
		error("out of memory");
	for (i = 0; i < in_ehdr.shdr_cnt; i++)
		conv_shdr(i);
	conv_ehdr();

	append(&out_file, &out_ehdr, sizeof(out_ehdr));
	append(&out_file, out_sections.ptr, out_sections.size);
	append(&out_file, out_shdr_tbl.ptr, out_shdr_tbl.size);

	write_file(&out_file, argv[3]);

	free(in_file.ptr);
	free(flist_file.ptr);
	free(out_file.ptr);

	free(out_sections.ptr);
	free(out_shdr_tbl.ptr);

	free(new_shdr_idx);
	free(copied_sym_idx);

	return 0;
}
