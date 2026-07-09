// Tests for named .scope file= output redirection and predefined defines.
// Stefan Wessels, 2026
// This is free and unencumbered software released into the public domain.

#include "asm.h"
#include "errorlog.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A named output target: a flat image plus the file/dest names it was opened with.
typedef struct {
    char name[64];
    char file[64];
    uint8_t ram[65536];
    int wrote_any;
    uint32_t lo;
    uint32_t hi;
} target_image;

typedef struct {
    target_image *def;          // default (unnamed) target
    target_image *opened[8];    // targets created by .scope file=
    int opened_count;
} target_host;

static void image_write(target_image *img, uint16_t addr, uint8_t val) {
    img->ram[addr] = val;
    if(!img->wrote_any) {
        img->wrote_any = 1;
        img->lo = addr;
        img->hi = (uint32_t)addr + 1;
    } else {
        if(addr < img->lo) {
            img->lo = addr;
        }
        if((uint32_t)addr + 1 > img->hi) {
            img->hi = (uint32_t)addr + 1;
        }
    }
}

static void host_output_byte(void *target, uint16_t addr, uint8_t val) {
    image_write((target_image *)target, addr, val);
}

static void *host_target_open(void *user, const char *name, int name_len,
                              const char *file, int file_len,
                              const char *dest, int dest_len) {
    target_host *host = (target_host *)user;
    (void)dest;
    (void)dest_len;
    target_image *img = calloc(1, sizeof(*img));
    if(!img) {
        return NULL;
    }
    if(name_len > 0 && name_len < (int)sizeof(img->name)) {
        memcpy(img->name, name, (size_t)name_len);
    }
    if(file && file_len > 0 && file_len < (int)sizeof(img->file)) {
        memcpy(img->file, file, (size_t)file_len);
    }
    if(host->opened_count < 8) {
        host->opened[host->opened_count++] = img;
    }
    return img;
}

static void host_target_release(void *user, void *target) {
    (void)user;
    free(target);
}

static target_image *host_find(target_host *host, const char *file) {
    for(int i = 0; i < host->opened_count; i++) {
        if(0 == strcmp(host->opened[i]->file, file)) {
            return host->opened[i];
        }
    }
    return NULL;
}

static int assemble(const char *source, target_host *host, ERRORLOG *log,
                    int provide_target_open, const char *predef_name, const char *predef_value) {
    char path[128];
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    if(c64m_test_write_temp_file(path, sizeof(path), "c64m_assembler_targets", source) != 0) {
        return ASM_ERR;
    }

    memset(&cb, 0, sizeof(cb));
    cb.user = host;
    cb.default_target = host->def;
    cb.output_byte = host_output_byte;
    if(provide_target_open) {
        cb.target_open = host_target_open;
        cb.target_release = host_target_release;
    }

    if(assembler_init(&as, log, &cb) != ASM_OK) {
        c64m_test_remove_file(path);
        return ASM_ERR;
    }
    if(predef_name) {
        assembler_predefine(&as, predef_name, predef_value);
    }
    result = assembler_assemble(&as, path, 0x0801);
    assembler_shutdown(&as);
    c64m_test_remove_file(path);
    return result;
}

