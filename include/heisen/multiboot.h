#ifndef _HEISEN_MULTI_BOOT_H
#define _HEISEN_MULTI_BOOT_H

typedef unsigned char multiboot_uint8_t;
typedef unsigned short multiboot_uint16_t;
typedef unsigned intintmultiboot_uint32_t;
typedef unsigned long long multiboot_uint64_t;
typedef unsigned multiboot_uint32_t;

#define MULTIBOOT_SEARCH 32768
#define MULTIBOOT_HEADER_ALIGN 8

/* The magic field should contain this.  */
#define MULTIBOOT2_HEADER_MAGIC 0xe85250d6

/* This should be in %eax.  */
#define MULTIBOOT_MAGIC 0x36d76289

/* Alignment of multiboot modules.  */
#define MULTIBOOT_MOD_ALIGN 0x00001000

/* Alignment of the multiboot info structure.  */
#define MULTIBOOT_INFO_ALIGN 0x00000008

/* Flags set in the 'flags' member of the multiboot header.  */

#define MULTIBOOT_TAG_ALIGN 8
#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_CMDLINE 1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_TYPE_MODULE 3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO 4
#define MULTIBOOT_TAG_TYPE_BOOTDEV 5
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_TAG_TYPE_VBE 7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS 9
#define MULTIBOOT_TAG_TYPE_APM 10
#define MULTIBOOT_TAG_TYPE_EFI32 11
#define MULTIBOOT_TAG_TYPE_EFI64 12
#define MULTIBOOT_TAG_TYPE_SMBIOS 13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD 14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW 15
#define MULTIBOOT_TAG_TYPE_NETWORK 16

#define MULTIBOOT_HEADER_TAG_END 0
#define MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST 1
#define MULTIBOOT_HEADER_TAG_ADDRESS 2
#define MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS 3
#define MULTIBOOT_HEADER_TAG_CONSOLE_FLAGS 4
#define MULTIBOOT_HEADER_TAG_FRAMEBUFFER 5
#define MULTIBOOT_HEADER_TAG_MODULE_ALIGN 6

#define MULTIBOOT_ARCHITECTURE_I386 0
#define MULTIBOOT_ARCHITECTURE_MIPS32 4
#define MULTIBOOT_HEADER_TAG_OPTIONAL 1

#define MULTIBOOT_CONSOLE_FLAGS_CONSOLE_REQUIRED 1
#define MULTIBOOT_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED 2


struct multiBootHeader {
    intintmultiboot_uint32_t magical;

    intintmultiboot_uint32_t arch;

    intintmultiboot_uint32_t headerLen;

    intintmultiboot_uint32_t checkSum;

};


struct multiBootHeaderTag{
    multiboot_uint16_t type;
    multiboot_uint16_t flag;
    multiboot_uint16_t size;
};

struct multiBootHeaderTagInfRequest{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint16_t size;
    multiboot_uint16_t requests[0];
};

struct multiBootHeaderTagAddress{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint32_t size;
    multiboot_uint32_t header_addr;
    multiboot_uint32_t load_addr;
    multiboot_uint32_t load_end_addr;
    multiboot_uint32_t bss_end_addr;
};

struct multiBootHeaderTagEntryAddress{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint16_t size;
    multiboot_uint16_t entry_address;
};

struct multiBootHeaderTagConsoleFlags{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint16_t size;
    multiboot_uint16_t console_flags;
};


struct multiBootHeaderTagFrameBuffer{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint16_t size;
    multiboot_uint16_t width;
    multiboot_uint16_t height;
    multiboot_uint16_t depth;
};

struct multiBootHeaderTagModuleAling{
    multiboot_uint16_t type;
    multiboot_uint16_t flags;
    multiboot_uint32_t size;
    multiboot_uint32_t width;
    multiboot_uint32_t height;
    multiboot_uint32_t depth;
};

