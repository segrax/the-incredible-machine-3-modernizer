#define _CRT_SECURE_NO_WARNINGS

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_REL_I386_DIR32 0x0006
#define IMAGE_REL_I386_REL32 0x0014
#define IMAGE_SCN_CNT_CODE 0x00000020u
#define IMAGE_SYM_ABSOLUTE (-1)
#define IMAGE_SYM_CLASS_EXTERNAL 2

#define MAX_OBJECTS 8

typedef struct {
    unsigned char *data;
    size_t size;
} Buffer;

typedef struct {
    char *name;
    uint32_t raw_size;
    uint32_t raw_ptr;
    uint32_t rel_ptr;
    uint16_t nrel;
    uint32_t chars;
    uint32_t out_off;
    int included;
} CoffSection;

typedef struct {
    char *name;
    uint32_t value;
    int16_t section;
    uint8_t storage;
    int is_aux;
} CoffSymbol;

typedef struct {
    const char *path;
    Buffer file;
    CoffSection *sections;
    uint16_t nsects;
    CoffSymbol *symbols;
    uint32_t nsymbols;
    const unsigned char *string_table;
    uint32_t string_table_size;
} CoffObject;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} ByteVec;

typedef struct {
    char *name;
    uint32_t value;
} NamedValue;

typedef struct {
    NamedValue *items;
    size_t count;
    size_t capacity;
} NamedValues;

typedef struct {
    CoffObject objects[MAX_OBJECTS];
    size_t count;
} ObjectSet;

typedef struct {
    const char *header_path;
    const char *timwin_objects[MAX_OBJECTS];
    size_t timwin_count;
    const char *sos9502_objects[MAX_OBJECTS];
    size_t sos9502_count;
    uint32_t timwin_base_va;
    uint32_t sos9502_base_va;
} Options;

