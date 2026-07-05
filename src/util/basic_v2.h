#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Commodore BASIC V2 tokenizer / detokenizer.
 *
 * These convert between ASCII source text and the in-memory tokenized program
 * image that BASIC V2 keeps starting at TXTTAB (normally $0801).  Only stock
 * BASIC V2 keywords are handled; extension dialects (Simon's BASIC, etc.) use
 * different token tables and are out of scope.
 *
 * The tokenized image layout, per line, is:
 *     [link lo][link hi][line# lo][line# hi][token bytes...][$00]
 * terminated by a final [$00][$00] link.  Link pointers are absolute addresses
 * based on load_addr.
 */

/* Tokenize ASCII BASIC source text into a tokenized program image.
 *
 * text/text_len is the source; each logical line must start with a decimal
 * line number.  load_addr is the address the image will occupy (normally
 * $0801).  The image is written into out (capacity out_cap) and *out_len is
 * set to the number of bytes produced, including the terminating $00 $00 link.
 *
 * Returns true on success.  On failure returns false and, if err is non-NULL,
 * writes a human-readable message into err (capacity err_cap). */
bool basic_v2_tokenize(const char *text, size_t text_len,
                       uint16_t load_addr,
                       uint8_t *out, size_t out_cap, size_t *out_len,
                       char *err, size_t err_cap);

/* Detokenize a tokenized program image into ASCII BASIC source text.
 *
 * bytes/len is the image beginning with the first line's link pointer, as it
 * sits in memory at load_addr.  Output lines are "<number> <text>\n".  The
 * result is NUL-terminated when it fits.  *out_len is set to the text length
 * excluding the terminating NUL.
 *
 * Walking stops at a $0000 link or when the buffer is exhausted.  Returns true
 * on success; on failure returns false with a message in err (if non-NULL). */
bool basic_v2_detokenize(const uint8_t *bytes, size_t len,
                         uint16_t load_addr,
                         char *out, size_t out_cap, size_t *out_len,
                         char *err, size_t err_cap);
