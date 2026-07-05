#include "basic_v2.h"

#include <stdio.h>
#include <string.h>

/* BASIC V2 keyword table.  Index 0 corresponds to token $80 (END); the last
 * entry is token $CB (GO).  Token $FF is the PI constant, handled separately. */
#define BASIC_V2_TOKEN_BASE 0x80u
#define BASIC_V2_TOKEN_PI   0xFFu

static const char *const basic_v2_keywords[] = {
    "END",   "FOR",    "NEXT",   "DATA",   "INPUT#", "INPUT",  "DIM",   "READ",
    "LET",   "GOTO",   "RUN",    "IF",     "RESTORE","GOSUB",  "RETURN","REM",
    "STOP",  "ON",     "WAIT",   "LOAD",   "SAVE",   "VERIFY", "DEF",   "POKE",
    "PRINT#","PRINT",  "CONT",   "LIST",   "CLR",    "CMD",    "SYS",   "OPEN",
    "CLOSE", "GET",    "NEW",    "TAB(",   "TO",     "FN",     "SPC(",  "THEN",
    "NOT",   "STEP",   "+",      "-",      "*",      "/",      "^",     "AND",
    "OR",    ">",      "=",      "<",      "SGN",    "INT",    "ABS",   "USR",
    "FRE",   "POS",    "SQR",    "RND",    "LOG",    "EXP",    "COS",   "SIN",
    "TAN",   "ATN",    "PEEK",   "LEN",    "STR$",   "VAL",    "ASC",   "CHR$",
    "LEFT$", "RIGHT$", "MID$",   "GO"
};

#define BASIC_V2_KEYWORD_COUNT ((int)(sizeof(basic_v2_keywords) / sizeof(basic_v2_keywords[0])))

/* Token that suspends keyword crunching to end of line (REM). */
#define BASIC_V2_TOKEN_REM  0x8Fu
/* Token that suspends keyword crunching until the next colon (DATA). */
#define BASIC_V2_TOKEN_DATA 0x83u

static void basic_v2_set_err(char *err, size_t err_cap, const char *msg) {
    if (err != NULL && err_cap > 0) {
        snprintf(err, err_cap, "%s", msg);
    }
}

/* Uppercase an ASCII letter; other bytes pass through unchanged.  Source text
 * is normalised to the C64 default (uppercase/graphics) character set, where
 * letters live at PETSCII $41-$5A. */
static uint8_t basic_v2_to_upper(uint8_t c) {
    if (c >= 'a' && c <= 'z') {
        return (uint8_t)(c - 'a' + 'A');
    }
    return c;
}

/* Try to match a keyword at text+pos (already uppercased on read).  Returns the
 * token byte and the consumed length, or 0 if nothing matches.  The table is
 * scanned in ROM order so that longer/earlier forms (INPUT#, GOTO) win over
 * their prefixes (INPUT, GO). */
static uint8_t basic_v2_match_keyword(const char *text, size_t len, size_t pos,
                                      size_t *consumed) {
    int k;
    for (k = 0; k < BASIC_V2_KEYWORD_COUNT; ++k) {
        const char *kw = basic_v2_keywords[k];
        size_t kl = strlen(kw);
        size_t i;
        if (pos + kl > len) {
            continue;
        }
        for (i = 0; i < kl; ++i) {
            if (basic_v2_to_upper((uint8_t)text[pos + i]) != (uint8_t)kw[i]) {
                break;
            }
        }
        if (i == kl) {
            *consumed = kl;
            return (uint8_t)(BASIC_V2_TOKEN_BASE + (unsigned)k);
        }
    }
    return 0;
}

/* Append one byte to out, guarding capacity. */
static bool basic_v2_emit(uint8_t *out, size_t out_cap, size_t *n, uint8_t b) {
    if (*n >= out_cap) {
        return false;
    }
    out[(*n)++] = b;
    return true;
}