static void fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char *xstrdup_range(const char *s, size_t len)
{
    char *out = (char *)malloc(len + 1u);
    if (!out) {
        fatal("out of memory");
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *xstrdup(const char *s)
{
    return xstrdup_range(s, strlen(s));
}

static uint16_t rd16(const unsigned char *data, size_t size, size_t off)
{
    if (off + 2u > size) {
        fatal("read past end of file");
    }
    return (uint16_t)(data[off] | ((uint16_t)data[off + 1u] << 8));
}

static uint32_t rd32(const unsigned char *data, size_t size, size_t off)
{
    if (off + 4u > size) {
        fatal("read past end of file");
    }
    return (uint32_t)data[off]
        | ((uint32_t)data[off + 1u] << 8)
        | ((uint32_t)data[off + 2u] << 16)
        | ((uint32_t)data[off + 3u] << 24);
}

static void wr32(unsigned char *data, size_t size, size_t off, uint32_t value)
{
    if (off + 4u > size) {
        fatal("write past end of hook blob");
    }
    data[off] = (unsigned char)(value & 0xffu);
    data[off + 1u] = (unsigned char)((value >> 8) & 0xffu);
    data[off + 2u] = (unsigned char)((value >> 16) & 0xffu);
    data[off + 3u] = (unsigned char)((value >> 24) & 0xffu);
}

static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void read_file(const char *path, Buffer *out)
{
    FILE *file = fopen(path, "rb");
    long size;
    size_t got;

    if (!file) {
        fatal("could not open %s", path);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fatal("could not seek %s", path);
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        fatal("could not measure %s", path);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fatal("could not rewind %s", path);
    }

    out->data = (unsigned char *)malloc((size_t)size ? (size_t)size : 1u);
    if (!out->data) {
        fclose(file);
        fatal("out of memory");
    }
    out->size = (size_t)size;
    got = fread(out->data, 1, out->size, file);
    fclose(file);
    if (got != out->size) {
        fatal("could not read %s", path);
    }
}

static char *coff_string_at(const CoffObject *obj, uint32_t off)
{
    const unsigned char *start;
    const unsigned char *end;

    if (off >= obj->string_table_size) {
        fatal("%s has a bad COFF string offset %u", obj->path, off);
    }

    start = obj->string_table + off;
    end = start;
    while ((uint32_t)(end - obj->string_table) < obj->string_table_size && *end) {
        ++end;
    }
    return xstrdup_range((const char *)start, (size_t)(end - start));
}

static char *coff_symbol_name(const CoffObject *obj, const unsigned char *raw)
{
    uint32_t first = rd32(raw, 8u, 0u);
    uint32_t second = rd32(raw, 8u, 4u);
    size_t len;

    if (first == 0) {
        return coff_string_at(obj, second);
    }

    len = 0;
    while (len < 8u && raw[len]) {
        ++len;
    }
    return xstrdup_range((const char *)raw, len);
}

static char *coff_section_name(const CoffObject *obj, const unsigned char *raw)
{
    char temp[9];
    size_t len;

    len = 0;
    while (len < 8u && raw[len]) {
        ++len;
    }
    memcpy(temp, raw, len);
    temp[len] = '\0';

    if (temp[0] == '/') {
        char *end = NULL;
        unsigned long off = strtoul(temp + 1, &end, 10);
        if (end && *end == '\0') {
            return coff_string_at(obj, (uint32_t)off);
        }
    }
    return xstrdup(temp);
}

static void parse_coff(const char *path, CoffObject *obj)
{
    uint16_t machine;
    uint32_t sym_ptr;
    uint32_t string_table_ptr;
    uint16_t opt_size;
    uint32_t i;
    size_t off;

    memset(obj, 0, sizeof(*obj));
    obj->path = path;
    read_file(path, &obj->file);

    if (obj->file.size < 20u) {
        fatal("%s is too small to be a COFF object", path);
    }

    machine = rd16(obj->file.data, obj->file.size, 0u);
    obj->nsects = rd16(obj->file.data, obj->file.size, 2u);
    sym_ptr = rd32(obj->file.data, obj->file.size, 8u);
    obj->nsymbols = rd32(obj->file.data, obj->file.size, 12u);
    opt_size = rd16(obj->file.data, obj->file.size, 16u);

    if (machine != IMAGE_FILE_MACHINE_I386) {
        fatal("%s is not an i386 COFF object", path);
    }
    if (opt_size != 0) {
        fatal("%s has an unexpected optional header", path);
    }

    string_table_ptr = sym_ptr + obj->nsymbols * 18u;
    obj->string_table_size = rd32(obj->file.data, obj->file.size, string_table_ptr);
    if ((size_t)string_table_ptr + obj->string_table_size > obj->file.size) {
        fatal("%s has a bad COFF string table", path);
    }
    obj->string_table = obj->file.data + string_table_ptr;

    obj->sections = (CoffSection *)calloc(obj->nsects + 1u, sizeof(CoffSection));
    if (!obj->sections) {
        fatal("out of memory");
    }

    off = 20u;
    for (i = 1u; i <= obj->nsects; ++i) {
        CoffSection *section = &obj->sections[i];
        const unsigned char *header;
        uint32_t raw_size;
        uint32_t raw_ptr;
        uint32_t rel_ptr;

        if (off + 40u > obj->file.size) {
            fatal("%s has a truncated section table", path);
        }
        header = obj->file.data + off;
        section->name = coff_section_name(obj, header);
        raw_size = rd32(header, 40u, 16u);
        raw_ptr = rd32(header, 40u, 20u);
        rel_ptr = rd32(header, 40u, 24u);
        section->nrel = rd16(header, 40u, 32u);
        section->chars = rd32(header, 40u, 36u);

        if (raw_size && (size_t)raw_ptr + raw_size > obj->file.size) {
            fatal("%s section %s has bad raw data bounds", path, section->name);
        }
        if (section->nrel && (size_t)rel_ptr + (size_t)section->nrel * 10u > obj->file.size) {
            fatal("%s section %s has bad relocation bounds", path, section->name);
        }

        section->raw_size = raw_size;
        section->raw_ptr = raw_ptr;
        section->rel_ptr = rel_ptr;
        off += 40u;
    }

    obj->symbols = (CoffSymbol *)calloc(obj->nsymbols ? obj->nsymbols : 1u, sizeof(CoffSymbol));
    if (!obj->symbols) {
        fatal("out of memory");
    }

    i = 0u;
    while (i < obj->nsymbols) {
        size_t entry_off = sym_ptr + i * 18u;
        CoffSymbol *sym;
        uint8_t aux_count;
        uint32_t aux;

        if (entry_off + 18u > obj->file.size) {
            fatal("%s has a truncated symbol table", path);
        }

        sym = &obj->symbols[i];
        sym->name = coff_symbol_name(obj, obj->file.data + entry_off);
        sym->value = rd32(obj->file.data, obj->file.size, entry_off + 8u);
        sym->section = (int16_t)rd16(obj->file.data, obj->file.size, entry_off + 12u);
        sym->storage = obj->file.data[entry_off + 16u];
        aux_count = obj->file.data[entry_off + 17u];

        for (aux = 1u; aux <= aux_count && i + aux < obj->nsymbols; ++aux) {
            obj->symbols[i + aux].is_aux = 1;
        }
        i += 1u + aux_count;
    }
}

static void bytevec_reserve(ByteVec *vec, size_t need)
{
    unsigned char *new_data;
    size_t new_capacity;

    if (need <= vec->capacity) {
        return;
    }

    new_capacity = vec->capacity ? vec->capacity : 256u;
    while (new_capacity < need) {
        new_capacity *= 2u;
    }
    new_data = (unsigned char *)realloc(vec->data, new_capacity);
    if (!new_data) {
        fatal("out of memory");
    }
    vec->data = new_data;
    vec->capacity = new_capacity;
}

static void bytevec_append(ByteVec *vec, const unsigned char *data, size_t len)
{
    bytevec_reserve(vec, vec->size + len);
    memcpy(vec->data + vec->size, data, len);
    vec->size += len;
}

static void bytevec_pad_to(ByteVec *vec, size_t size)
{
    bytevec_reserve(vec, size);
    while (vec->size < size) {
        vec->data[vec->size++] = 0;
    }
}

static void named_values_set(NamedValues *values, const char *name, uint32_t value)
{
    NamedValue *new_items;
    size_t i;

    for (i = 0; i < values->count; ++i) {
        if (strcmp(values->items[i].name, name) == 0) {
            values->items[i].value = value;
            return;
        }
    }

    if (values->count == values->capacity) {
        size_t new_capacity = values->capacity ? values->capacity * 2u : 64u;
        new_items = (NamedValue *)realloc(values->items, new_capacity * sizeof(NamedValue));
        if (!new_items) {
            fatal("out of memory");
        }
        values->items = new_items;
        values->capacity = new_capacity;
    }
    values->items[values->count].name = xstrdup(name);
    values->items[values->count].value = value;
    ++values->count;
}

static int named_values_get(const NamedValues *values, const char *name, uint32_t *out)
{
    size_t i;
    for (i = 0; i < values->count; ++i) {
        if (strcmp(values->items[i].name, name) == 0) {
            *out = values->items[i].value;
            return 1;
        }
    }
    return 0;
}

static int compare_named_values(const void *a, const void *b)
{
    const NamedValue *left = (const NamedValue *)a;
    const NamedValue *right = (const NamedValue *)b;
    return strcmp(left->name, right->name);
}

static uint32_t resolve_symbol(const ObjectSet *set, const NamedValues *global_defs,
    size_t obj_index, uint32_t sym_index, uint32_t base_va)
{
    const CoffObject *obj;
    const CoffSymbol *sym;
    uint32_t target;

    if (obj_index >= set->count) {
        fatal("bad object index");
    }
    obj = &set->objects[obj_index];
    if (sym_index >= obj->nsymbols) {
        fatal("%s relocation references bad symbol index %u", obj->path, sym_index);
    }

    sym = &obj->symbols[sym_index];
    if (sym->is_aux) {
        fatal("%s relocation references auxiliary COFF symbol index %u", obj->path, sym_index);
    }

    if (sym->section == IMAGE_SYM_ABSOLUTE) {
        return sym->value;
    }
    if (sym->section == 0) {
        if (!named_values_get(global_defs, sym->name, &target)) {
            fatal("unresolved external symbol in hook code: %s", sym->name);
        }
        return target;
    }
    if (sym->section < 0 || (uint16_t)sym->section > obj->nsects) {
        fatal("%s symbol %s references unsupported section %d", obj->path, sym->name, sym->section);
    }
    if (!obj->sections[sym->section].included) {
        fatal("%s hook code references unsupported section %s", obj->path, obj->sections[sym->section].name);
    }
    return base_va + obj->sections[sym->section].out_off + sym->value;
}

static void load_object_set(ObjectSet *set, const char * const *paths, size_t count)
{
    size_t i;
    if (count > MAX_OBJECTS) {
        fatal("too many object files");
    }
    memset(set, 0, sizeof(*set));
    set->count = count;
    for (i = 0; i < count; ++i) {
        parse_coff(paths[i], &set->objects[i]);
    }
}

static void link_coff_texts(const char * const *paths, size_t path_count, uint32_t base_va,
    const char *export_prefix, ByteVec *blob, NamedValues *exports)
{
    ObjectSet set;
    NamedValues global_defs;
    size_t obj_index;

    memset(blob, 0, sizeof(*blob));
    memset(exports, 0, sizeof(*exports));
    memset(&global_defs, 0, sizeof(global_defs));
    load_object_set(&set, paths, path_count);

    for (obj_index = 0; obj_index < set.count; ++obj_index) {
        CoffObject *obj = &set.objects[obj_index];
        uint16_t section_index;

        for (section_index = 1u; section_index <= obj->nsects; ++section_index) {
            CoffSection *section = &obj->sections[section_index];
            if ((section->chars & IMAGE_SCN_CNT_CODE) == 0 || section->raw_size == 0) {
                continue;
            }

            bytevec_pad_to(blob, align_up_size(blob->size, 0x10u));
            section->out_off = (uint32_t)blob->size;
            section->included = 1;
            bytevec_append(blob, obj->file.data + section->raw_ptr, section->raw_size);
        }
    }

    if (!blob->size) {
        fatal("hook objects contain no executable sections");
    }

    for (obj_index = 0; obj_index < set.count; ++obj_index) {
        CoffObject *obj = &set.objects[obj_index];
        uint32_t sym_index;

        for (sym_index = 0u; sym_index < obj->nsymbols; ++sym_index) {
            CoffSymbol *sym = &obj->symbols[sym_index];
            uint32_t value;

            if (sym->is_aux || sym->section <= 0 || sym->storage != IMAGE_SYM_CLASS_EXTERNAL) {
                continue;
            }
            if ((uint16_t)sym->section > obj->nsects || !obj->sections[sym->section].included) {
                continue;
            }
            value = base_va + obj->sections[sym->section].out_off + sym->value;
            named_values_set(&global_defs, sym->name, value);
        }
    }

    for (obj_index = 0; obj_index < set.count; ++obj_index) {
        CoffObject *obj = &set.objects[obj_index];
        uint16_t section_index;

        for (section_index = 1u; section_index <= obj->nsects; ++section_index) {
            CoffSection *section = &obj->sections[section_index];
            uint32_t rel_index;
            uint32_t section_base;
            uint32_t out_base;

            if (!section->included) {
                continue;
            }
            section_base = base_va + section->out_off;
            out_base = section->out_off;

            for (rel_index = 0u; rel_index < section->nrel; ++rel_index) {
                size_t rel_off = section->rel_ptr + (size_t)rel_index * 10u;
                uint32_t rva = rd32(obj->file.data, obj->file.size, rel_off);
                uint32_t sym_index = rd32(obj->file.data, obj->file.size, rel_off + 4u);
                uint16_t rtype = rd16(obj->file.data, obj->file.size, rel_off + 8u);
                uint32_t patch_off = out_base + rva;
                uint32_t target = resolve_symbol(&set, &global_defs, obj_index, sym_index, base_va);

                if ((size_t)patch_off + 4u > blob->size) {
                    fatal("%s relocation writes outside hook blob", obj->path);
                }

                if (rtype == IMAGE_REL_I386_DIR32) {
                    uint32_t addend = rd32(blob->data, blob->size, patch_off);
                    wr32(blob->data, blob->size, patch_off, target + addend);
                } else if (rtype == IMAGE_REL_I386_REL32) {
                    int32_t addend = (int32_t)rd32(blob->data, blob->size, patch_off);
                    int64_t value = (int64_t)target + addend - (int64_t)(section_base + rva + 4u);
                    wr32(blob->data, blob->size, patch_off, (uint32_t)value);
                } else {
                    fatal("unsupported i386 relocation type 0x%04x in hook code", rtype);
                }
            }
        }
    }

    for (obj_index = 0; obj_index < set.count; ++obj_index) {
        CoffObject *obj = &set.objects[obj_index];
        uint32_t sym_index;

        for (sym_index = 0u; sym_index < obj->nsymbols; ++sym_index) {
            CoffSymbol *sym = &obj->symbols[sym_index];
            uint32_t value;

            if (sym->is_aux || sym->section <= 0 || !starts_with(sym->name, export_prefix)) {
                continue;
            }
            if ((uint16_t)sym->section > obj->nsects || !obj->sections[sym->section].included) {
                continue;
            }
            value = obj->sections[sym->section].out_off + sym->value;
            named_values_set(exports, sym->name + 1, value);
        }
    }
}

static void require_symbols(const NamedValues *symbols, const char **required, size_t count, const char *label)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        uint32_t ignored;
        if (!named_values_get(symbols, required[i], &ignored)) {
            fatal("%s object is missing symbol: %s", label, required[i]);
        }
    }
}

