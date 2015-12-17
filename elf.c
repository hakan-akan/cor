#include "common.h"
#include "task.h"

#pragma pack(push, 1)
struct elf64_header {
  uint32_t magic;
  uint8_t class;
  uint8_t endian;
  uint8_t version1;
  uint8_t abi_major;
  uint8_t abi_minor;
  uint32_t pad1;
  uint16_t pad2;
  uint8_t pad3;
  uint16_t type;
  uint16_t arch;
  uint32_t version2;
  uint64_t entrypoint;
  uint64_t ph_offset;
  uint64_t sh_offset;
  uint32_t flags;
  uint16_t mysize;
  uint16_t ph_entsize;
  uint16_t ph_entnum;
  uint16_t sh_entsize;
  uint16_t sh_entnum;
  uint16_t sh_section_name_entry_idx;
};

struct elf64_sectionheader {
  uint32_t name;
  uint32_t type;
  uint64_t flags;
  uint64_t addr;
  uint64_t offset;
  uint64_t size;
  uint32_t link;
  uint32_t info;
  uint64_t addralign;
  uint64_t entsize;
};
#pragma pack(pop)

extern char cor_stage2_init_data;
extern int cor_stage2_init_data_len;

uint64_t cor_elf_load(char *elf, unsigned int len) {
  cor_printk("Called cor_elf_exec with elf=%x, len=%x\n",elf, len);
  if(sizeof(struct elf64_header) != 64) {
    cor_printk("assertion failed: elf64 header struct is size %x\n",sizeof(struct elf64_header));
    return 0;
  }

  if(len < sizeof(struct elf64_header)) {
    cor_printk("assertion failed: elf64 data too short at %x\n", len);
    return 0;
  }

  struct elf64_header *hdr = (struct elf64_header *)elf;

  if(0x464c457f != hdr->magic) {
    cor_printk("ERROR: magic of %x did not match ELF header of 0x464c457f\n", hdr->magic);
    return 0;
  } else {
    cor_printk("ELF magic looks OK.\n");
  }

  if(sizeof(struct elf64_sectionheader) != hdr->sh_entsize) {
    cor_printk("ERROR: ELF section header entry size invalid\n");
    return 0;
  }

  cor_printk("Nsections: %x\n", hdr->sh_entnum);

  if(hdr->sh_entnum > 30) {
    cor_printk("ELF: too many sections");
    return 0;
  }

  struct task_table_entry *t = task_new();

  for(int i = 0; i < hdr->sh_entnum; i++) {
    void *p = elf + hdr->sh_offset + hdr->sh_entsize * i;
    struct elf64_sectionheader *sectionheader = (struct elf64_sectionheader *) p;

    // TODO: is it allowed to have LMA=0 for non-debug-aux sections?
    if(sectionheader->addr == 0 || sectionheader->size == 0) {
      continue;
    }

    void *addr_space_start = (void*)sectionheader->addr;
    void *addr_space_end = (void*)sectionheader->addr + sectionheader->size;
    cor_printk("Section-used address space is: %p -- %p\n", addr_space_start, addr_space_end);

    void *lowpage = (void*)((uint64_t)addr_space_start & ~0xfff);
    void *highpage = (void*)ALIGN((uint64_t)addr_space_end, 0x1000);
    cor_printk("This means we need to make pages from %p -- %p\n", lowpage, highpage);

    for(void *page = lowpage; page < highpage; page += 0x1000) {
      cor_printk("Adding page at %p\n", page);

      // FIXME: we massively leak pages here by adding the same page multiple times
      task_addpage(t, page);
    }
  }



  task_enter_memspace(t);

  for(int i = 0; i < hdr->sh_entnum; i++) {
    void *p = elf + hdr->sh_offset + hdr->sh_entsize * i;
    struct elf64_sectionheader *sectionheader = (struct elf64_sectionheader *) p;

    // TODO: see above
    if(sectionheader->addr == 0 || sectionheader->size == 0) {
      continue;
    }

    if(sectionheader->type == 0x8 && sectionheader->flags == 0x3) {
      // this seems to be a/the data section
      void *brk = (void *)ALIGN(sectionheader->addr+sectionheader->size, 0x1000);
      t->brk = brk;
      cor_printk("found data section at %p+%p; setting brk to %p\n",
        sectionheader->addr, sectionheader->size, brk);
    }

    //struct task_section *s = task_add_section(t, 1, sectionheader->size);
    char *source = elf + sectionheader->offset;

    cor_printk("section %x loading from %p to %p (size %x)\n",
      i, source, sectionheader->addr, sectionheader->size);

    for(size_t j = 0; j < sectionheader->size; j++) {
      char *loadtarget = (char*)sectionheader->addr + j;
      char *loadsrc = source + j;
      *loadtarget = *loadsrc;
    }
  }

  if(t->brk == 0) {
    cor_panic("could not determine brk for init");
  }
  cor_printk("init's brk is at: %x\n", t->brk);

  // memory sanity check; this is the "push rbp" opcode that should (always?) be the entry point
  // not really a good marker, but meh
  if(0x4855 != *((uint16_t *)(hdr->entrypoint))) {
    cor_printk("Virtual memory sanity check failed %x\n", *((uint16_t *)(hdr->entrypoint)));
    return 0;
  }

  void (*entry)() = (void(*)(void *))(hdr->entrypoint);
  cor_printk("entry = %x\n", entry);

  cor_printk("done?!\n");
  return (uint64_t)entry;
}

uint64_t cor_load_init() {
  return cor_elf_load(&cor_stage2_init_data, cor_stage2_init_data_len);
}
