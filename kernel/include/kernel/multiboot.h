#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stddef.h>
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

#define MULTIBOOT_INFO_MEMORY       (1u << 0)
#define MULTIBOOT_INFO_MEMORY_MAP   (1u << 6)
#define MULTIBOOT_INFO_FRAMEBUFFER  (1u << 12)

#define MULTIBOOT_MEMORY_AVAILABLE 1u

#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0u
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1u
#define MULTIBOOT_FRAMEBUFFER_TYPE_TEXT    2u

/*
 * Multiboot v1 information structure.
 *
 * The fixed fields through framebuffer_type have deterministic offsets.
 *
 * framebuffer_color_data deliberately remains raw.
 *
 * The Multiboot specification places RGB data directly at offset 110,
 * but the current GRUB/QEMU environment used by Meaty has been observed
 * to insert two alignment bytes and begin RGB information at offset 112.
 *
 * The framebuffer parser handles both forms.
 */
struct multiboot_info
{
    uint32_t flags;

    uint32_t mem_lower;
    uint32_t mem_upper;

    uint32_t boot_device;
    uint32_t cmdline;

    uint32_t mods_count;
    uint32_t mods_addr;

    uint32_t syms[4];

    /*
     * Existing spelling retained because the current PMM and memory-map
     * code already use this member.
     */
    uint32_t mmap_lengh;
    uint32_t mmap_addr;

    uint32_t drives_length;
    uint32_t drives_addr;

    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;

    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;

    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr;

    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;

    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;

    /*
     * Bytes beginning at Multiboot offset 110.
     *
     * Eight bytes are retained so the parser can accept:
     *
     * spec layout:
     *
     *   110 R-pos
     *   111 R-size
     *   112 G-pos
     *   113 G-size
     *   114 B-pos
     *   115 B-size
     *
     * aligned layout observed under the current GRUB/QEMU setup:
     *
     *   110 padding
     *   111 padding
     *   112 R-pos
     *   113 R-size
     *   114 G-pos
     *   115 G-size
     *   116 B-pos
     *   117 B-size
     */
    uint8_t framebuffer_color_data[8];

} __attribute__((packed));

struct multiboot_mmap_entry
{
    uint32_t size;
    uint64_t address;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));


_Static_assert(
    offsetof(
        struct multiboot_info,
        mmap_lengh) == 44,
    "Multiboot mmap offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_addr) == 88,
    "Multiboot framebuffer address offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_pitch) == 96,
    "Multiboot framebuffer pitch offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_width) == 100,
    "Multiboot framebuffer width offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_height) == 104,
    "Multiboot framebuffer height offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_bpp) == 108,
    "Multiboot framebuffer bpp offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_type) == 109,
    "Multiboot framebuffer type offset incorrect");

_Static_assert(
    offsetof(
        struct multiboot_info,
        framebuffer_color_data) == 110,
    "Multiboot framebuffer color-data offset incorrect");


void print_memory_map(
    const struct multiboot_info *mbi);

#endif