static void append_blob_header(FILE *file, const char *prefix, uint32_t base_va,
    const ByteVec *blob, NamedValues *symbols)
{
    size_t i;
    size_t offset;

    qsort(symbols->items, symbols->count, sizeof(symbols->items[0]), compare_named_values);

    fprintf(file, "#define %s_BASE_VA 0x%08Xu\n", prefix, base_va);
    fprintf(file, "#define %s_BLOB_SIZE %uu\n\n", prefix, (unsigned)blob->size);

    for (i = 0; i < symbols->count; ++i) {
        fprintf(file, "#define %s_OFF_%s 0x%Xu\n", prefix, symbols->items[i].name, symbols->items[i].value);
    }

    fprintf(file, "\nstatic const unsigned char %s_BLOB[%s_BLOB_SIZE] = {\n", prefix, prefix);
    for (offset = 0; offset < blob->size; offset += 12u) {
        size_t end = offset + 12u;
        fprintf(file, "    ");
        if (end > blob->size) {
            end = blob->size;
        }
        for (i = offset; i < end; ++i) {
            fprintf(file, "0x%02X,", blob->data[i]);
            if (i + 1u < end) {
                fprintf(file, " ");
            }
        }
        fprintf(file, "\n");
    }
    fprintf(file, "};\n\n");
}

