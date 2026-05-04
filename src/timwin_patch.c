#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timwin_hook_blob.h"

#define IMAGE_BASE 0x00400000u
#define DLL_IMAGE_BASE 0x00400000u
#define DLL_TEXT_RAW 0x400u
#define DLL_TEXT_RVA 0x1000u
#define ORIG_REPAINT_VA 0x00458364u
#define MAX_SECTIONS 32
#define MAX_PATH_BUF 4096

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    char name[9];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_size;
    uint32_t raw_pointer;
} Section;

typedef struct {
    uint32_t pe;
    uint32_t nsects_off;
    uint16_t nsects;
    uint32_t opt;
    uint32_t sect_table;
    uint32_t sect_align;
    uint32_t file_align;
    Section sections[MAX_SECTIONS];
} PeInfo;

typedef struct {
    uint32_t base_va;
    uint32_t raw;
    uint32_t used;
    Buffer *file;
} Alloc;

typedef struct {
    uint32_t va;
    const char *hex;
} Check;

typedef struct {
    uint32_t va;
    uint32_t target;
    uint32_t length;
} JumpPatch;

typedef struct {
    uint32_t va;
    uint32_t target;
} CallPatch;

typedef struct {
    uint32_t va;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} RectPatch;

typedef struct {
    uint32_t va;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t rows;
} RectTablePatch;

typedef struct {
    uint32_t hook_va;
    const char *expected_hex;
    uint32_t cave_va;
    const unsigned char *cave_code;
    size_t cave_len;
} DllHookPatch;

typedef struct {
    uint32_t va;
    const char *expected_hex;
    const char *replacement_hex;
} DllBytePatch;

static void fatal(const char *message)
{
    fprintf(stderr, "error: %s\n", message);
    exit(1);
}

static void fatal_path(const char *message, const char *path)
{
    fprintf(stderr, "error: %s: %s\n", message, path);
    exit(1);
}

static uint16_t rd16(const Buffer *buf, size_t off)
{
    if (off + 2 > buf->size) {
        fatal("read past end of file");
    }
    return (uint16_t)(buf->data[off] | (buf->data[off + 1] << 8));
}

static uint32_t rd32(const Buffer *buf, size_t off)
{
    if (off + 4 > buf->size) {
        fatal("read past end of file");
    }
    return (uint32_t)buf->data[off]
        | ((uint32_t)buf->data[off + 1] << 8)
        | ((uint32_t)buf->data[off + 2] << 16)
        | ((uint32_t)buf->data[off + 3] << 24);
}

static void wr16(Buffer *buf, size_t off, uint16_t value)
{
    if (off + 2 > buf->size) {
        fatal("write past end of file");
    }
    buf->data[off] = (unsigned char)(value & 0xFF);
    buf->data[off + 1] = (unsigned char)((value >> 8) & 0xFF);
}

static void wr32(Buffer *buf, size_t off, uint32_t value)
{
    if (off + 4 > buf->size) {
        fatal("write past end of file");
    }
    buf->data[off] = (unsigned char)(value & 0xFF);
    buf->data[off + 1] = (unsigned char)((value >> 8) & 0xFF);
    buf->data[off + 2] = (unsigned char)((value >> 16) & 0xFF);
    buf->data[off + 3] = (unsigned char)((value >> 24) & 0xFF);
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void ensure_size(Buffer *buf, size_t size)
{
    if (size <= buf->capacity) {
        if (size > buf->size) {
            memset(buf->data + buf->size, 0, size - buf->size);
            buf->size = size;
        }
        return;
    }

    unsigned char *new_data = (unsigned char *)realloc(buf->data, size);
    if (!new_data) {
        fatal("out of memory");
    }
    buf->data = new_data;
    memset(buf->data + buf->size, 0, size - buf->size);
    buf->capacity = size;
    buf->size = size;
}

static void read_file(const char *path, Buffer *out)
{
    FILE *file = fopen(path, "rb");
    long size;
    size_t got;

    if (!file) {
        fatal_path("could not open", path);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fatal_path("could not seek", path);
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        fatal_path("could not measure", path);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fatal_path("could not rewind", path);
    }

    out->data = (unsigned char *)malloc((size_t)size ? (size_t)size : 1u);
    if (!out->data) {
        fclose(file);
        fatal("out of memory");
    }
    out->size = (size_t)size;
    out->capacity = (size_t)size ? (size_t)size : 1u;
    got = fread(out->data, 1, out->size, file);
    fclose(file);
    if (got != out->size) {
        fatal_path("could not read", path);
    }
}

static void write_file(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        fatal_path("could not write", path);
    }
    if (fwrite(data, 1, size, file) != size) {
        fclose(file);
        fatal_path("could not write all bytes to", path);
    }
    fclose(file);
}