// A named .scope file= routes its bytes to a separate target, leaving the
// default target untouched, and the split survives both assembly passes
// (forward references resolve to the right per-target addresses).
static int test_named_scope_to_file(void) {
    target_host host;
    target_image def;
    ERRORLOG log;
    int failures = 0;

    memset(&host, 0, sizeof(host));
    memset(&def, 0, sizeof(def));
    host.def = &def;

    const char *source =
        "* = $0801\n"
        "loader:\n"
        "    jmp done\n"
        "done:\n"
        "    rts\n"
        ".scope game file=\"game.bin\"\n"
        "    * = $c000\n"
        "    jmp gmain\n"
        "gmain:\n"
        "    inc $d021\n"
        ".endscope\n"
        "    lda #<game::gmain\n"   // cross-target symbol reference resolves to $C003
        "    sta $fb\n";

    errlog_init(&log);
    if(assemble(source, &host, &log, 1, NULL, NULL) != ASM_OK) {
        fprintf(stderr, "named-scope assembly failed with %zu errors\n", log.log_array.items);
        for(size_t i = 0; i < log.log_array.items; i++) {
            ERROR_ENTRY *e = ARRAY_GET(&log.log_array, ERROR_ENTRY, i);
            fprintf(stderr, "  %s\n", e->err_str ? e->err_str : "");
        }
        failures++;
    }

    // Default target: jmp done ($0804), rts, then lda #<gmain / sta $fb.
    // $0801: 4C 04 08   jmp $0804
    // $0804: 60         rts
    // $0805: A9 03      lda #$03   (low byte of gmain = $C003)
    // $0807: 85 FB      sta $fb
    const uint8_t def_expected[] = {0x4C, 0x04, 0x08, 0x60, 0xA9, 0x03, 0x85, 0xFB};
    if(!def.wrote_any || def.lo != 0x0801 ||
       memcmp(&def.ram[0x0801], def_expected, sizeof(def_expected)) != 0) {
        fprintf(stderr, "default target output mismatch (lo=$%04X)\n", def.lo);
        failures++;
    }

    // game.bin target: jmp gmain ($C003) then inc $d021.
    // $C000: 4C 03 C0   jmp $C003
    // $C003: EE 21 D0   inc $d021
    target_image *game = host_find(&host, "game.bin");
    const uint8_t game_expected[] = {0x4C, 0x03, 0xC0, 0xEE, 0x21, 0xD0};
    if(!game) {
        fprintf(stderr, "game.bin target was not opened\n");
        failures++;
    } else if(!game->wrote_any || game->lo != 0xC000 ||
              memcmp(&game->ram[0xC000], game_expected, sizeof(game_expected)) != 0) {
        fprintf(stderr, "game.bin target output mismatch (lo=$%04X)\n", game->lo);
        failures++;
    }

    // The redirect must not have leaked game bytes into the default image.
    if(def.hi > 0x0900) {
        fprintf(stderr, "default target unexpectedly wrote past $0900 (hi=$%04X)\n", def.hi);
        failures++;
    }

    errlog_shutdown(&log);
    return failures;
}

// A host without target_open must reject .scope file= rather than mis-route bytes.
static int test_scope_file_unsupported(void) {
    target_host host;
    target_image def;
    ERRORLOG log;
    int failures = 0;

    memset(&host, 0, sizeof(host));
    memset(&def, 0, sizeof(def));
    host.def = &def;

    const char *source =
        "* = $0801\n"
        ".scope game file=\"game.bin\"\n"
        "    .byte $01\n"
        ".endscope\n";

    errlog_init(&log);
    if(assemble(source, &host, &log, 0 /* no target_open */, NULL, NULL) != ASM_ERR ||
       log.log_array.items == 0) {
        fprintf(stderr, ".scope file= was not rejected when redirection is unsupported\n");
        failures++;
    }
    errlog_shutdown(&log);
    return failures;
}

// A predefined text define is visible to `.if` and survives both passes.
static int test_predefine_detection(void) {
    target_host host;
    target_image def;
    ERRORLOG log;
    int failures = 0;

    memset(&host, 0, sizeof(host));
    memset(&def, 0, sizeof(def));
    host.def = &def;

    const char *source =
        "* = $1000\n"
        ".if C64MASM\n"
        "    .byte $aa\n"
        ".else\n"
        "    .byte $bb\n"
        ".endif\n";

    // With C64MASM=1 the .if branch is taken -> $AA.
    errlog_init(&log);
    if(assemble(source, &host, &log, 1, "C64MASM", "1") != ASM_OK) {
        fprintf(stderr, "predefine (true) assembly failed\n");
        failures++;
    }
    if(def.ram[0x1000] != 0xAA) {
        fprintf(stderr, "predefine true: expected $AA at $1000, got $%02X\n", def.ram[0x1000]);
        failures++;
    }
    errlog_shutdown(&log);

    // With C64MASM=0 the .else branch is taken -> $BB.
    memset(&def, 0, sizeof(def));
    errlog_init(&log);
    if(assemble(source, &host, &log, 1, "C64MASM", "0") != ASM_OK) {
        fprintf(stderr, "predefine (false) assembly failed\n");
        failures++;
    }
    if(def.ram[0x1000] != 0xBB) {
        fprintf(stderr, "predefine false: expected $BB at $1000, got $%02X\n", def.ram[0x1000]);
        failures++;
    }
    errlog_shutdown(&log);

    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_named_scope_to_file();
    failures += test_scope_file_unsupported();
    failures += test_predefine_detection();

    return failures == 0 ? 0 : 1;
}
