typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef signed   long long s64;

#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6

#define ELFMAG "\177ELF"

#define CLASS_32 1
#define CLASS_64 2
#define DATA_LE  1
#define DATA_BE  2

#define ET_REL 1

#define EM_386    3
#define EM_X86_64 62

#define SHN_LORESERVE 0xff00

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOTE     7
#define SHT_REL      9

#define SHF_WRITE (1 << 0)
#define SHF_ALLOC (1 << 1)
#define SHF_EXECINSTR (1 << 2)

#define ST_BIND(info) ((info) >> 4)
#define ST_TYPE(info) ((info) & 0xf)
#define ST_INFO(bind, type) ((bind) << 4 | ((type) & 0xf))

#define STB_LOCAL  0 
#define STB_GLOBAL 1

#define STT_FUNC    2
#define STT_SECTION 3

#define R32_SYM(info)  ((info) >> 8)
#define R32_TYPE(info) ((info) & 0xff)
#define R32_INFO(sym, type) (((u64) (sym)) << 8 | type)

#define R64_SYM(info)  ((info) >> 32)
#define R64_TYPE(info) ((info) & 0xffffffff)
#define R64_INFO(sym, type) (((u64) (sym)) << 32 | type)

#define R_386_32    1
#define R_386_PC32  2
#define R_386_PLT32 4

#define R_X86_64_PC32 2
#define R_X86_64_32   10

typedef struct Ehdr32 Ehdr32;
struct Ehdr32 {
	u8  ident[16];
	u16 type;
	u16 arch;
	u32 ver;
	u32 entry;
	u32 phdr_pos;
	u32 shdr_pos;
	u32 flags;
	u16 ehdr_size;
	u16 phdr_size;
	u16 phdr_cnt;
	u16 shdr_size;
	u16 shdr_cnt;
	u16 shdr_str_tbl_idx;
};

typedef struct Ehdr64 Ehdr64;
struct Ehdr64 {
	u8  ident[16];
	u16 type;
	u16 arch;
	u32 ver;
	u64 entry;
	u64 phdr_pos;
	u64 shdr_pos;
	u32 flags;
	u16 ehdr_size;
	u16 phdr_size;
	u16 phdr_cnt;
	u16 shdr_size;
	u16 shdr_cnt;
	u16 shdr_str_tbl_idx;
};

typedef struct Shdr32 Shdr32;
struct Shdr32 {
	u32 name_idx;
	u32 type;
	u32 flags;
	u32 addr;
	u32 pos;
	u32 size;
	u32 link;
	u32 info;
	u32 align;
	u32 ent_size;
};

typedef struct Shdr64 Shdr64;
struct Shdr64 {
	u32 name_idx;
	u32 type;
	u64 flags;
	u64 addr;
	u64 pos;
	u64 size;
	u32 link;
	u32 info;
	u64 align;
	u64 ent_size;
};

typedef struct Sym32 Sym32;
struct Sym32 {
	u32 name_idx;
	u32 val;
	u32 size;
	u8  info;
	u8  other;
	u16 shdr_idx;
};

typedef struct Sym64 Sym64;
struct Sym64 {
	u32 name_idx;
	u8  info;
	u8  other;
	u16 shdr_idx;
	u64 val;
	u64 size;
};

typedef struct Rel32 Rel32;
struct Rel32 {
	u32 offset;
	u32 info;
};

typedef struct Rela64 Rela64;
struct Rela64 {
	u64 offset;
	u64 info;
	s64 addend;
};