static void free_buffer(Buffer *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static int file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

static void join_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    const char *sep = "";
    if (len && dir[len - 1] != '\\' && dir[len - 1] != '/') {
        sep = "\\";
    }
    if (snprintf(out, cap, "%s%s%s", dir, sep, name) >= (int)cap) {
        fatal("path is too long");
    }
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static size_t parse_hex(const char *hex, unsigned char *out, size_t cap)
{
    size_t count = 0;
    while (*hex) {
        int hi;
        int lo;
        while (*hex == ' ' || *hex == '\t' || *hex == '\r' || *hex == '\n') {
            ++hex;
        }
        if (!*hex) {
            break;
        }
        hi = hex_value(*hex++);
        lo = hex_value(*hex++);
        if (hi < 0 || lo < 0) {
            fatal("invalid hex byte in patch definition");
        }
        if (count >= cap) {
            fatal("hex patch definition is too large");
        }
        out[count++] = (unsigned char)((hi << 4) | lo);
    }
    return count;
}

static int rel32(uint32_t src, uint32_t dst)
{
    return (int)(dst - (src + 5u));
}

static int rel32_len(uint32_t src, uint32_t length, uint32_t dst)
{
    return (int)(dst - (src + length));
}

static void put32(unsigned char *out, uint32_t value)
{
    out[0] = (unsigned char)(value & 0xFF);
    out[1] = (unsigned char)((value >> 8) & 0xFF);
    out[2] = (unsigned char)((value >> 16) & 0xFF);
    out[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void make_jmp(unsigned char out[5], uint32_t src, uint32_t dst)
{
    out[0] = 0xE9;
    put32(out + 1, (uint32_t)rel32(src, dst));
}

static void parse_pe(const Buffer *buf, PeInfo *pe)
{
    uint32_t off;
    uint32_t opt_size;
    uint16_t i;

    memset(pe, 0, sizeof(*pe));
    pe->pe = rd32(buf, 0x3C);
    pe->nsects_off = pe->pe + 6u;
    pe->nsects = rd16(buf, pe->nsects_off);
    if (pe->nsects > MAX_SECTIONS) {
        fatal("too many PE sections");
    }
    opt_size = rd16(buf, pe->pe + 20u);
    pe->opt = pe->pe + 24u;
    pe->sect_table = pe->opt + opt_size;
    pe->sect_align = rd32(buf, pe->opt + 32u);
    pe->file_align = rd32(buf, pe->opt + 36u);

    off = pe->sect_table;
    for (i = 0; i < pe->nsects; ++i) {
        Section *s = &pe->sections[i];
        size_t name_len;
        memcpy(s->name, buf->data + off, 8);
        s->name[8] = '\0';
        name_len = strcspn(s->name, "\0");
        s->name[name_len] = '\0';
        s->virtual_size = rd32(buf, off + 8u);
        s->virtual_address = rd32(buf, off + 12u);
        s->raw_size = rd32(buf, off + 16u);
        s->raw_pointer = rd32(buf, off + 20u);
        off += 40u;
    }
}

static int pe_has_section(const PeInfo *pe, const char *name)
{
    uint16_t i;
    for (i = 0; i < pe->nsects; ++i) {
        if (strcmp(pe->sections[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint32_t va_to_off(const PeInfo *pe, uint32_t va)
{
    uint32_t rva = va - IMAGE_BASE;
    uint16_t i;
    for (i = 0; i < pe->nsects; ++i) {
        const Section *s = &pe->sections[i];
        uint32_t span = s->virtual_size > s->raw_size ? s->virtual_size : s->raw_size;
        if (s->virtual_address <= rva && rva < s->virtual_address + span) {
            return s->raw_pointer + rva - s->virtual_address;
        }
    }
    fatal("VA outside executable sections");
    return 0;
}

static void expect_at(const Buffer *buf, uint32_t off, uint32_t va, const unsigned char *expected, size_t len)
{
    if (off + len > buf->size) {
        fatal("patch address is outside file");
    }
    if (memcmp(buf->data + off, expected, len) != 0) {
        fprintf(stderr, "error: unsupported or already-patched bytes at %08X\n", va);
        exit(1);
    }
}

static void expect_exe_hex(const Buffer *buf, const PeInfo *pe, uint32_t va, const char *hex)
{
    unsigned char expected[128];
    size_t len = parse_hex(hex, expected, sizeof(expected));
    expect_at(buf, va_to_off(pe, va), va, expected, len);
}

static void patch_jmp(Buffer *buf, const PeInfo *pe, uint32_t src, uint32_t dst, uint32_t length)
{
    unsigned char code[5];
    uint32_t off;
    if (length < 5) {
        fatal("jump patch length is too small");
    }
    make_jmp(code, src, dst);
    off = va_to_off(pe, src);
    memcpy(buf->data + off, code, 5);
    memset(buf->data + off + 5, 0x90, length - 5u);
}

static void patch_call(Buffer *buf, const PeInfo *pe, uint32_t call_va, uint32_t dst)
{
    uint32_t off = va_to_off(pe, call_va);
    if (buf->data[off] != 0xE8) {
        fprintf(stderr, "error: expected CALL at %08X\n", call_va);
        exit(1);
    }
    wr32(buf, off + 1u, (uint32_t)rel32(call_va, dst));
}

static void patch_bytes_raw(Buffer *buf, const PeInfo *pe, uint32_t va, const char *expected_hex,
    const unsigned char *replacement, size_t replacement_len, const char *name)
{
    unsigned char expected[128];
    size_t expected_len = parse_hex(expected_hex, expected, sizeof(expected));
    uint32_t off;
    if (replacement_len > expected_len) {
        fprintf(stderr, "error: %s patch is too large at %08X\n", name, va);
        exit(1);
    }
    off = va_to_off(pe, va);
    expect_at(buf, off, va, expected, expected_len);
    memcpy(buf->data + off, replacement, replacement_len);
    memset(buf->data + off + replacement_len, 0x90, expected_len - replacement_len);
}

static void patch_bytes_hex(Buffer *buf, const PeInfo *pe, uint32_t va, const char *expected_hex,
    const char *replacement_hex, const char *name)
{
    unsigned char replacement[128];
    size_t replacement_len = parse_hex(replacement_hex, replacement, sizeof(replacement));
    patch_bytes_raw(buf, pe, va, expected_hex, replacement, replacement_len, name);
}

static void write_dword(Buffer *buf, const PeInfo *pe, uint32_t va, uint32_t value)
{
    wr32(buf, va_to_off(pe, va), value);
}

static void write_rect(Buffer *buf, const PeInfo *pe, uint32_t va, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    write_dword(buf, pe, va, x);
    write_dword(buf, pe, va + 4u, y);
    write_dword(buf, pe, va + 8u, w);
    write_dword(buf, pe, va + 12u, h);
}

static void write_rect_table(Buffer *buf, const PeInfo *pe, uint32_t va, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rows)
{
    uint32_t row;
    for (row = 0; row < rows; ++row) {
        write_rect(buf, pe, va + (uint32_t)row * 16u, x, y, w, h);
    }
}

static void add_patch_section(Buffer *buf, uint32_t size, uint32_t *patch_va, uint32_t *patch_raw)
{
    PeInfo pe;
    const Section *last;
    uint32_t new_rva;
    uint32_t new_raw;
    uint32_t raw_size;
    uint32_t header_off;
    uint32_t first_raw = 0xFFFFFFFFu;
    uint16_t i;

    parse_pe(buf, &pe);
    if (pe_has_section(&pe, ".patch")) {
        fatal("TIMWIN.EXE already has a .patch section; use a clean source executable");
    }
    last = &pe.sections[pe.nsects - 1u];
    new_rva = align_up(last->virtual_address + (last->virtual_size > last->raw_size ? last->virtual_size : last->raw_size), pe.sect_align);
    new_raw = align_up((uint32_t)buf->size, pe.file_align);
    raw_size = align_up(size, pe.file_align);
    header_off = pe.sect_table + (uint32_t)pe.nsects * 40u;

    for (i = 0; i < pe.nsects; ++i) {
        if (pe.sections[i].raw_pointer && pe.sections[i].raw_pointer < first_raw) {
            first_raw = pe.sections[i].raw_pointer;
        }
    }
    if (header_off + 40u > first_raw) {
        fatal("no room for a new section header");
    }

    ensure_size(buf, new_raw + raw_size);
    memset(buf->data + header_off, 0, 40);
    memcpy(buf->data + header_off, ".patch", 6);
    wr32(buf, header_off + 8u, size);
    wr32(buf, header_off + 12u, new_rva);
    wr32(buf, header_off + 16u, raw_size);
    wr32(buf, header_off + 20u, new_raw);
    wr32(buf, header_off + 36u, 0x60000020u);
    wr16(buf, pe.nsects_off, (uint16_t)(pe.nsects + 1u));
    wr32(buf, pe.opt + 56u, align_up(new_rva + size, pe.sect_align));

    *patch_va = IMAGE_BASE + new_rva;
    *patch_raw = new_raw;
}

static uint32_t alloc_put(Alloc *alloc, const unsigned char *code, uint32_t len)
{
    uint32_t va = alloc->base_va + alloc->used;
    memcpy(alloc->file->data + alloc->raw + alloc->used, code, len);
    alloc->used += align_up(len, 0x10u);
    return va;
}

#define HOOK_VA(name) (TIMWIN_HOOK_BASE_VA + TIMWIN_HOOK_OFF_##name)
#define SOS_HOOK_VA(name) (SOS9502_HOOK_BASE_VA + SOS9502_HOOK_OFF_##name)
#define SOS_HOOK_PTR(name) (SOS9502_HOOK_BLOB + SOS9502_HOOK_OFF_##name)
#define SOS_HOOK_LEN(name) (SOS9502_HOOK_OFF_##name##_end - SOS9502_HOOK_OFF_##name)

static void patch_exe(Buffer *buf)
{
    static const Check checks[] = {
        {0x00415BB7u, "8B 43 08 3B 05"},
        {0x00415F57u, "0F B7 C6 A3 88"},
        {0x00415F93u, "83 3D 78 79 47 00 00"},
        {0x00415FBBu, "56 FF 75 10 53 57 E8 D1 C9 FF FF"},
        {0x00415441u, "8B C6 C1 E8 10 66 25 FF FF 0F B7 C0 2B 05 84 C4 47 00 50 0F B7 C6 50 E8 C9 F9 FF FF 83 C4 08"},
        {0x00417E04u, "8B C6 C1 E8 10 66 25 FF FF 0F B7 C0 2B 05 00 C5 47 00 50 0F B7 C6 50 E8 FD F0 FF FF 83 C4 08"},
        {0x00415622u, "56 57 53 FF 75 08 E8 6A D3 FF FF"},
        {0x00418117u, "56 57 53 FF 75 08 E8 75 A8 FF FF"},
        {0x00414DD3u, "E8 CD D1 FF FF"},
        {0x00415BFEu, "E8 A2 C3 FF FF"},
        {0x00416EC2u, "E8 DE B0 FF FF"},
        {0x00412A6Cu, "0F 84 F8 02 00 00"},
        {0x00412C46u, "FF 75 14 FF 75 10 50 57 E8 E5 EB 03 00 83 C4 10 33 DB"},
        {0x004126A7u, "FF 75 14 57 56 53 E8 F2 08 00 00 83 C4 10 85 C0"},
        {0x00412856u, "81 FF 30 F0 00 00 0F 85 21 01 00 00"},
        {0x004189EBu, "BA 22 00 00 00"},
        {0x00418A5Du, "E8 0A F5 03 00 59 A3 CC 0F 48 00"},
        {0x00418A63u, "A3 CC 0F 48 00 6A 14"},
        {0x00418A84u, "FF 35 CC 0F 48 00 E8 21 F7 03 00"},
        {0x004190A8u, "E8 80 94 01 00"},
        {0x004190B8u, "E8 9B C5 01 00"},
        {0x004190BEu, "E9 8D 00 00 00"},
        {0x00419180u, "E9 2F 06 00 00"},
        {0x00451A0Du, "89 3D 64 00 49 00 8B 45 F4 A3 68 00 49 00"},
        {0x00451A31u, "89 3D 5C 00 49 00 8B 45 F4 A3 60 00 49 00"},
        {0x0045198Eu, "8D 0C 38 85 C9 7C 22 8D 0C 38 3B 4B 18 7D 1A 8B 4D F4 03 CA 85 C9 7C 11 8B 4D F4 03 CA 3B 4B 1C 7D 07"},
        {0x0042ECC0u, "8B 03 8B 55 FC 89 50 34 8B 03 8B 55 F8 89 50 38"},
        {0x00443DD0u, "8B 52 6C 8B 48 18 8B 14 8A"},
        {0x00444F63u, "8B 52 6C 8B 12"},
        {0x004582B0u, "F6 43 38 40 74 45"},
        {0x0045842Fu, "F6 43 38 40 74 5D"},
        {0x00458384u, "55 8B EC 83 C4 88 53 56 57"},
        {0x00416F33u, "3B 05 0C C5 47 00 75 0C 3B 15 10 C5 47 00 0F 84 E7 01 00 00"},
    };
    static const RectPatch rects[] = {
        {0x00471B24u, 2320, 1608, 864, 308},
        {0x00471D0Cu, 16, 68, 2272, 1848},
        {0x00471F7Cu, 2320, 68, 864, 1326},
    };
    static const RectTablePatch rect_tables[] = {
        {0x00471B40u, 2320, 1608, 864, 308, 4},
        {0x00471B80u, 2320, 1608, 864, 308, 4},
        {0x00471D28u, 16, 68, 2272, 1848, 4},
        {0x00471D68u, 16, 68, 2272, 1848, 4},
        {0x00471F9Cu, 2320, 68, 864, 1326, 4},
        {0x00471FDCu, 2320, 68, 864, 1326, 4},
    };
    static const CallPatch call_patches[] = {
        {0x00414DD3u, HOOK_VA(timwin_child_create_layout_wrapper)},
        {0x00415BFEu, HOOK_VA(timwin_child_create_layout_wrapper)},
        {0x00416EC2u, HOOK_VA(timwin_child_create_layout_wrapper)},
    };
    static const JumpPatch jump_patches[] = {
        {0x00419180u, HOOK_VA(timwin_fullscreen_restore_size_hook), 5},
        {0x00412856u, HOOK_VA(timwin_parent_syscommand_restore_hook), 6},
        {0x004126A7u, HOOK_VA(timwin_parent_resize_layout_hook), 5},
        {0x00418A84u, HOOK_VA(timwin_toolbar_surface_native_layout_hook), 6},
        {0x00415BB7u, 0x00415BDDu, 5},
        {0x00415F57u, 0x00415F70u, 5},
        {0x00415F93u, 0x00415FAEu, 5},
        {0x00412C46u, HOOK_VA(timwin_enter_key_hook), 18},
        {0x00417E04u, HOOK_VA(timwin_part_buffer_resize_hook), 31},
        {0x00416FD8u, HOOK_VA(timwin_part_surface_stretch_hook), 11},
        {0x00416F33u, HOOK_VA(timwin_partbin_reopen_guard), 20},
        {0x00415FBBu, HOOK_VA(timwin_playfield_mouse_hook), 11},
        {0x00415622u, HOOK_VA(timwin_goal_mouse_hook), 11},
        {0x00418117u, HOOK_VA(timwin_part_mouse_hook), 11},
        {0x0045198Eu, HOOK_VA(timwin_playfield_region_client_bounds_hook), 0x22},
        {0x00451A31u, HOOK_VA(timwin_input_mouse_move_hook), 14},
        {0x00451A0Du, HOOK_VA(timwin_input_mouse_down_hook), 14},
        {ORIG_REPAINT_VA, HOOK_VA(timwin_toolbar_scaled_repaint_hook), 6},
        {0x00458384u, HOOK_VA(timwin_toolbar_partial_repaint_hook), 9},
        {0x0045842Fu, HOOK_VA(timwin_partbin_partial_stretch_hook), 6},
        {0x004582B0u, HOOK_VA(timwin_toolbar_wm_paint_scale_hook), 6},
        {0x00443DD0u, HOOK_VA(timwin_shape_frame_table_guard), 9},
        {0x00444F63u, HOOK_VA(timwin_single_shape_frame_guard), 5},
    };
    Buffer before = *buf;
    PeInfo pe;
    Alloc alloc;
    uint32_t patch_va;
    uint32_t patch_raw;
    unsigned char timhelp_replacement[10];
    unsigned char timhelp_jmp[5];
    size_t i;

    parse_pe(buf, &pe);
    if (pe_has_section(&pe, ".patch")) {
        fatal("TIMWIN.EXE already has a .patch section; use a clean source executable");
    }
    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
        expect_exe_hex(&before, &pe, checks[i].va, checks[i].hex);
    }

    add_patch_section(buf, 0x2000u, &patch_va, &patch_raw);
    parse_pe(buf, &pe);
    alloc.base_va = patch_va;
    alloc.raw = patch_raw;
    alloc.used = 0;
    alloc.file = buf;
    if (patch_va != TIMWIN_HOOK_BASE_VA) {
        fatal("generated hook blob does not match this executable's .patch address");
    }
    alloc_put(&alloc, TIMWIN_HOOK_BLOB, TIMWIN_HOOK_BLOB_SIZE);

    for (i = 0; i < sizeof(rects) / sizeof(rects[0]); ++i) {
        write_rect(buf, &pe, rects[i].va, rects[i].x, rects[i].y, rects[i].w, rects[i].h);
    }
    for (i = 0; i < sizeof(rect_tables) / sizeof(rect_tables[0]); ++i) {
        write_rect_table(buf, &pe, rect_tables[i].va, rect_tables[i].x, rect_tables[i].y, rect_tables[i].w, rect_tables[i].h, rect_tables[i].rows);
    }
    for (i = 0; i < sizeof(call_patches) / sizeof(call_patches[0]); ++i) {
        patch_call(buf, &pe, call_patches[i].va, call_patches[i].target);
    }
    for (i = 0; i < sizeof(jump_patches) / sizeof(jump_patches[0]); ++i) {
        patch_jmp(buf, &pe, jump_patches[i].va, jump_patches[i].target, jump_patches[i].length);
    }

    patch_bytes_hex(buf, &pe, 0x00415C52u, "83 3D C8 0F 48 00 00 74 4E", "85 C0 74 53 80 48 38 40", "playfield stretch surface flag");
    patch_bytes_hex(buf, &pe, 0x0042ECC0u, "8B 03 8B 55 FC 89 50 34 8B 03 8B 55 F8 89 50 38", "", "drag-release coordinate preserve");

    timhelp_replacement[0] = 0xBB;
    timhelp_replacement[1] = 0x01;
    timhelp_replacement[2] = 0x00;
    timhelp_replacement[3] = 0x00;
    timhelp_replacement[4] = 0x00;
    make_jmp(timhelp_jmp, 0x00416B25u, 0x00416B41u);
    memcpy(timhelp_replacement + 5, timhelp_jmp, 5);
    patch_bytes_raw(buf, &pe, 0x00416B20u,
        "50 57 6A 00 6A 00 6A 00 6A 00 6A 00 6A 00 8D 85 D0 FE FF FF 50 68 01 1F 47 00 E8 8C 05 05 00 8B D8",
        timhelp_replacement, sizeof(timhelp_replacement), "suppress TIMHELP.EXE launch");

    wr32(buf, va_to_off(&pe, 0x00412A6Cu) + 2u, (uint32_t)rel32_len(0x00412A6Cu, 6, HOOK_VA(timwin_enter_command_hook)));
    patch_bytes_hex(buf, &pe, 0x00416FF1u, "FF 35 0C C5 47 00", "FF 35 84 1F 47 00", "partbin header physical width");

    printf("TIMWIN.EXE: hook code at %08X, %u bytes\n", TIMWIN_HOOK_BASE_VA, TIMWIN_HOOK_BLOB_SIZE);
}

static uint32_t dll_va_to_off(uint32_t va)
{
    return DLL_TEXT_RAW + (va - DLL_IMAGE_BASE - DLL_TEXT_RVA);
}

static void dll_expect_hex(const Buffer *buf, uint32_t va, const char *hex)
{
    unsigned char expected[128];
    size_t len = parse_hex(hex, expected, sizeof(expected));
    uint32_t off = dll_va_to_off(va);
    if (off + len > buf->size) {
        fatal("DLL patch address is outside file");
    }
    if (memcmp(buf->data + off, expected, len) != 0) {
        if (buf->data[off] == 0xE9) {
            fprintf(stderr, "error: SOS9502.DLL appears already patched at %08X; use a clean source DLL\n", va);
        } else {
            fprintf(stderr, "error: unsupported SOS9502.DLL bytes at %08X\n", va);
        }
        exit(1);
    }
}

static void dll_patch_raw(Buffer *buf, uint32_t va, const unsigned char *data, size_t len)
{
    uint32_t off = dll_va_to_off(va);
    if (off + len > buf->size) {
        fatal("DLL patch write is outside file");
    }
    memcpy(buf->data + off, data, len);
}

static void dll_patch_bytes_hex(Buffer *buf, uint32_t va, const char *expected_hex, const char *replacement_hex)
{
    unsigned char expected[128];
    unsigned char replacement[128];
    size_t expected_len = parse_hex(expected_hex, expected, sizeof(expected));
    size_t replacement_len = parse_hex(replacement_hex, replacement, sizeof(replacement));
    uint32_t off = dll_va_to_off(va);
    if (replacement_len > expected_len) {
        fatal("DLL byte patch replacement is too large");
    }
    dll_expect_hex(buf, va, expected_hex);
    memcpy(buf->data + off, replacement, replacement_len);
    memset(buf->data + off + replacement_len, 0x90, expected_len - replacement_len);
}

static void dll_install_hook(Buffer *buf, uint32_t hook_va, const char *expected_hex,
    uint32_t cave_va, const unsigned char *cave_code, size_t cave_len)
{
    unsigned char expected[128];
    unsigned char patch[128];
    size_t expected_len = parse_hex(expected_hex, expected, sizeof(expected));
    if (expected_len < 5 || expected_len > sizeof(patch)) {
        fatal("invalid DLL hook overwrite size");
    }

    dll_expect_hex(buf, hook_va, expected_hex);
    make_jmp(patch, hook_va, cave_va);
    memset(patch + 5, 0x90, expected_len - 5u);
    dll_patch_raw(buf, hook_va, patch, expected_len);
    dll_patch_raw(buf, cave_va, cave_code, cave_len);
}

static void patch_sos9502(Buffer *buf)
{
    static const DllHookPatch hook_patches[] = {
        {
            0x004093D6u,
            "8B 45 F8 0F BF 40 20 F6 C4 20 0F 85 5D 00 00 00",
            SOS_HOOK_VA(sos9502_reloc_guard),
            SOS_HOOK_PTR(sos9502_reloc_guard),
            SOS_HOOK_LEN(sos9502_reloc_guard),
        },
        {
            0x0040AF3Cu,
            "8B 4D F8 66 8B 09 81 E1 FF FF 00 00 3B C1 0F 85 06 00 00 00 66 C7 45 F4 01 00",
            SOS_HOOK_VA(sos9502_midi_a00a),
            SOS_HOOK_PTR(sos9502_midi_a00a),
            SOS_HOOK_LEN(sos9502_midi_a00a),
        },
        {
            0x00409652u,
            "5F 5E 5B C9 C2 08 00",
            SOS_HOOK_VA(sos9502_midi_return),
            SOS_HOOK_PTR(sos9502_midi_return),
            SOS_HOOK_LEN(sos9502_midi_return),
        },
    };
    static const DllBytePatch byte_patches[] = {
        {0x004017A5u, "0F 84 3D 00 00 00", "90 90 90 90 90 90"},
        {0x004025F6u, "66 8B 80 DA 09 00 00 25 FF FF 00 00 50", "B8 FF FF FF FF 50 90 90 90 90 90 90 90"},
    };
    size_t i;

    for (i = 0; i < sizeof(hook_patches) / sizeof(hook_patches[0]); ++i) {
        dll_install_hook(
            buf,
            hook_patches[i].hook_va,
            hook_patches[i].expected_hex,
            hook_patches[i].cave_va,
            hook_patches[i].cave_code,
            hook_patches[i].cave_len
        );
    }
    for (i = 0; i < sizeof(byte_patches) / sizeof(byte_patches[0]); ++i) {
        dll_patch_bytes_hex(
            buf,
            byte_patches[i].va,
            byte_patches[i].expected_hex,
            byte_patches[i].replacement_hex
        );
    }
}

static void make_backup_name(char *out, size_t cap, const char *target)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    if (snprintf(out, cap, "%s.bak_pre_timwin_patch_%04d%02d%02d_%02d%02d%02d",
            target,
            tm_now.tm_year + 1900,
            tm_now.tm_mon + 1,
            tm_now.tm_mday,
            tm_now.tm_hour,
            tm_now.tm_min,
            tm_now.tm_sec) >= (int)cap) {
        fatal("backup path is too long");
    }
}

static void backup_file(const char *target)
{
    char backup[MAX_PATH_BUF];
    Buffer existing = {0};
    if (!file_exists(target)) {
        return;
    }
    make_backup_name(backup, sizeof(backup), target);
    read_file(target, &existing);
    write_file(backup, existing.data, existing.size);
    printf("backup %s\n", backup);
    free_buffer(&existing);
}

static void write_if_changed(const char *target, const Buffer *patched, int dry_run, int check_only)
{
    Buffer current = {0};
    if (file_exists(target)) {
        read_file(target, &current);
        if (current.size == patched->size && memcmp(current.data, patched->data, patched->size) == 0) {
            printf("already up to date %s\n", target);
            free_buffer(&current);
            return;
        }
        free_buffer(&current);
    }

    if (check_only || dry_run) {
        printf("would update %s\n", target);
        return;
    }

    backup_file(target);
    write_file(target, patched->data, patched->size);
    printf("updated %s\n", target);
}

static void usage(void)
{
    printf("usage: timwin-patch.exe [--game-dir DIR] [--source-dir DIR] [--check] [--dry-run]\n");
    printf("\n");
    printf("  --game-dir DIR    directory containing the TIMWIN.EXE/SOS9502.DLL to update\n");
    printf("  --source-dir DIR  directory containing clean original TIMWIN.EXE/SOS9502.DLL\n");
    printf("  --check           validate and report what would change\n");
    printf("  --dry-run         build patched files but do not write them\n");
}

int main(int argc, char **argv)
{
    const char *game_dir = "TIM3\\TIMWIN";
    const char *source_dir = NULL;
    int check_only = 0;
    int dry_run = 0;
    char source_exe[MAX_PATH_BUF];
    char source_dll[MAX_PATH_BUF];
    char target_exe[MAX_PATH_BUF];
    char target_dll[MAX_PATH_BUF];
    Buffer exe = {0};
    Buffer dll = {0};
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(argv[i], "--game-dir") == 0 && i + 1 < argc) {
            game_dir = argv[++i];
        } else if (strncmp(argv[i], "--game-dir=", 11) == 0) {
            game_dir = argv[i] + 11;
        } else if (strcmp(argv[i], "--source-dir") == 0 && i + 1 < argc) {
            source_dir = argv[++i];
        } else if (strncmp(argv[i], "--source-dir=", 13) == 0) {
            source_dir = argv[i] + 13;
        } else {
            usage();
            return 1;
        }
    }

    if (!source_dir) {
        source_dir = game_dir;
    }
    if (check_only) {
        dry_run = 1;
    }

    join_path(source_exe, sizeof(source_exe), source_dir, "TIMWIN.EXE");
    join_path(source_dll, sizeof(source_dll), source_dir, "SOS9502.DLL");
    join_path(target_exe, sizeof(target_exe), game_dir, "TIMWIN.EXE");
    join_path(target_dll, sizeof(target_dll), game_dir, "SOS9502.DLL");

    printf("source: %s\n", source_dir);
    printf("target: %s\n", game_dir);

    read_file(source_exe, &exe);
    patch_exe(&exe);
    write_if_changed(target_exe, &exe, dry_run, check_only);

    read_file(source_dll, &dll);
    patch_sos9502(&dll);
    write_if_changed(target_dll, &dll, dry_run, check_only);

    free_buffer(&exe);
    free_buffer(&dll);

    if (check_only) {
        printf("check complete: supported source files\n");
    } else if (dry_run) {
        printf("dry run complete\n");
    } else {
        printf("patch complete\n");
    }
    return 0;
}
