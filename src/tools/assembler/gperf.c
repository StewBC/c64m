/* ANSI-C code produced by gperf version 3.0.3 */
/* Command-line: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/gperf --language=ANSI-C -c --output-file=src/tools/assembler/gperf.c src/tools/assembler/gperf.gperf  */
/* Computed positions: -k'1-2,5,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "src/tools/assembler/gperf.gperf"

#include "asm_lib.h"
#line 7 "src/tools/assembler/gperf.gperf"
struct OPCODEINFO;

#define TOTAL_KEYWORDS 92
#define MIN_WORD_LENGTH 3
#define MAX_WORD_LENGTH 10
#define MIN_HASH_VALUE 4
#define MAX_HASH_VALUE 226
/* maximum key range = 223, duplicates = 0 */

#ifndef GPERF_DOWNCASE
#define GPERF_DOWNCASE 1
static unsigned char gperf_downcase[256] =
  {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106,
    107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
    122,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255
  };
#endif

#ifndef GPERF_CASE_STRNCMP
#define GPERF_CASE_STRNCMP 1
static int
gperf_case_strncmp (register const char *s1, register const char *s2, register unsigned int n)
{
  for (; n > 0;)
    {
      unsigned char c1 = gperf_downcase[(unsigned char)*s1++];
      unsigned char c2 = gperf_downcase[(unsigned char)*s2++];
      if (c1 != 0 && c1 == c2)
        {
          n--;
          continue;
        }
      return (int)c1 - (int)c2;
    }
  return 0;
}
#endif

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
hash (register const char *str, register unsigned int len)
{
  static unsigned char asso_values[] =
    {
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227,   5, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227,   0, 227,  15, 227,
        0, 227, 227, 227, 227,   0, 227, 227, 227, 227,
      227, 227, 227, 227, 227,  65,  32,   5,  15,   0,
        0,  10, 227,  25,  62,   0,  65,  45, 110,  95,
       30,  72,  15,   0,  20,  84,   0, 105,  42,   2,
       10, 227, 227, 227, 227, 227, 227,  65,  32,   5,
       15,   0,   0,  10, 227,  25,  62,   0,  65,  45,
      110,  95,  30,  72,  15,   0,  20,  84,   0, 105,
       42,   2,  10, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227, 227, 227, 227,
      227, 227, 227, 227, 227, 227, 227
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[4]];
      /*FALLTHROUGH*/
      case 4:
      case 3:
      case 2:
        hval += asso_values[(unsigned char)str[1]+1];
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

struct OPCODEINFO *
in_word_set (register const char *str, register unsigned int len)
{
  static struct OPCODEINFO wordlist[] =
    {
      {""}, {""}, {""}, {""},
#line 39 "src/tools/assembler/gperf.gperf"
      {".res",       GPERF_DOT_RES       , 0, 0, 0},
#line 21 "src/tools/assembler/gperf.gperf"
      {".else",      GPERF_DOT_ELSE      , 0, 0, 0},
      {""}, {""},
#line 90 "src/tools/assembler/gperf.gperf"
      {"sec",        GPERF_OPCODE_SEC    , 0, 0, 0},
#line 28 "src/tools/assembler/gperf.gperf"
      {".endscope",  GPERF_DOT_ENDSCOPE  , 0, 0, 0},
#line 15 "src/tools/assembler/gperf.gperf"
      {".byte",      GPERF_DOT_BYTE      , 0, 0, 0},
      {""}, {""},
#line 89 "src/tools/assembler/gperf.gperf"
      {"sbc",        GPERF_OPCODE_SBC    , 8, 0, 0},
      {""},
#line 10 "src/tools/assembler/gperf.gperf"
      {".6502     ", GPERF_DOT_6502      , 0, 0, 0},
      {""}, {""},
#line 91 "src/tools/assembler/gperf.gperf"
      {"sed",        GPERF_OPCODE_SED    , 0, 0, 0},
      {""},
#line 68 "src/tools/assembler/gperf.gperf"
      {"dey",        GPERF_OPCODE_DEY    , 0, 0, 0},
      {""},
#line 22 "src/tools/assembler/gperf.gperf"
      {".endfor",    GPERF_DOT_ENDFOR    , 0, 0, 0},
#line 66 "src/tools/assembler/gperf.gperf"
      {"dec",        GPERF_OPCODE_DEC    , 8, 0, 0},
      {""},
#line 100 "src/tools/assembler/gperf.gperf"
      {"txs",        GPERF_OPCODE_TXS    , 0, 0, 0},
      {""},
#line 38 "src/tools/assembler/gperf.gperf"
      {".repeat",    GPERF_DOT_REPEAT    , 0, 0, 0},
#line 92 "src/tools/assembler/gperf.gperf"
      {"sei",        GPERF_OPCODE_SEI    , 0, 0, 0},
#line 29 "src/tools/assembler/gperf.gperf"
      {".for",       GPERF_DOT_FOR       , 0, 0, 0},
#line 11 "src/tools/assembler/gperf.gperf"
      {".65c02    ", GPERF_DOT_65c02     , 0, 0, 0},
#line 23 "src/tools/assembler/gperf.gperf"
      {".endif",     GPERF_DOT_ENDIF     , 0, 0, 0},
#line 16 "src/tools/assembler/gperf.gperf"
      {".define",    GPERF_DOT_DEFINE    , 0, 0, 0},
#line 43 "src/tools/assembler/gperf.gperf"
      {".strcode",   GPERF_DOT_STRCODE   , 0, 0, 0},
      {""},
#line 56 "src/tools/assembler/gperf.gperf"
      {"brk",        GPERF_OPCODE_BRK    , 0, 0, 0},
#line 20 "src/tools/assembler/gperf.gperf"
      {".dword",     GPERF_DOT_DWORD     , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""},
#line 40 "src/tools/assembler/gperf.gperf"
      {".segdef",    GPERF_DOT_SEGDEF    , 0, 0, 0},
#line 25 "src/tools/assembler/gperf.gperf"
      {".endproc",   GPERF_DOT_ENDPROC   , 0, 0, 0},
#line 35 "src/tools/assembler/gperf.gperf"
      {".org",       GPERF_DOT_ORG       , 0, 0, 0},
#line 27 "src/tools/assembler/gperf.gperf"
      {".endrepeat", GPERF_DOT_ENDREPEAT , 0, 0, 0},
      {""}, {""},
#line 69 "src/tools/assembler/gperf.gperf"
      {"eor",        GPERF_OPCODE_EOR    , 8, 0, 0},
      {""},
#line 50 "src/tools/assembler/gperf.gperf"
      {"bcs",        GPERF_OPCODE_BCS    , 1, 0, 0},
#line 37 "src/tools/assembler/gperf.gperf"
      {".qword",     GPERF_DOT_QWORD     , 0, 0, 0},
#line 26 "src/tools/assembler/gperf.gperf"
      {".endrep",    GPERF_DOT_ENDREPEAT , 0, 0, 0},
#line 62 "src/tools/assembler/gperf.gperf"
      {"clv",        GPERF_OPCODE_CLV    , 0, 0, 0},
      {""},
#line 49 "src/tools/assembler/gperf.gperf"
      {"bcc",        GPERF_OPCODE_BCC    , 1, 0, 0},
#line 42 "src/tools/assembler/gperf.gperf"
      {".scope",     GPERF_DOT_SCOPE     , 0, 0, 0},
#line 97 "src/tools/assembler/gperf.gperf"
      {"tay",        GPERF_OPCODE_TAY    , 0, 0, 0},
#line 59 "src/tools/assembler/gperf.gperf"
      {"clc",        GPERF_OPCODE_CLC    , 0, 0, 0},
      {""},
#line 67 "src/tools/assembler/gperf.gperf"
      {"dex",        GPERF_OPCODE_DEX    , 0, 0, 0},
      {""},
#line 44 "src/tools/assembler/gperf.gperf"
      {".string",    GPERF_DOT_STRING    , 0, 0, 0},
#line 86 "src/tools/assembler/gperf.gperf"
      {"ror",        GPERF_OPCODE_ROR    , 8, 0, 0},
      {""},
#line 30 "src/tools/assembler/gperf.gperf"
      {".if",        GPERF_DOT_IF        , 0, 0, 0},
      {""},
#line 12 "src/tools/assembler/gperf.gperf"
      {".addr",      GPERF_DOT_WORD      , 0, 0, 0},
#line 60 "src/tools/assembler/gperf.gperf"
      {"cld",        GPERF_OPCODE_CLD    , 0, 0, 0},
      {""},
#line 77 "src/tools/assembler/gperf.gperf"
      {"ldy",        GPERF_OPCODE_LDY    , 8, 0, 0},
      {""}, {""},
#line 46 "src/tools/assembler/gperf.gperf"
      {"adc",        GPERF_OPCODE_ADC    , 8, 0, 0},
#line 14 "src/tools/assembler/gperf.gperf"
      {".asciiz",    GPERF_DOT_ASCIIZ    , 0, 0, 0},
      {""}, {""},
#line 45 "src/tools/assembler/gperf.gperf"
      {".word",      GPERF_DOT_WORD      , 0, 0, 0},
#line 61 "src/tools/assembler/gperf.gperf"
      {"cli",        GPERF_OPCODE_CLI    , 0, 0, 0},
      {""}, {""}, {""},
#line 65 "src/tools/assembler/gperf.gperf"
      {"cpy",        GPERF_OPCODE_CPY    , 8, 0, 0},
      {""}, {""},
#line 98 "src/tools/assembler/gperf.gperf"
      {"tsx",        GPERF_OPCODE_TSX    , 0, 0, 0},
      {""},
#line 36 "src/tools/assembler/gperf.gperf"
      {".proc",      GPERF_DOT_PROC      , 0, 0, 0},
#line 82 "src/tools/assembler/gperf.gperf"
      {"php",        GPERF_OPCODE_PHP    , 0, 0, 0},
#line 95 "src/tools/assembler/gperf.gperf"
      {"sty",        GPERF_OPCODE_STY    , 8, 0, 0},
#line 99 "src/tools/assembler/gperf.gperf"
      {"txa",        GPERF_OPCODE_TXA    , 0, 0, 0},
      {""}, {""},
#line 41 "src/tools/assembler/gperf.gperf"
      {".segment",   GPERF_DOT_SEGMENT   , 0, 0, 0},
      {""}, {""}, {""},
#line 96 "src/tools/assembler/gperf.gperf"
      {"tax",        GPERF_OPCODE_TAX    , 0, 0, 0},
#line 101 "src/tools/assembler/gperf.gperf"
      {"tya",        GPERF_OPCODE_TYA    , 0, 0, 0},
      {""},
#line 74 "src/tools/assembler/gperf.gperf"
      {"jsr",        GPERF_OPCODE_JSR    ,16, 0, 0},
      {""},
#line 88 "src/tools/assembler/gperf.gperf"
      {"rts",        GPERF_OPCODE_RTS    , 0, 0, 0},
#line 78 "src/tools/assembler/gperf.gperf"
      {"lsr",        GPERF_OPCODE_LSR    , 0, 0, 0},
      {""}, {""}, {""},
#line 51 "src/tools/assembler/gperf.gperf"
      {"beq",        GPERF_OPCODE_BEQ    , 1, 0, 0},
#line 84 "src/tools/assembler/gperf.gperf"
      {"plp",        GPERF_OPCODE_PLP    , 0, 0, 0},
      {""},
#line 76 "src/tools/assembler/gperf.gperf"
      {"ldx",        GPERF_OPCODE_LDX    , 8, 0, 0},
      {""}, {""},
#line 85 "src/tools/assembler/gperf.gperf"
      {"rol",        GPERF_OPCODE_ROL    , 8, 0, 0},
      {""}, {""}, {""},
#line 52 "src/tools/assembler/gperf.gperf"
      {"bit",        GPERF_OPCODE_BIT    , 8, 0, 0},
      {""}, {""}, {""}, {""},
#line 64 "src/tools/assembler/gperf.gperf"
      {"cpx",        GPERF_OPCODE_CPX    , 8, 0, 0},
#line 81 "src/tools/assembler/gperf.gperf"
      {"pha",        GPERF_OPCODE_PHA    , 0, 0, 0},
      {""},
#line 72 "src/tools/assembler/gperf.gperf"
      {"iny",        GPERF_OPCODE_INY    , 0, 0, 0},
#line 18 "src/tools/assembler/gperf.gperf"
      {".drowd",     GPERF_DOT_DROWD     , 0, 0, 0},
#line 87 "src/tools/assembler/gperf.gperf"
      {"rti",        GPERF_OPCODE_RTI    , 0, 0, 0},
#line 70 "src/tools/assembler/gperf.gperf"
      {"inc",        GPERF_OPCODE_INC    , 8, 0, 0},
#line 94 "src/tools/assembler/gperf.gperf"
      {"stx",        GPERF_OPCODE_STX    , 8, 0, 0},
#line 54 "src/tools/assembler/gperf.gperf"
      {"bne",        GPERF_OPCODE_BNE    , 1, 0, 0},
      {""}, {""},
#line 75 "src/tools/assembler/gperf.gperf"
      {"lda",        GPERF_OPCODE_LDA    , 8, 0, 0},
      {""},
#line 32 "src/tools/assembler/gperf.gperf"
      {".include",   GPERF_DOT_INCLUDE   , 0, 0, 0},
      {""}, {""}, {""}, {""},
#line 58 "src/tools/assembler/gperf.gperf"
      {"bvs",        GPERF_OPCODE_BVS    , 1, 0, 0},
      {""}, {""},
#line 83 "src/tools/assembler/gperf.gperf"
      {"pla",        GPERF_OPCODE_PLA    , 0, 0, 0},
      {""},
#line 57 "src/tools/assembler/gperf.gperf"
      {"bvc",        GPERF_OPCODE_BVC    , 1, 0, 0},
      {""}, {""},
#line 63 "src/tools/assembler/gperf.gperf"
      {"cmp",        GPERF_OPCODE_CMP    , 8, 0, 0},
#line 24 "src/tools/assembler/gperf.gperf"
      {".endmacro",  GPERF_DOT_ENDMACRO  , 0, 0, 0},
      {""}, {""},
#line 93 "src/tools/assembler/gperf.gperf"
      {"sta",        GPERF_OPCODE_STA    , 8, 0, 0},
#line 48 "src/tools/assembler/gperf.gperf"
      {"asl",        GPERF_OPCODE_ASL    , 8, 0, 0},
      {""}, {""}, {""}, {""},
#line 13 "src/tools/assembler/gperf.gperf"
      {".align",     GPERF_DOT_ALIGN     , 0, 0, 0},
      {""}, {""}, {""}, {""},
#line 80 "src/tools/assembler/gperf.gperf"
      {"ora",        GPERF_OPCODE_ORA    , 8, 0, 0},
      {""},
#line 71 "src/tools/assembler/gperf.gperf"
      {"inx",        GPERF_OPCODE_INX    , 0, 0, 0},
      {""}, {""}, {""}, {""},
#line 53 "src/tools/assembler/gperf.gperf"
      {"bmi",        GPERF_OPCODE_BMI    , 1, 0, 0},
      {""},
#line 55 "src/tools/assembler/gperf.gperf"
      {"bpl",        GPERF_OPCODE_BPL    , 1, 0, 0},
#line 79 "src/tools/assembler/gperf.gperf"
      {"nop",        GPERF_OPCODE_NOP    , 0, 0, 0},
      {""}, {""}, {""}, {""},
#line 47 "src/tools/assembler/gperf.gperf"
      {"and",        GPERF_OPCODE_AND    , 8, 0, 0},
      {""}, {""},
#line 33 "src/tools/assembler/gperf.gperf"
      {".local",     GPERF_DOT_LOCAL     , 0, 0, 0},
      {""},
#line 19 "src/tools/assembler/gperf.gperf"
      {".drowq",     GPERF_DOT_DROWQ     , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""},
#line 73 "src/tools/assembler/gperf.gperf"
      {"jmp",        GPERF_OPCODE_JMP    ,16, 0, 0},
      {""}, {""}, {""}, {""}, {""},
#line 31 "src/tools/assembler/gperf.gperf"
      {".incbin",    GPERF_DOT_INCBIN    , 0, 0, 0},
      {""}, {""}, {""},
#line 17 "src/tools/assembler/gperf.gperf"
      {".drow",      GPERF_DOT_DROW      , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""},
#line 34 "src/tools/assembler/gperf.gperf"
      {".macro",     GPERF_DOT_MACRO     , 0, 0, 0}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register const char *s = wordlist[key].mnemonic;

          if ((((unsigned char)*str ^ (unsigned char)*s) & ~32) == 0 && !gperf_case_strncmp (str, s, len) && s[len] == '\0')
            return &wordlist[key];
        }
    }
  return 0;
}
#line 102 "src/tools/assembler/gperf.gperf"

