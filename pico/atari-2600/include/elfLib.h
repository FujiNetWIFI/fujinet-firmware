#ifndef ELFLIB_H
#define ELFLIB_H

#include <stdint.h>

#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_INIT_ARRAY 0xe
#define SHT_PREINIT_ARRAY 0x10

#define SHF_WRITE					0x001
#define SHF_ALLOC					0x002
#define SHF_EXECINSTR			0x004
#define SHF_MERGE					0x010
#define SHF_STRINGS				0x020
#define SHF_INFO_LINK			0x040
#define SHF_LINK_ORDER			0x080
#define SHF_OS_NONCONFORMING	0x100
#define SHF_GROUP					0x200
#define SHF_TLS					0x400

#define R_ARM_ABS32			0x02
#define R_ARM_THM_CALL		0x0a
#define R_ARM_THM_JUMP24	0x1e
#define R_ARM_TARGET1		0x26

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_LOPROC  5
#define STT_HIPROC  6
#define STB_LOCAL   7
#define STB_GLOBAL  8
#define STB_WEAK    9
#define STB_LOPROC  ??
#define STB_HIPROC  ??

#pragma pack(1)
typedef struct ElfHeader {
   uint8_t	e_ident[16];
   uint16_t	e_type;
   uint16_t	e_machine;
   uint32_t	e_version;
   uint32_t	e_entry;
   uint32_t	e_phoff;
   uint32_t	e_shoff;
   uint32_t	e_flags;
   uint16_t	e_ehsize;
   uint16_t	e_phentsize;
   uint16_t	e_phnum;
   uint16_t	e_shentsize;
   uint16_t	e_shnum;
   uint16_t	e_shstrndx;
} ElfHeader;

#pragma pack(1)
typedef struct SectionHeader {
   uint32_t sh_name;
   uint32_t sh_type;
   uint32_t sh_flags;
   uint32_t sh_addr;
   uint32_t sh_offset;
   uint32_t sh_size;
   uint32_t sh_link;
   uint32_t sh_info;
   uint32_t sh_addralign;
   uint32_t sh_entsize;
} SectionHeader;

#pragma pack(1)
typedef struct RelEntry {
   uint32_t offset;
   uint32_t info;
} RelEntry;

#pragma pack(1)
typedef struct SymbolEntry {
   uint32_t st_name; //index to string table
   uint32_t st_value;
   uint32_t st_size;
   unsigned char st_info;
   unsigned char st_other; // unused, 0
   uint16_t st_shndx;
} SymbolEntry;

typedef struct SectionMetaEntry {
   SectionHeader* header;
   uint32_t loadAddress;
   char* name;
   SectionHeader* relocation;
   uint8_t* buffer;
} SectionMetaEntry;

typedef struct NameAddressMapEntry {
   uint32_t address;
   char* name;
} NameAddressMapEntry;

extern NameAddressMapEntry NameAddressMap[43];

int isElf(uint32_t size, uint8_t* buffer);

int initSectionsMeta(uint8_t* elfBuffer, SectionMetaEntry meta[], uint32_t nobitsLoadAddress);

int loadElf(uint8_t* elfBuffer, uint32_t metaCount, SectionMetaEntry meta[], uint32_t* pMainAddress, int* usesVcsWrite3);

void runPreInitFuncs(uint32_t metaCount, SectionMetaEntry meta[]);
void runInitFuncs(uint32_t metaCount, SectionMetaEntry meta[]);

int relocateSection(
   SectionMetaEntry* sectionMeta,
   SectionHeader* symTableSectionHeader,
   SectionHeader* pSymStringSection,
   uint8_t* elfBuffer,
   SectionMetaEntry meta[]
);

#endif // ELFLIB_H