struct multiboot_mmap_entry {
    multiboot_uint64_t addr;
    multiboot_uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5
    multiboot_uint32_t type;
    multiboot_uint32_t zero;
} __attribute__((packed));

typedef struct multiboot_mmap_entry multiboot_memory_map_t;

struct multiboot_tag {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
};

struct multiboot_tag_string {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    char string[0];
};

struct multiboot_tag_module {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t mod_start;
    multiboot_uint32_t mod_end;
    char cmdline[0];
};

struct multiboot_tag_basic_meminfo {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t mem_lower;
    multiboot_uint32_t mem_upper;
};

struct multiboot_tag_bootdev {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t biosdev;
    multiboot_uint32_t slice;
    multiboot_uint32_t part;
};

struct multiboot_tag_mmap {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t entry_size;
    multiboot_uint32_t entry_version;
    struct multiboot_mmap_entry entries[0];
};

struct multiboot_vbe_info_block {
    multiboot_uint8_t external_specification[512];
};

struct multiboot_vbe_mode_info_block {
    multiboot_uint8_t external_specification[256];
};

struct multiboot_tag_vbe {
    multiboot_uint32_t type;
    multiboot_uint32_t size;

    multiboot_uint16_t vbe_mode;
    multiboot_uint16_t vbe_interface_seg;
    multiboot_uint16_t vbe_interface_off;
    multiboot_uint16_t vbe_interface_len;

    struct multiboot_vbe_info_block vbe_control_info;
    struct multiboot_vbe_mode_info_block vbe_mode_info;
};

struct multiboot_tag_framebuffer_common {
    multiboot_uint32_t type;
    multiboot_uint32_t size;

    multiboot_uint64_t FramebufferAddress;
    multiboot_uint32_t FramebufferPitch;
    multiboot_uint32_t FramebufferWidth;
    multiboot_uint32_t FramebufferHeight;
    multiboot_uint8_t FramebufferBPP;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2
    multiboot_uint8_t framebuffer_type;
    multiboot_uint16_t reserved;
};

struct multiboot_tag_framebuffer {
    struct multiboot_tag_framebuffer_common common;

    union {
        struct {
            multiboot_uint16_t framebuffer_palette_num_colors;
            struct multiboot_color framebuffer_palette[0];
        };
        struct {
            multiboot_uint8_t FramebufferRedFieldPosition;
            multiboot_uint8_t FramebufferRedMaskSize;
            multiboot_uint8_t FramebufferGreenFieldPosition;
            multiboot_uint8_t FramebufferGreenMaskSize;
            multiboot_uint8_t FramebufferBlueFieldPosition;
            multiboot_uint8_t FramebufferBlueMaskSize;
        };
    };
};

struct multiboot_tag_elf_sections {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t num;
    multiboot_uint32_t entsize;
    multiboot_uint32_t shndx;
    char sections[0];
};

struct multiboot_tag_apm {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint16_t version;
    multiboot_uint16_t cseg;
    multiboot_uint32_t offset;
    multiboot_uint16_t cseg_16;
    multiboot_uint16_t dseg;
    multiboot_uint16_t flags;
    multiboot_uint16_t cseg_len;
    multiboot_uint16_t cseg_16_len;
    multiboot_uint16_t dseg_len;
};

struct multiboot_tag_efi32 {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint32_t pointer;
};

struct multiboot_tag_efi64 {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint64_t pointer;
};

struct multiboot_tag_smbios {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint8_t major;
    multiboot_uint8_t minor;
    multiboot_uint8_t reserved[6];
    multiboot_uint8_t tables[0];
};

struct multiboot_tag_old_acpi {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint8_t rsdp[0];
};

struct multiboot_tag_new_acpi {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint8_t rsdp[0];
};

struct multiboot_tag_network {
    multiboot_uint32_t type;
    multiboot_uint32_t size;
    multiboot_uint8_t dhcpack[0];
};


#endif // _HEISEN_MULTI_BOOT_H