bool basic_v2_tokenize(const char *text, size_t text_len,
                       uint16_t load_addr,
                       uint8_t *out, size_t out_cap, size_t *out_len,
                       char *err, size_t err_cap) {
    size_t pos = 0;
    size_t n = 0;
    uint32_t addr = load_addr;

    if (text == NULL || out == NULL || out_len == NULL) {
        basic_v2_set_err(err, err_cap, "null argument");
        return false;
    }

    while (pos < text_len) {
        size_t line_start_addr = addr;
        size_t record_start = n;         /* index of this line's link bytes */
        size_t body_len;
        uint32_t line_number = 0;
        bool have_digits = false;
        bool in_quote = false;
        bool literal_rem = false;   /* copy rest of line literally */
        bool literal_data = false;  /* copy literally until ':' */

        /* Skip blank lines (only whitespace / CR / LF). */
        {
            size_t look = pos;
            bool blank = true;
            while (look < text_len && text[look] != '\n' && text[look] != '\r') {
                if (text[look] != ' ' && text[look] != '\t') {
                    blank = false;
                    break;
                }
                ++look;
            }
            if (blank) {
                /* consume the line terminator(s) and continue */
                while (pos < text_len && text[pos] != '\n' && text[pos] != '\r') {
                    ++pos;
                }
                while (pos < text_len && (text[pos] == '\n' || text[pos] == '\r')) {
                    ++pos;
                }
                continue;
            }
        }

        /* Leading whitespace before the line number is ignored. */
        while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }

        /* Line number. */
        while (pos < text_len && text[pos] >= '0' && text[pos] <= '9') {
            line_number = line_number * 10u + (uint32_t)(text[pos] - '0');
            have_digits = true;
            ++pos;
        }
        if (!have_digits) {
            basic_v2_set_err(err, err_cap, "line without a line number");
            return false;
        }
        if (line_number > 63999u) {
            basic_v2_set_err(err, err_cap, "line number exceeds 63999");
            return false;
        }
        /* One optional space after the line number is not significant; the ROM
         * skips spaces here.  Any further spaces are preserved. */
        while (pos < text_len && text[pos] == ' ') {
            ++pos;
        }

        /* Reserve link bytes, then emit line number low/high. */
        if (!basic_v2_emit(out, out_cap, &n, 0) ||
            !basic_v2_emit(out, out_cap, &n, 0)) {
            basic_v2_set_err(err, err_cap, "output buffer full");
            return false;
        }
        if (!basic_v2_emit(out, out_cap, &n, (uint8_t)(line_number & 0xFFu)) ||
            !basic_v2_emit(out, out_cap, &n, (uint8_t)((line_number >> 8) & 0xFFu))) {
            basic_v2_set_err(err, err_cap, "output buffer full");
            return false;
        }

        /* Line body. */
        while (pos < text_len && text[pos] != '\n' && text[pos] != '\r') {
            uint8_t c = (uint8_t)text[pos];

            if (in_quote) {
                if (!basic_v2_emit(out, out_cap, &n, basic_v2_to_upper(c))) {
                    basic_v2_set_err(err, err_cap, "output buffer full");
                    return false;
                }
                if (c == '"') {
                    in_quote = false;
                }
                ++pos;
                continue;
            }
            if (literal_rem) {
                if (!basic_v2_emit(out, out_cap, &n, basic_v2_to_upper(c))) {
                    basic_v2_set_err(err, err_cap, "output buffer full");
                    return false;
                }
                ++pos;
                continue;
            }
            if (literal_data) {
                if (c == ':') {
                    literal_data = false;
                    /* fall through to normal handling of ':' below */
                } else {
                    if (!basic_v2_emit(out, out_cap, &n, basic_v2_to_upper(c))) {
                        basic_v2_set_err(err, err_cap, "output buffer full");
                        return false;
                    }
                    ++pos;
                    continue;
                }
            }

            if (c == '"') {
                in_quote = true;
                if (!basic_v2_emit(out, out_cap, &n, c)) {
                    basic_v2_set_err(err, err_cap, "output buffer full");
                    return false;
                }
                ++pos;
                continue;
            }

            /* Attempt a keyword match. */
            {
                size_t consumed = 0;
                uint8_t token = basic_v2_match_keyword(text, text_len, pos, &consumed);
                if (token != 0) {
                    if (!basic_v2_emit(out, out_cap, &n, token)) {
                        basic_v2_set_err(err, err_cap, "output buffer full");
                        return false;
                    }
                    pos += consumed;
                    if (token == BASIC_V2_TOKEN_REM) {
                        literal_rem = true;
                    } else if (token == BASIC_V2_TOKEN_DATA) {
                        literal_data = true;
                    }
                    continue;
                }
            }

            /* Plain character (uppercased letters -> PETSCII $41-$5A). */
            if (!basic_v2_emit(out, out_cap, &n, basic_v2_to_upper(c))) {
                basic_v2_set_err(err, err_cap, "output buffer full");
                return false;
            }
            ++pos;
        }

        /* Line terminator byte. */
        if (!basic_v2_emit(out, out_cap, &n, 0)) {
            basic_v2_set_err(err, err_cap, "output buffer full");
            return false;
        }

        /* Back-patch the link pointer to point past this line. */
        body_len = n - record_start;               /* includes the 2 link bytes */
        {
            uint32_t link = (uint32_t)line_start_addr + (uint32_t)body_len;
            if (link > 0xFFFFu) {
                basic_v2_set_err(err, err_cap, "program too large for address space");
                return false;
            }
            out[record_start]     = (uint8_t)(link & 0xFFu);
            out[record_start + 1] = (uint8_t)((link >> 8) & 0xFFu);
            addr = link;
        }

        /* Consume the line terminator(s). */
        while (pos < text_len && (text[pos] == '\n' || text[pos] == '\r')) {
            ++pos;
        }
    }

    /* Terminating null link. */
    if (!basic_v2_emit(out, out_cap, &n, 0) ||
        !basic_v2_emit(out, out_cap, &n, 0)) {
        basic_v2_set_err(err, err_cap, "output buffer full");
        return false;
    }

    *out_len = n;
    return true;
}