static void write_header(const Options *options)
{
    static const char *timwin_required[] = {
        "timwin_apply_dynamic_layout",
        "timwin_center_child_rect",
        "timwin_force_half_screen",
        "timwin_force_half_if_near_screen",
        "timwin_parent_client_height_metric",
        "timwin_parent_client_width_metric",
        "timwin_partbin_content_stretch",
        "timwin_scale_mouse_move",
        "timwin_scale_mouse_down",
        "timwin_child_create_layout_wrapper",
        "timwin_fullscreen_restore_size_hook",
        "timwin_parent_syscommand_restore_hook",
        "timwin_parent_resize_layout_hook",
        "timwin_toolbar_surface_native_layout_hook",
        "timwin_enter_command_hook",
        "timwin_enter_key_hook",
        "timwin_part_buffer_resize_hook",
        "timwin_part_surface_stretch_hook",
        "timwin_partbin_reopen_guard",
        "timwin_playfield_mouse_hook",
        "timwin_goal_mouse_hook",
        "timwin_part_mouse_hook",
        "timwin_playfield_region_client_bounds_hook",
        "timwin_input_mouse_move_hook",
        "timwin_input_mouse_down_hook",
        "timwin_toolbar_scaled_repaint_hook",
        "timwin_toolbar_partial_repaint_hook",
        "timwin_partbin_partial_stretch_hook",
        "timwin_toolbar_wm_paint_scale_hook",
        "timwin_shape_frame_table_guard",
        "timwin_single_shape_frame_guard",
    };
    static const char *sos9502_required[] = {
        "sos9502_reloc_guard",
        "sos9502_reloc_guard_end",
        "sos9502_midi_a00a",
        "sos9502_midi_a00a_end",
        "sos9502_midi_return",
        "sos9502_midi_return_end",
    };
    ByteVec timwin_blob;
    ByteVec sos9502_blob;
    NamedValues timwin_symbols;
    NamedValues sos9502_symbols;
    FILE *file;

    link_coff_texts(options->timwin_objects, options->timwin_count, options->timwin_base_va,
        "_timwin_", &timwin_blob, &timwin_symbols);
    link_coff_texts(options->sos9502_objects, options->sos9502_count, options->sos9502_base_va,
        "_sos9502_", &sos9502_blob, &sos9502_symbols);

    require_symbols(&timwin_symbols, timwin_required,
        sizeof(timwin_required) / sizeof(timwin_required[0]), "TIMWIN hook");
    require_symbols(&sos9502_symbols, sos9502_required,
        sizeof(sos9502_required) / sizeof(sos9502_required[0]), "SOS9502 hook");

    file = fopen(options->header_path, "wb");
    if (!file) {
        fatal("could not write %s", options->header_path);
    }

    fprintf(file, "/* Generated by tools/hooklink.c. Do not edit. */\n");
    fprintf(file, "#ifndef TIMWIN_HOOK_BLOB_H\n");
    fprintf(file, "#define TIMWIN_HOOK_BLOB_H\n\n");
    append_blob_header(file, "TIMWIN_HOOK", options->timwin_base_va, &timwin_blob, &timwin_symbols);
    append_blob_header(file, "SOS9502_HOOK", options->sos9502_base_va, &sos9502_blob, &sos9502_symbols);
    fprintf(file, "#endif\n");

    if (fclose(file) != 0) {
        fatal("could not finish writing %s", options->header_path);
    }
}