/* Append a NUL-terminated string to out, guarding capacity. */
static bool basic_v2_emit_str(char *out, size_t out_cap, size_t *n, const char *s) {
    while (*s != '\0') {
        if (*n >= out_cap) {
            return false;
        }
        out[(*n)++] = *s++;
    }
    return true;
}

static bool basic_v2_emit_char(char *out, size_t out_cap, size_t *n, char c) {
    if (*n >= out_cap) {
        return false;
    }
    out[(*n)++] = c;
    return true;
}

/* Convert a PETSCII text byte to ASCII for output.  Letters stored at $41-$5A
 * and the shifted uppercase range $C1-$DA both become ASCII 'A'-'Z'; other
 * printable bytes pass through unchanged. */
static char basic_v2_petscii_to_ascii(uint8_t b) {
    if (b >= 0xC1u && b <= 0xDAu) {
        return (char)(b - 0xC1u + 'A');
    }
    return (char)b;
}

bool basic_v2_detokenize(const uint8_t *bytes, size_t len,
                         uint16_t load_addr,
                         char *out, size_t out_cap, size_t *out_len,
                         char *err, size_t err_cap) {
    size_t pos = 0;
    size_t n = 0;
    char numbuf[8];

    (void)load_addr;

    if (bytes == NULL || out == NULL || out_len == NULL) {
        basic_v2_set_err(err, err_cap, "null argument");
        return false;
    }

    while (pos + 2u <= len) {
        uint16_t link;
        uint16_t line_number;
        bool in_quote = false;

        link = (uint16_t)(bytes[pos] | ((uint16_t)bytes[pos + 1] << 8));
        if (link == 0) {
            break;  /* end of program */
        }
        pos += 2u;

        if (pos + 2u > len) {
            basic_v2_set_err(err, err_cap, "truncated line header");
            return false;
        }
        line_number = (uint16_t)(bytes[pos] | ((uint16_t)bytes[pos + 1] << 8));
        pos += 2u;

        snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)line_number);
        if (!basic_v2_emit_str(out, out_cap, &n, numbuf) ||
            !basic_v2_emit_char(out, out_cap, &n, ' ')) {
            basic_v2_set_err(err, err_cap, "output buffer full");
            return false;
        }

        while (pos < len && bytes[pos] != 0) {
            uint8_t b = bytes[pos++];

            if (b == '"') {
                in_quote = !in_quote;
                if (!basic_v2_emit_char(out, out_cap, &n, '"')) {
                    basic_v2_set_err(err, err_cap, "output buffer full");
                    return false;
                }
                continue;
            }
            if (!in_quote && b >= BASIC_V2_TOKEN_BASE) {
                if (b == BASIC_V2_TOKEN_PI) {
                    if (!basic_v2_emit_str(out, out_cap, &n, "{PI}")) {
                        basic_v2_set_err(err, err_cap, "output buffer full");
                        return false;
                    }
                } else if (b <= BASIC_V2_TOKEN_BASE + (unsigned)BASIC_V2_KEYWORD_COUNT - 1u) {
                    const char *kw = basic_v2_keywords[b - BASIC_V2_TOKEN_BASE];
                    if (!basic_v2_emit_str(out, out_cap, &n, kw)) {
                        basic_v2_set_err(err, err_cap, "output buffer full");
                        return false;
                    }
                }
                /* Undefined tokens ($CC-$FE) produce no output. */
                continue;
            }

            if (!basic_v2_emit_char(out, out_cap, &n, basic_v2_petscii_to_ascii(b))) {
                basic_v2_set_err(err, err_cap, "output buffer full");
                return false;
            }
        }

        if (!basic_v2_emit_char(out, out_cap, &n, '\n')) {
            basic_v2_set_err(err, err_cap, "output buffer full");
            return false;
        }

        if (pos < len && bytes[pos] == 0) {
            ++pos;  /* skip line terminator */
        } else {
            break;  /* ran off the end without a terminator */
        }
    }

    /* NUL-terminate when there is room; the terminator is not counted. */
    if (n < out_cap) {
        out[n] = '\0';
    }
    *out_len = n;
    return true;
}