static uint32_t parse_u32(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (!end || *end != '\0' || value > 0xfffffffful) {
        fatal("invalid integer: %s", text);
    }
    return (uint32_t)value;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: hooklink.exe --header OUT --timwin-base-va VA --sos9502-base-va VA "
        "--timwin-obj OBJ [--timwin-obj OBJ ...] --sos9502-obj OBJ\n");
}

static void add_object_arg(const char **items, size_t *count, const char *value)
{
    if (*count >= MAX_OBJECTS) {
        fatal("too many object files");
    }
    items[*count] = value;
    ++*count;
}

static void parse_args(int argc, char **argv, Options *options)
{
    int i;

    memset(options, 0, sizeof(*options));
    options->timwin_base_va = 0x00500000u;
    options->sos9502_base_va = 0x00410F20u;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--header") == 0 && i + 1 < argc) {
            options->header_path = argv[++i];
        } else if (strcmp(argv[i], "--timwin-base-va") == 0 && i + 1 < argc) {
            options->timwin_base_va = parse_u32(argv[++i]);
        } else if (strcmp(argv[i], "--sos9502-base-va") == 0 && i + 1 < argc) {
            options->sos9502_base_va = parse_u32(argv[++i]);
        } else if (strcmp(argv[i], "--timwin-obj") == 0 && i + 1 < argc) {
            add_object_arg(options->timwin_objects, &options->timwin_count, argv[++i]);
        } else if (strcmp(argv[i], "--sos9502-obj") == 0 && i + 1 < argc) {
            add_object_arg(options->sos9502_objects, &options->sos9502_count, argv[++i]);
        } else {
            usage();
            exit(1);
        }
    }

    if (!options->header_path || !options->timwin_count || !options->sos9502_count) {
        usage();
        exit(1);
    }
}

int main(int argc, char **argv)
{
    Options options;
    parse_args(argc, argv, &options);
    write_header(&options);
    return 0;
}
