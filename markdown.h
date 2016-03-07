/* sd_markdown.h - generic markdown parser 
 *
 *  A single header modification of the Sundown markdown parser,
 *  which is itself a modification of the earlier Upskirt parser.
 *
 *  As fast, secure, and efficient as the library it's converted from, which
 *  was deprecated in 2012. So take it with a grain of salt.
 *
 *  # INCLUSION
 *  In *ONE* source file, put:
 * 
 *     #define SD_IMPLEMENTATION
 *     #include "markdown.h"
 *
 *  All others may simply #include "markdown.h"
 *
 *  By default, the HTML renderer is included. To disable it (e.g. if you are not
 *  using it), add above the #include:
 *
 *     #define SD_NO_HTML
 *
 *  # Compiler warnings
 *
 *  In MSVC v19.x, this header will generate the following two warnings on level 4:
 *
 *      warning C4100: '____': unreferenced formal parameter
 *      warning C4146: unary minus operator applied to unsigned type
 *
 *  These will not be fixed for algorithmic/API reasons. I recommend you ignore them.
 *
 *  In GCC, in C99 mode, this header should compile without warnings. It will not
 *  compile in C89 mode.
 *
 *  # DOCUMENTATION
 *
 *  ## BASIC USAGE
 *  
 *      struct sd_markdown *md = 
 *          sd_markdown_new(extensions, max_nesting, &callbacks, &your_data);
 *
 *      sd_markdown_render(output_buffer, input_data, in_data_size, md);
 *
 *      sd_markdown_free(md);
 *
 *  ### Explanation:
 *
 *  Sundown supports several extensions to the markdown syntax which can be
 *  selectively enabled when creating a new context:
 *
 *       unsigned int extensions = MKDEXT_TABLE | MKDEXT_SUPERSCRIPT ... ;
 *    
 *  You will need to decide on a maximum nesting level for the context to use
 *  ahead of time. Most markdown documents don't end up nesting very deep, so
 *  a low number (10-20) is probably fine. 
 *
 *       size_t max_nesting = 15;
 *
 *  Sundown will call back into your code to render an output document:
 *
 *       struct sd_callbacks callbacks;
 *
 *  Most of its callbacks are of the form:
 *
 *       void callback(struct sd_buf *output_buffer, const struct sd_buf *text, void* opaque);
 *
 *  Where output_buffer is an sd_buf that you write the output string to, and 
 *  text is the raw textual contents of whichever piece of syntax you're getting 
 *  a callback for. The span-level callbacks also need to return a value, which
 *  tells it whether you handled the content specially or if it should just print
 *  it out verbatim. The opaque pointer is some piece of your own data that you 
 *  pass in when you create the markdown parsing context.
 *
 *       struct sd_markdown *md = 
 *        sd_markdown_new(extensions, max_nesting, &callbacks, &your_data);
 *
 *  The call to render is when you provide an output buffer. sd_bufnew requires an
 *  initial capacity, but you will have the chance to grow the buffer during
 *  rendering callbacks.
 *
 *       struct sd_buf *output_buffer = sd_bufnew(...);
 *
 *  Input data is simply an array of characters, which for the most part are left 
 *  as-is and thus should more or less support UTF-8. There are a few functions that
 *  make assumptions about character size (for example running tolower() on a single
 *  8-bit character), so watch out for that.
 *
 *       sd_markdown_render(output_buffer, input_data, in_data_size, md);
 *
 *  And of course, to clean up when you're done:
 *
 *       sd_markdown_free(md);
 *
 *  ## HTML RENDERING
 *
 *  Included is the fairly compliant HTML renderer that was packaged with sundown 
 *  originally. It will be enabled by default unless SD_NO_HTML is defined.
 *
 *       sdhtml_renderer(&callbacks, &options, 0);
 *
 *       struct sd_markdown *md = 
 *           sd_markdown_new(extensions, max_nesting, &callbacks, &options);
 *
 *       sd_markdown_render(output_buffer, input_data, in_data_size, md);
 *
 *       sd_markdown_free(md);
 *
 *  ### Explanation
 *
 *  sdhtml_renderer will initialize the struct sd_callbacks with the set of html
 *  rendering callbacks defined near the bottom of this file. It will also
 *  initialize a struct html_renderopt that holds some information it needs to use
 *  while rendering the document, which you should pass in as the opaque pointer
 *  when creating the markdown parser.
 *
 *  # Philosophy
 *
 *  This port of sundown is crafted in the style of Sean Barett's stb_ libraries
 *  (https://github.com/nothings/stb). As this is merely an adaptation of an existing
 *  library, the usual priorities are somewhat diluted, but nonetheless are:
 *
 *   1. Ease of use
 *   2. Ease of maintenance
 *   3. Performance
 *
 *  The first two priorities are reflected in this port in that it is made to be even
 *  more portable than the original library (one header file, as opposed to 8-odd .c
 *  and .h files), uses no dependencies other than the CRT, and strives to be careful
 *  about names in the global namespace. 
 *
 *  # HISTORY
 *   - v0.90   2016-03-06      First public release
 */

/* ORIGINAL COPYRIGHT NOTICE:
 * Copyright (c) 2009, Natacha Porté
 * Copyright (c) 2011, Vicent Marti
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SD_SINGLE_HEADER
#define SD_SINGLE_HEADER

#ifdef __cplusplus
extern "C" {
#endif

// We disable CRT security warnings as the code is written for
// _snprintf, not _snprintf_s.
#define _CRT_SECURE_NO_WARNINGS 1
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define strncasecmp	_strnicmp
#endif

#if defined(_MSC_VER)
#define __attribute__(x)
#define inline
#endif

#if defined(_WIN32)
#define snprintf	_snprintf		
#endif

struct sd_buf;

// REGION: MARKDOWN.H

// We maintain the sundown version numbers as it is a clean conversion from
// this version of the sundown codebase.
#define SUNDOWN_VERSION "1.16.0"
#define SUNDOWN_VER_MAJOR 1
#define SUNDOWN_VER_MINOR 16
#define SUNDOWN_VER_REVISION 0

/********************
 * TYPE DEFINITIONS *
 ********************/

/* mkd_autolink - type of autolink */
enum mkd_autolink {
	MKDA_NOT_AUTOLINK,	/* used internally when it is not an autolink*/
	MKDA_NORMAL,		/* normal http/http/ftp/mailto/etc link */
	MKDA_EMAIL,			/* e-mail link without explit mailto: */
};

enum mkd_tableflags {
	MKD_TABLE_ALIGN_L = 1,
	MKD_TABLE_ALIGN_R = 2,
	MKD_TABLE_ALIGN_CENTER = 3,
	MKD_TABLE_ALIGNMASK = 3,
	MKD_TABLE_HEADER = 4
};

enum mkd_extensions {
	MKDEXT_NO_INTRA_EMPHASIS = (1 << 0),
	MKDEXT_TABLES = (1 << 1),
	MKDEXT_FENCED_CODE = (1 << 2),
	MKDEXT_AUTOLINK = (1 << 3),
	MKDEXT_STRIKETHROUGH = (1 << 4),
	MKDEXT_SPACE_HEADERS = (1 << 6),
	MKDEXT_SUPERSCRIPT = (1 << 7),
	MKDEXT_LAX_SPACING = (1 << 8),
};

/* sd_callbacks - functions for rendering parsed data */
struct sd_callbacks {
	/* block level callbacks - NULL skips the block */
	void (*blockcode)(struct sd_buf *ob, const struct sd_buf *text, const struct sd_buf *lang, void *opaque);
	void (*blockquote)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	void (*blockhtml)(struct sd_buf *ob,const  struct sd_buf *text, void *opaque);
	void (*header)(struct sd_buf *ob, const struct sd_buf *text, int level, void *opaque);
	void (*hrule)(struct sd_buf *ob, void *opaque);
	void (*list)(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque);
	void (*listitem)(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque);
	void (*paragraph)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	void (*table)(struct sd_buf *ob, const struct sd_buf *header, const struct sd_buf *body, void *opaque);
	void (*table_row)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	void (*table_cell)(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque);


	/* span level callbacks - NULL or return 0 prints the span verbatim */
	int (*autolink)(struct sd_buf *ob, const struct sd_buf *link, enum mkd_autolink type, void *opaque);
	int (*codespan)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	int (*double_emphasis)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	int (*emphasis)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	int (*image)(struct sd_buf *ob, const struct sd_buf *link, const struct sd_buf *title, const struct sd_buf *alt, void *opaque);
	int (*linebreak)(struct sd_buf *ob, void *opaque);
	int (*link)(struct sd_buf *ob, const struct sd_buf *link, const struct sd_buf *title, const struct sd_buf *content, void *opaque);
	int (*raw_html_tag)(struct sd_buf *ob, const struct sd_buf *tag, void *opaque);
	int (*triple_emphasis)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	int (*strikethrough)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	int (*superscript)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);

	/* low level callbacks - NULL copies input directly into the output */
	void (*entity)(struct sd_buf *ob, const struct sd_buf *entity, void *opaque);
	void (*normal_text)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);

	/* header and footer */
	void (*doc_header)(struct sd_buf *ob, void *opaque);
	void (*doc_footer)(struct sd_buf *ob, void *opaque);
};

struct sd_markdown;

/*********
 * FLAGS *
 *********/

/* list/listitem flags */
#define MKD_LIST_ORDERED	1
#define MKD_LI_BLOCK		2  /* <li> containing block data */

/************
 * MAIN API *
 ************/

extern struct sd_markdown *
sd_markdown_new(
	unsigned int extensions,
	size_t max_nesting,
	const struct sd_callbacks *callbacks,
	void *opaque);

extern void
sd_markdown_render(struct sd_buf *outbuffer, const uint8_t *document, size_t doc_size, struct sd_markdown *md);

extern void
sd_markdown_free(struct sd_markdown *md);

extern void
sd_version(int *major, int *minor, int *revision);

// ENDREGION: MARKDOWN.H

// REGION: BUFFER.H

typedef enum {
	BUF_OK = 0,
	BUF_ENOMEM = -1,
} buferror_t;

/* struct sd_buf: character array buffer */
struct sd_buf {
	uint8_t *data;		/* actual character data */
	size_t size;	/* size of the string */
	size_t asize;	/* allocated size (0 = volatile buffer) */
	size_t unit;	/* reallocation unit size (0 = read-only buffer) */
};

/* CONST_BUF: global buffer from a string litteral */
#define BUF_STATIC(string) \
	{ (uint8_t *)string, sizeof string -1, sizeof string, 0, 0 }

/* VOLATILE_BUF: macro for creating a volatile buffer on the stack */
#define BUF_VOLATILE(strname) \
	{ (uint8_t *)strname, strlen(strname), 0, 0, 0 }

/* SD_BUFPUTSL: optimized bufputs of a string litteral */
#define SD_BUFPUTSL(output, literal) \
	sd_bufput(output, literal, sizeof literal - 1)

/* sd_bufgrow: increasing the allocated size to the given value */
int sd_bufgrow(struct sd_buf *, size_t);

/* sd_bufnew: allocation of a new buffer */
struct sd_buf *sd_bufnew(size_t) __attribute__ ((malloc));

/* bufnullterm: NUL-termination of the string array (making a C-string) */
const char *sd_bufcstr(struct sd_buf *);

/* sd_bufprefix: compare the beginning of a buffer with a string */
int sd_bufprefix(const struct sd_buf *buf, const char *prefix);

/* sd_bufput: appends raw data to a buffer */
void sd_bufput(struct sd_buf *, const void *, size_t);

/* sd_bufputs: appends a NUL-terminated string to a buffer */
void sd_bufputs(struct sd_buf *, const char *);

/* sd_bufputc: appends a single char to a buffer */
void sd_bufputc(struct sd_buf *, int);

/* sd_bufrelease: decrease the reference count and free the buffer if needed */
void sd_bufrelease(struct sd_buf *);

/* sd_bufreset: frees internal data of the buffer */
void sd_bufreset(struct sd_buf *);

/* sd_bufslurp: removes a given number of bytes from the head of the array */
void sd_bufslurp(struct sd_buf *, size_t);

/* sd_bufprintf: formatted printing to a buffer */
void sd_bufprintf(struct sd_buf *, const char *, ...) __attribute__ ((format (printf, 2, 3)));

// ENDREGION: BUFFER.H

// REGION: AUTOLINK.H

enum {
	SD_AUTOLINK_SHORT_DOMAINS = (1 << 0),
};

int
sd_autolink_issafe(const uint8_t *link, size_t link_len);

size_t
sd_autolink__www(size_t *rewind_p, struct sd_buf *link,
	uint8_t *data, size_t offset, size_t size, unsigned int flags);

size_t
sd_autolink__email(size_t *rewind_p, struct sd_buf *link,
	uint8_t *data, size_t offset, size_t size, unsigned int flags);

size_t
sd_autolink__url(size_t *rewind_p, struct sd_buf *link,
	uint8_t *data, size_t offset, size_t size, unsigned int flags);

// ENDREGION: AUTOLINK.H

// REGION: STACK.H

struct stack {
	void **item;
	size_t size;
	size_t asize;
};

void stack_free(struct stack *);
int stack_grow(struct stack *, size_t);
int stack_init(struct stack *, size_t);

int stack_push(struct stack *, void *);

void *stack_pop(struct stack *);
void *stack_top(struct stack *);

// ENDREGION: STACK.H

// REGION: HTML_BLOCKS.H

/* C code produced by gperf version 3.0.3 */
/* Command-line: gperf -N find_block_tag -H hash_block_tag -C -c -E --ignore-case html_block_names.txt  */
/* Computed positions: -k'1-2' */

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
error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

/* maximum key range = 37, duplicates = 0 */

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
hash_block_tag (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
       8, 30, 25, 20, 15, 10, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38,  0, 38,  0, 38,
       5,  5,  5, 15,  0, 38, 38,  0, 15, 10,
       0, 38, 38, 15,  0,  5, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38,  0, 38,
       0, 38,  5,  5,  5, 15,  0, 38, 38,  0,
      15, 10,  0, 38, 38, 15,  0,  5, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38
    };
  register int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[1]+1];
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

// ENDREGION: HTML_BLOCKS.H

#ifdef SD_IMPLEMENTATION

#ifdef __GNUC__
__inline
#ifdef __GNUC_STDC_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
const char *
find_block_tag (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 24,
      MIN_WORD_LENGTH = 1,
      MAX_WORD_LENGTH = 10,
      MIN_HASH_VALUE = 1,
      MAX_HASH_VALUE = 37
    };

  static const char * const wordlist[] =
    {
      "",
      "p",
      "dl",
      "div",
      "math",
      "table",
      "",
      "ul",
      "del",
      "form",
      "blockquote",
      "figure",
      "ol",
      "fieldset",
      "",
      "h1",
      "",
      "h6",
      "pre",
      "", "",
      "script",
      "h5",
      "noscript",
      "",
      "style",
      "iframe",
      "h4",
      "ins",
      "", "", "",
      "h3",
      "", "", "", "",
      "h2"
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = hash_block_tag (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key];

          if ((((unsigned char)*str ^ (unsigned char)*s) & ~32) == 0 && !gperf_case_strncmp (str, s, len) && s[len] == '\0')
            return s;
        }
    }
  return 0;
}

// REGION: BUFFER.C

#define BUFFER_MAX_ALLOC_SIZE (1024 * 1024 * 16) //16mb

/* MSVC compat */
#if defined(_MSC_VER)
#	define _buf_vsnprintf _vsnprintf
#else
#	define _buf_vsnprintf vsnprintf
#endif

int
sd_bufprefix(const struct sd_buf *buf, const char *prefix)
{
	size_t i;
	assert(buf && buf->unit);

	for (i = 0; i < buf->size; ++i) {
		if (prefix[i] == 0)
			return 0;

		if (buf->data[i] != prefix[i])
			return buf->data[i] - prefix[i];
	}

	return 0;
}

/* sd_bufgrow: increasing the allocated size to the given value */
int
sd_bufgrow(struct sd_buf *buf, size_t neosz)
{
	size_t neoasz;
	void *neodata;

	assert(buf && buf->unit);

	if (neosz > BUFFER_MAX_ALLOC_SIZE)
		return BUF_ENOMEM;

	if (buf->asize >= neosz)
		return BUF_OK;

	neoasz = buf->asize + buf->unit;
	while (neoasz < neosz)
		neoasz += buf->unit;

	neodata = realloc(buf->data, neoasz);
	if (!neodata)
		return BUF_ENOMEM;

	buf->data = neodata;
	buf->asize = neoasz;
	return BUF_OK;
}


/* sd_bufnew: allocation of a new buffer */
struct sd_buf *
sd_bufnew(size_t unit)
{
	struct sd_buf *ret;
	ret = malloc(sizeof (struct sd_buf));

	if (ret) {
		ret->data = 0;
		ret->size = ret->asize = 0;
		ret->unit = unit;
	}
	return ret;
}

/* bufnullterm: NULL-termination of the string array */
const char *
sd_bufcstr(struct sd_buf *buf)
{
	assert(buf && buf->unit);

	if (buf->size < buf->asize && buf->data[buf->size] == 0)
		return (char *)buf->data;

	if (buf->size + 1 <= buf->asize || sd_bufgrow(buf, buf->size + 1) == 0) {
		buf->data[buf->size] = 0;
		return (char *)buf->data;
	}

	return NULL;
}

/* sd_bufprintf: formatted printing to a buffer */
void
sd_bufprintf(struct sd_buf *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	assert(buf && buf->unit);

	if (buf->size >= buf->asize && sd_bufgrow(buf, buf->size + 1) < 0)
		return;
	
	va_start(ap, fmt);
	n = _buf_vsnprintf((char *)buf->data + buf->size, buf->asize - buf->size, fmt, ap);
	va_end(ap);

	if (n < 0) {
#ifdef _MSC_VER
		va_start(ap, fmt);
		n = _vscprintf(fmt, ap);
		va_end(ap);
#else
		return;
#endif
	}

	if ((size_t)n >= buf->asize - buf->size) {
		if (sd_bufgrow(buf, buf->size + n + 1) < 0)
			return;

		va_start(ap, fmt);
		n = _buf_vsnprintf((char *)buf->data + buf->size, buf->asize - buf->size, fmt, ap);
		va_end(ap);
	}

	if (n < 0)
		return;

	buf->size += n;
}

/* sd_bufput: appends raw data to a buffer */
void
sd_bufput(struct sd_buf *buf, const void *data, size_t len)
{
	assert(buf && buf->unit);

	if (buf->size + len > buf->asize && sd_bufgrow(buf, buf->size + len) < 0)
		return;

	memcpy(buf->data + buf->size, data, len);
	buf->size += len;
}

/* sd_bufputs: appends a NUL-terminated string to a buffer */
void
sd_bufputs(struct sd_buf *buf, const char *str)
{
	sd_bufput(buf, str, strlen(str));
}


/* sd_bufputc: appends a single uint8_t to a buffer */
void
sd_bufputc(struct sd_buf *buf, int c)
{
	assert(buf && buf->unit);

	if (buf->size + 1 > buf->asize && sd_bufgrow(buf, buf->size + 1) < 0)
		return;

	buf->data[buf->size] = (uint8_t)c;
	buf->size += 1;
}

/* sd_bufrelease: decrease the reference count and free the buffer if needed */
void
sd_bufrelease(struct sd_buf *buf)
{
	if (!buf)
		return;

	free(buf->data);
	free(buf);
}


/* sd_bufreset: frees internal data of the buffer */
void
sd_bufreset(struct sd_buf *buf)
{
	if (!buf)
		return;

	free(buf->data);
	buf->data = NULL;
	buf->size = buf->asize = 0;
}

/* sd_bufslurp: removes a given number of bytes from the head of the array */
void
sd_bufslurp(struct sd_buf *buf, size_t len)
{
	assert(buf && buf->unit);

	if (len >= buf->size) {
		buf->size = 0;
		return;
	}

	buf->size -= len;
	memmove(buf->data, buf->data + len, buf->size);
}

// ENDREGION: BUFFER.C

// REGION: AUTOLINK.C

int
sd_autolink_issafe(const uint8_t *link, size_t link_len)
{
	static const size_t valid_uris_count = 5;
	static const char *valid_uris[] = {
		"/", "http://", "https://", "ftp://", "mailto:"
	};

	size_t i;

	for (i = 0; i < valid_uris_count; ++i) {
		size_t len = strlen(valid_uris[i]);

		if (link_len > len &&
			strncasecmp((char *)link, valid_uris[i], len) == 0 &&
			isalnum(link[len]))
			return 1;
	}

	return 0;
}

static size_t
autolink_delim(uint8_t *data, size_t link_end, size_t max_rewind, size_t size)
{
	uint8_t cclose, copen = 0;
	size_t i;

	for (i = 0; i < link_end; ++i)
		if (data[i] == '<') {
			link_end = i;
			break;
		}

	while (link_end > 0) {
		if (strchr("?!.,", data[link_end - 1]) != NULL)
			link_end--;

		else if (data[link_end - 1] == ';') {
			size_t new_end = link_end - 2;

			while (new_end > 0 && isalpha(data[new_end]))
				new_end--;

			if (new_end < link_end - 2 && data[new_end] == '&')
				link_end = new_end;
			else
				link_end--;
		}
		else break;
	}

	if (link_end == 0)
		return 0;

	cclose = data[link_end - 1];

	switch (cclose) {
	case '"':	copen = '"'; break;
	case '\'':	copen = '\''; break;
	case ')':	copen = '('; break;
	case ']':	copen = '['; break;
	case '}':	copen = '{'; break;
	}

	if (copen != 0) {
		size_t closing = 0;
		size_t opening = 0;
		size_t j = 0;

		/* Try to close the final punctuation sign in this same line;
		 * if we managed to close it outside of the URL, that means that it's
		 * not part of the URL. If it closes inside the URL, that means it
		 * is part of the URL.
		 *
		 * Examples:
		 *
		 *	foo http://www.pokemon.com/Pikachu_(Electric) bar
		 *		=> http://www.pokemon.com/Pikachu_(Electric)
		 *
		 *	foo (http://www.pokemon.com/Pikachu_(Electric)) bar
		 *		=> http://www.pokemon.com/Pikachu_(Electric)
		 *
		 *	foo http://www.pokemon.com/Pikachu_(Electric)) bar
		 *		=> http://www.pokemon.com/Pikachu_(Electric))
		 *
		 *	(foo http://www.pokemon.com/Pikachu_(Electric)) bar
		 *		=> foo http://www.pokemon.com/Pikachu_(Electric)
		 */

		while (j < link_end) {
			if (data[j] == copen)
				opening++;
			else if (data[j] == cclose)
				closing++;

			j++;
		}

		if (closing != opening)
			link_end--;
	}

	return link_end;
}

static size_t
check_domain(uint8_t *data, size_t size, int allow_short)
{
	size_t i, np = 0;

	if (!isalnum(data[0]))
		return 0;

	for (i = 1; i < size - 1; ++i) {
		if (data[i] == '.') np++;
		else if (!isalnum(data[i]) && data[i] != '-') break;
	}

	if (allow_short) {
		/* We don't need a valid domain in the strict sense (with
		 * least one dot; so just make sure it's composed of valid
		 * domain characters and return the length of the the valid
		 * sequence. */
		return i;
	} else {
		/* a valid domain needs to have at least a dot.
		 * that's as far as we get */
		return np ? i : 0;
	}
}

size_t
sd_autolink__www(
	size_t *rewind_p,
	struct sd_buf *link,
	uint8_t *data,
	size_t max_rewind,
	size_t size,
	unsigned int flags)
{
	size_t link_end;

	if (max_rewind > 0 && !ispunct(data[-1]) && !isspace(data[-1]))
		return 0;

	if (size < 4 || memcmp(data, "www.", strlen("www.")) != 0)
		return 0;

	link_end = check_domain(data, size, 0);

	if (link_end == 0)
		return 0;

	while (link_end < size && !isspace(data[link_end]))
		link_end++;

	link_end = autolink_delim(data, link_end, max_rewind, size);

	if (link_end == 0)
		return 0;

	sd_bufput(link, data, link_end);
	*rewind_p = 0;

	return (int)link_end;
}

size_t
sd_autolink__email(
	size_t *rewind_p,
	struct sd_buf *link,
	uint8_t *data,
	size_t max_rewind,
	size_t size,
	unsigned int flags)
{
	size_t link_end, rewind;
	int nb = 0, np = 0;

	for (rewind = 0; rewind < max_rewind; ++rewind) {
		uint8_t c = data[(-rewind - 1)];

		if (isalnum(c))
			continue;

		if (strchr(".+-_", c) != NULL)
			continue;

		break;
	}

	if (rewind == 0)
		return 0;

	for (link_end = 0; link_end < size; ++link_end) {
		uint8_t c = data[link_end];

		if (isalnum(c))
			continue;

		if (c == '@')
			nb++;
		else if (c == '.' && link_end < size - 1)
			np++;
		else if (c != '-' && c != '_')
			break;
	}

	if (link_end < 2 || nb != 1 || np == 0 ||
		!isalpha(data[link_end - 1]))
		return 0;

	link_end = autolink_delim(data, link_end, max_rewind, size);

	if (link_end == 0)
		return 0;

	sd_bufput(link, data - rewind, link_end + rewind);
	*rewind_p = rewind;

	return link_end;
}

size_t
sd_autolink__url(
	size_t *rewind_p,
	struct sd_buf *link,
	uint8_t *data,
	size_t max_rewind,
	size_t size,
	unsigned int flags)
{
	size_t link_end, rewind = 0, domain_len;

	if (size < 4 || data[1] != '/' || data[2] != '/')
		return 0;

	while (rewind < max_rewind && isalpha(data[-rewind - 1]))
		rewind++;

	if (!sd_autolink_issafe(data - rewind, size + rewind))
		return 0;

	link_end = strlen("://");

	domain_len = check_domain(
		data + link_end,
		size - link_end,
		flags & SD_AUTOLINK_SHORT_DOMAINS);

	if (domain_len == 0)
		return 0;

	link_end += domain_len;
	while (link_end < size && !isspace(data[link_end]))
		link_end++;

	link_end = autolink_delim(data, link_end, max_rewind, size);

	if (link_end == 0)
		return 0;

	sd_bufput(link, data - rewind, link_end + rewind);
	*rewind_p = rewind;

	return link_end;
}

// ENDREGION: AUTOLINK.C

// REGION: MARKDOWN.C

#define REF_TABLE_SIZE 8

#define BUFFER_BLOCK 0
#define BUFFER_SPAN 1

#define MKD_LI_END 8	/* internal list flag */

#define gperf_case_strncmp(s1, s2, n) strncasecmp(s1, s2, n)
#define GPERF_DOWNCASE 1
#define GPERF_CASE_STRNCMP 1

/***************
 * LOCAL TYPES *
 ***************/

/* link_ref: reference to a link */
struct link_ref {
	unsigned int id;

	struct sd_buf *link;
	struct sd_buf *title;

	struct link_ref *next;
};

/* char_trigger: function pointer to render active chars */
/*   returns the number of chars taken care of */
/*   data is the pointer of the beginning of the span */
/*   offset is the number of valid chars before data */
struct sd_markdown;
typedef size_t
(*char_trigger)(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);

static size_t char_emphasis(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_linebreak(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_codespan(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_escape(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_entity(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_langle_tag(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_autolink_url(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_autolink_email(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_autolink_www(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_link(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);
static size_t char_superscript(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size);

enum markdown_char_t {
	MD_CHAR_NONE = 0,
	MD_CHAR_EMPHASIS,
	MD_CHAR_CODESPAN,
	MD_CHAR_LINEBREAK,
	MD_CHAR_LINK,
	MD_CHAR_LANGLE,
	MD_CHAR_ESCAPE,
	MD_CHAR_ENTITITY,
	MD_CHAR_AUTOLINK_URL,
	MD_CHAR_AUTOLINK_EMAIL,
	MD_CHAR_AUTOLINK_WWW,
	MD_CHAR_SUPERSCRIPT,
};

static char_trigger markdown_char_ptrs[] = {
	NULL,
	&char_emphasis,
	&char_codespan,
	&char_linebreak,
	&char_link,
	&char_langle_tag,
	&char_escape,
	&char_entity,
	&char_autolink_url,
	&char_autolink_email,
	&char_autolink_www,
	&char_superscript,
};

/* render • structure containing one particular render */
struct sd_markdown {
	struct sd_callbacks	cb;
	void *opaque;

	struct link_ref *refs[REF_TABLE_SIZE];
	uint8_t active_char[256];
	struct stack work_bufs[2];
	unsigned int ext_flags;
	size_t max_nesting;
	int in_link_body;
};

/***************************
 * HELPER FUNCTIONS *
 ***************************/

static inline struct sd_buf *
rndr_newbuf(struct sd_markdown *rndr, int type)
{
	static const size_t buf_size[2] = {256, 64};
	struct sd_buf *work = NULL;
	struct stack *pool = &rndr->work_bufs[type];

	if (pool->size < pool->asize &&
		pool->item[pool->size] != NULL) {
		work = pool->item[pool->size++];
		work->size = 0;
	} else {
		work = sd_bufnew(buf_size[type]);
		stack_push(pool, work);
	}

	return work;
}

static inline void
rndr_popbuf(struct sd_markdown *rndr, int type)
{
	rndr->work_bufs[type].size--;
}

static void
unscape_text(struct sd_buf *ob, struct sd_buf *src)
{
	size_t i = 0, org;
	while (i < src->size) {
		org = i;
		while (i < src->size && src->data[i] != '\\')
			i++;

		if (i > org)
			sd_bufput(ob, src->data + org, i - org);

		if (i + 1 >= src->size)
			break;

		sd_bufputc(ob, src->data[i + 1]);
		i += 2;
	}
}

static unsigned int
hash_link_ref(const uint8_t *link_ref, size_t length)
{
	size_t i;
	unsigned int hash = 0;

	for (i = 0; i < length; ++i)
		hash = tolower(link_ref[i]) + (hash << 6) + (hash << 16) - hash;

	return hash;
}

static struct link_ref *
add_link_ref(
	struct link_ref **references,
	const uint8_t *name, size_t name_size)
{
	struct link_ref *ref = calloc(1, sizeof(struct link_ref));

	if (!ref)
		return NULL;

	ref->id = hash_link_ref(name, name_size);
	ref->next = references[ref->id % REF_TABLE_SIZE];

	references[ref->id % REF_TABLE_SIZE] = ref;
	return ref;
}

static struct link_ref *
find_link_ref(struct link_ref **references, uint8_t *name, size_t length)
{
	unsigned int hash = hash_link_ref(name, length);
	struct link_ref *ref = NULL;

	ref = references[hash % REF_TABLE_SIZE];

	while (ref != NULL) {
		if (ref->id == hash)
			return ref;

		ref = ref->next;
	}

	return NULL;
}

static void
free_link_refs(struct link_ref **references)
{
	size_t i;

	for (i = 0; i < REF_TABLE_SIZE; ++i) {
		struct link_ref *r = references[i];
		struct link_ref *next;

		while (r) {
			next = r->next;
			sd_bufrelease(r->link);
			sd_bufrelease(r->title);
			free(r);
			r = next;
		}
	}
}

/*
 * Check whether a char is a Markdown space.

 * Right now we only consider spaces the actual
 * space and a newline: tabs and carriage returns
 * are filtered out during the preprocessing phase.
 *
 * If we wanted to actually be UTF-8 compliant, we
 * should instead extract an Unicode codepoint from
 * this character and check for space properties.
 */
static inline int
_isspace(int c)
{
	return c == ' ' || c == '\n';
}

/****************************
 * INLINE PARSING FUNCTIONS *
 ****************************/

/* is_mail_autolink • looks for the address part of a mail autolink and '>' */
/* this is less strict than the original markdown e-mail address matching */
static size_t
is_mail_autolink(uint8_t *data, size_t size)
{
	size_t i = 0, nb = 0;

	/* address is assumed to be: [-@._a-zA-Z0-9]+ with exactly one '@' */
	for (i = 0; i < size; ++i) {
		if (isalnum(data[i]))
			continue;

		switch (data[i]) {
			case '@':
				nb++;

			case '-':
			case '.':
			case '_':
				break;

			case '>':
				return (nb == 1) ? i + 1 : 0;

			default:
				return 0;
		}
	}

	return 0;
}

/* tag_length • returns the length of the given tag, or 0 is it's not valid */
static size_t
tag_length(uint8_t *data, size_t size, enum mkd_autolink *autolink)
{
	size_t i, j;

	/* a valid tag can't be shorter than 3 chars */
	if (size < 3) return 0;

	/* begins with a '<' optionally followed by '/', followed by letter or number */
	if (data[0] != '<') return 0;
	i = (data[1] == '/') ? 2 : 1;

	if (!isalnum(data[i]))
		return 0;

	/* scheme test */
	*autolink = MKDA_NOT_AUTOLINK;

	/* try to find the beginning of an URI */
	while (i < size && (isalnum(data[i]) || data[i] == '.' || data[i] == '+' || data[i] == '-'))
		i++;

	if (i > 1 && data[i] == '@') {
		if ((j = is_mail_autolink(data + i, size - i)) != 0) {
			*autolink = MKDA_EMAIL;
			return i + j;
		}
	}

	if (i > 2 && data[i] == ':') {
		*autolink = MKDA_NORMAL;
		i++;
	}

	/* completing autolink test: no whitespace or ' or " */
	if (i >= size)
		*autolink = MKDA_NOT_AUTOLINK;

	else if (*autolink) {
		j = i;

		while (i < size) {
			if (data[i] == '\\') i += 2;
			else if (data[i] == '>' || data[i] == '\'' ||
					data[i] == '"' || data[i] == ' ' || data[i] == '\n')
					break;
			else i++;
		}

		if (i >= size) return 0;
		if (i > j && data[i] == '>') return i + 1;
		/* one of the forbidden chars has been found */
		*autolink = MKDA_NOT_AUTOLINK;
	}

	/* looking for sometinhg looking like a tag end */
	while (i < size && data[i] != '>') i++;
	if (i >= size) return 0;
	return i + 1;
}

/* parse_inline • parses inline markdown elements */
static void
parse_inline(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t i = 0, end = 0;
	uint8_t action = 0;
	struct sd_buf work = { 0, 0, 0, 0 };

	if (rndr->work_bufs[BUFFER_SPAN].size +
		rndr->work_bufs[BUFFER_BLOCK].size > rndr->max_nesting)
		return;

	while (i < size) {
		/* copying inactive chars into the output */
		while (end < size && (action = rndr->active_char[data[end]]) == 0) {
			end++;
		}

		if (rndr->cb.normal_text) {
			work.data = data + i;
			work.size = end - i;
			rndr->cb.normal_text(ob, &work, rndr->opaque);
		}
		else
			sd_bufput(ob, data + i, end - i);

		if (end >= size) break;
		i = end;

		end = markdown_char_ptrs[(int)action](ob, rndr, data + i, i, size - i);
		if (!end) /* no action from the callback */
			end = i + 1;
		else {
			i += end;
			end = i;
		}
	}
}

/* find_emph_char • looks for the next emph uint8_t, skipping other constructs */
static size_t
find_emph_char(uint8_t *data, size_t size, uint8_t c)
{
	size_t i = 1;

	while (i < size) {
		while (i < size && data[i] != c && data[i] != '`' && data[i] != '[')
			i++;

		if (i == size)
			return 0;

		if (data[i] == c)
			return i;

		/* not counting escaped chars */
		if (i && data[i - 1] == '\\') {
			i++; continue;
		}

		if (data[i] == '`') {
			size_t span_nb = 0, bt;
			size_t tmp_i = 0;

			/* counting the number of opening backticks */
			while (i < size && data[i] == '`') {
				i++; span_nb++;
			}

			if (i >= size) return 0;

			/* finding the matching closing sequence */
			bt = 0;
			while (i < size && bt < span_nb) {
				if (!tmp_i && data[i] == c) tmp_i = i;
				if (data[i] == '`') bt++;
				else bt = 0;
				i++;
			}

			if (i >= size) return tmp_i;
		}
		/* skipping a link */
		else if (data[i] == '[') {
			size_t tmp_i = 0;
			uint8_t cc;

			i++;
			while (i < size && data[i] != ']') {
				if (!tmp_i && data[i] == c) tmp_i = i;
				i++;
			}

			i++;
			while (i < size && (data[i] == ' ' || data[i] == '\n'))
				i++;

			if (i >= size)
				return tmp_i;

			switch (data[i]) {
			case '[':
				cc = ']'; break;

			case '(':
				cc = ')'; break;

			default:
				if (tmp_i)
					return tmp_i;
				else
					continue;
			}

			i++;
			while (i < size && data[i] != cc) {
				if (!tmp_i && data[i] == c) tmp_i = i;
				i++;
			}

			if (i >= size)
				return tmp_i;

			i++;
		}
	}

	return 0;
}

/* parse_emph1 • parsing single emphase */
/* closed by a symbol not preceded by whitespace and not followed by symbol */
static size_t
parse_emph1(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, uint8_t c)
{
	size_t i = 0, len;
	struct sd_buf *work = 0;
	int r;

	if (!rndr->cb.emphasis) return 0;

	/* skipping one symbol if coming from emph3 */
	if (size > 1 && data[0] == c && data[1] == c) i = 1;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (!len) return 0;
		i += len;
		if (i >= size) return 0;

		if (data[i] == c && !_isspace(data[i - 1])) {

			if (rndr->ext_flags & MKDEXT_NO_INTRA_EMPHASIS) {
				if (i + 1 < size && isalnum(data[i + 1]))
					continue;
			}

			work = rndr_newbuf(rndr, BUFFER_SPAN);
			parse_inline(work, rndr, data, i);
			r = rndr->cb.emphasis(ob, work, rndr->opaque);
			rndr_popbuf(rndr, BUFFER_SPAN);
			return r ? i + 1 : 0;
		}
	}

	return 0;
}

/* parse_emph2 • parsing single emphase */
static size_t
parse_emph2(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, uint8_t c)
{
	int (*render_method)(struct sd_buf *ob, const struct sd_buf *text, void *opaque);
	size_t i = 0, len;
	struct sd_buf *work = 0;
	int r;

	render_method = (c == '~') ? rndr->cb.strikethrough : rndr->cb.double_emphasis;

	if (!render_method)
		return 0;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (!len) return 0;
		i += len;

		if (i + 1 < size && data[i] == c && data[i + 1] == c && i && !_isspace(data[i - 1])) {
			work = rndr_newbuf(rndr, BUFFER_SPAN);
			parse_inline(work, rndr, data, i);
			r = render_method(ob, work, rndr->opaque);
			rndr_popbuf(rndr, BUFFER_SPAN);
			return r ? i + 2 : 0;
		}
		i++;
	}
	return 0;
}

/* parse_emph3 • parsing single emphase */
/* finds the first closing tag, and delegates to the other emph */
static size_t
parse_emph3(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, uint8_t c)
{
	size_t i = 0, len;
	int r;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (!len) return 0;
		i += len;

		/* skip whitespace preceded symbols */
		if (data[i] != c || _isspace(data[i - 1]))
			continue;

		if (i + 2 < size && data[i + 1] == c && data[i + 2] == c && rndr->cb.triple_emphasis) {
			/* triple symbol found */
			struct sd_buf *work = rndr_newbuf(rndr, BUFFER_SPAN);

			parse_inline(work, rndr, data, i);
			r = rndr->cb.triple_emphasis(ob, work, rndr->opaque);
			rndr_popbuf(rndr, BUFFER_SPAN);
			return r ? i + 3 : 0;

		} else if (i + 1 < size && data[i + 1] == c) {
			/* double symbol found, handing over to emph1 */
			len = parse_emph1(ob, rndr, data - 2, size + 2, c);
			if (!len) return 0;
			else return len - 2;

		} else {
			/* single symbol found, handing over to emph2 */
			len = parse_emph2(ob, rndr, data - 1, size + 1, c);
			if (!len) return 0;
			else return len - 1;
		}
	}
	return 0;
}

/* char_emphasis • single and double emphasis parsing */
static size_t
char_emphasis(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	uint8_t c = data[0];
	size_t ret;

	if (rndr->ext_flags & MKDEXT_NO_INTRA_EMPHASIS) {
		if (offset > 0 && !_isspace(data[-1]) && data[-1] != '>')
			return 0;
	}

	if (size > 2 && data[1] != c) {
		/* whitespace cannot follow an opening emphasis;
		 * strikethrough only takes two characters '~~' */
		if (c == '~' || _isspace(data[1]) || (ret = parse_emph1(ob, rndr, data + 1, size - 1, c)) == 0)
			return 0;

		return ret + 1;
	}

	if (size > 3 && data[1] == c && data[2] != c) {
		if (_isspace(data[2]) || (ret = parse_emph2(ob, rndr, data + 2, size - 2, c)) == 0)
			return 0;

		return ret + 2;
	}

	if (size > 4 && data[1] == c && data[2] == c && data[3] != c) {
		if (c == '~' || _isspace(data[3]) || (ret = parse_emph3(ob, rndr, data + 3, size - 3, c)) == 0)
			return 0;

		return ret + 3;
	}

	return 0;
}


/* char_linebreak • '\n' preceded by two spaces (assuming linebreak != 0) */
static size_t
char_linebreak(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	if (offset < 2 || data[-1] != ' ' || data[-2] != ' ')
		return 0;

	/* removing the last space from ob and rendering */
	while (ob->size && ob->data[ob->size - 1] == ' ')
		ob->size--;

	return rndr->cb.linebreak(ob, rndr->opaque) ? 1 : 0;
}


/* char_codespan • '`' parsing a code span (assuming codespan != 0) */
static size_t
char_codespan(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	size_t end, nb = 0, i, f_begin, f_end;

	/* counting the number of backticks in the delimiter */
	while (nb < size && data[nb] == '`')
		nb++;

	/* finding the next delimiter */
	i = 0;
	for (end = nb; end < size && i < nb; end++) {
		if (data[end] == '`') i++;
		else i = 0;
	}

	if (i < nb && end >= size)
		return 0; /* no matching delimiter */

	/* trimming outside whitespaces */
	f_begin = nb;
	while (f_begin < end && data[f_begin] == ' ')
		f_begin++;

	f_end = end - nb;
	while (f_end > nb && data[f_end-1] == ' ')
		f_end--;

	/* real code span */
	if (f_begin < f_end) {
		struct sd_buf work = {0};
        work.data = data + f_begin;
        work.size = f_end - f_begin;
		if (!rndr->cb.codespan(ob, &work, rndr->opaque))
			end = 0;
	} else {
		if (!rndr->cb.codespan(ob, 0, rndr->opaque))
			end = 0;
	}

	return end;
}


/* char_escape • '\\' backslash escape */
static size_t
char_escape(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	static const char *escape_chars = "\\`*_{}[]()#+-.!:|&<>^~";
	struct sd_buf work = { 0, 0, 0, 0 };

	if (size > 1) {
		if (strchr(escape_chars, data[1]) == NULL)
			return 0;

		if (rndr->cb.normal_text) {
			work.data = data + 1;
			work.size = 1;
			rndr->cb.normal_text(ob, &work, rndr->opaque);
		}
		else sd_bufputc(ob, data[1]);
	} else if (size == 1) {
		sd_bufputc(ob, data[0]);
	}

	return 2;
}

/* char_entity • '&' escaped when it doesn't belong to an entity */
/* valid entities are assumed to be anything matching &#?[A-Za-z0-9]+; */
static size_t
char_entity(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	size_t end = 1;
	struct sd_buf work = { 0, 0, 0, 0 };

	if (end < size && data[end] == '#')
		end++;

	while (end < size && isalnum(data[end]))
		end++;

	if (end < size && data[end] == ';')
		end++; /* real entity */
	else
		return 0; /* lone '&' */

	if (rndr->cb.entity) {
		work.data = data;
		work.size = end;
		rndr->cb.entity(ob, &work, rndr->opaque);
	}
	else sd_bufput(ob, data, end);

	return end;
}

/* char_langle_tag • '<' when tags or autolinks are allowed */
static size_t
char_langle_tag(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	enum mkd_autolink altype = MKDA_NOT_AUTOLINK;
	size_t end = tag_length(data, size, &altype);
	struct sd_buf work = {0};
    work.data = data;
    work.size = end;
	int ret = 0;

	if (end > 2) {
		if (rndr->cb.autolink && altype != MKDA_NOT_AUTOLINK) {
			struct sd_buf *u_link = rndr_newbuf(rndr, BUFFER_SPAN);
			work.data = data + 1;
			work.size = end - 2;
			unscape_text(u_link, &work);
			ret = rndr->cb.autolink(ob, u_link, altype, rndr->opaque);
			rndr_popbuf(rndr, BUFFER_SPAN);
		}
		else if (rndr->cb.raw_html_tag)
			ret = rndr->cb.raw_html_tag(ob, &work, rndr->opaque);
	}

	if (!ret) return 0;
	else return end;
}

static size_t
char_autolink_www(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	struct sd_buf *link, *link_url, *link_text;
	size_t link_len, rewind;

	if (!rndr->cb.link || rndr->in_link_body)
		return 0;

	link = rndr_newbuf(rndr, BUFFER_SPAN);

	if ((link_len = sd_autolink__www(&rewind, link, data, offset, size, 0)) > 0) {
		link_url = rndr_newbuf(rndr, BUFFER_SPAN);
		SD_BUFPUTSL(link_url, "http://");
		sd_bufput(link_url, link->data, link->size);

		ob->size -= rewind;
		if (rndr->cb.normal_text) {
			link_text = rndr_newbuf(rndr, BUFFER_SPAN);
			rndr->cb.normal_text(link_text, link, rndr->opaque);
			rndr->cb.link(ob, link_url, NULL, link_text, rndr->opaque);
			rndr_popbuf(rndr, BUFFER_SPAN);
		} else {
			rndr->cb.link(ob, link_url, NULL, link, rndr->opaque);
		}
		rndr_popbuf(rndr, BUFFER_SPAN);
	}

	rndr_popbuf(rndr, BUFFER_SPAN);
	return link_len;
}

static size_t
char_autolink_email(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	struct sd_buf *link;
	size_t link_len, rewind;

	if (!rndr->cb.autolink || rndr->in_link_body)
		return 0;

	link = rndr_newbuf(rndr, BUFFER_SPAN);

	if ((link_len = sd_autolink__email(&rewind, link, data, offset, size, 0)) > 0) {
		ob->size -= rewind;
		rndr->cb.autolink(ob, link, MKDA_EMAIL, rndr->opaque);
	}

	rndr_popbuf(rndr, BUFFER_SPAN);
	return link_len;
}

static size_t
char_autolink_url(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	struct sd_buf *link;
	size_t link_len, rewind;

	if (!rndr->cb.autolink || rndr->in_link_body)
		return 0;

	link = rndr_newbuf(rndr, BUFFER_SPAN);

	if ((link_len = sd_autolink__url(&rewind, link, data, offset, size, 0)) > 0) {
		ob->size -= rewind;
		rndr->cb.autolink(ob, link, MKDA_NORMAL, rndr->opaque);
	}

	rndr_popbuf(rndr, BUFFER_SPAN);
	return link_len;
}

/* char_link • '[': parsing a link or an image */
static size_t
char_link(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	int is_img = (offset && data[-1] == '!'), level;
	size_t i = 1, txt_e, link_b = 0, link_e = 0, title_b = 0, title_e = 0;
	struct sd_buf *content = 0;
	struct sd_buf *link = 0;
	struct sd_buf *title = 0;
	struct sd_buf *u_link = 0;
	size_t org_work_size = rndr->work_bufs[BUFFER_SPAN].size;
	int text_has_nl = 0, ret = 0;
	int in_title = 0, qtype = 0;

	/* checking whether the correct renderer exists */
	if ((is_img && !rndr->cb.image) || (!is_img && !rndr->cb.link))
		goto cleanup;

	/* looking for the matching closing bracket */
	for (level = 1; i < size; i++) {
		if (data[i] == '\n')
			text_has_nl = 1;

		else if (data[i - 1] == '\\')
			continue;

		else if (data[i] == '[')
			level++;

		else if (data[i] == ']') {
			level--;
			if (level <= 0)
				break;
		}
	}

	if (i >= size)
		goto cleanup;

	txt_e = i;
	i++;

	/* skip any amount of whitespace or newline */
	/* (this is much more laxist than original markdown syntax) */
	while (i < size && _isspace(data[i]))
		i++;

	/* inline style link */
	if (i < size && data[i] == '(') {
		/* skipping initial whitespace */
		i++;

		while (i < size && _isspace(data[i]))
			i++;

		link_b = i;

		/* looking for link end: ' " ) */
		while (i < size) {
			if (data[i] == '\\') i += 2;
			else if (data[i] == ')') break;
			else if (i >= 1 && _isspace(data[i-1]) && (data[i] == '\'' || data[i] == '"')) break;
			else i++;
		}

		if (i >= size) goto cleanup;
		link_e = i;

		/* looking for title end if present */
		if (data[i] == '\'' || data[i] == '"') {
			qtype = data[i];
			in_title = 1;
			i++;
			title_b = i;

			while (i < size) {
				if (data[i] == '\\') i += 2;
				else if (data[i] == qtype) {in_title = 0; i++;}
				else if ((data[i] == ')') && !in_title) break;
				else i++;
			}

			if (i >= size) goto cleanup;

			/* skipping whitespaces after title */
			title_e = i - 1;
			while (title_e > title_b && _isspace(data[title_e]))
				title_e--;

			/* checking for closing quote presence */
			if (data[title_e] != '\'' &&  data[title_e] != '"') {
				title_b = title_e = 0;
				link_e = i;
			}
		}

		/* remove whitespace at the end of the link */
		while (link_e > link_b && _isspace(data[link_e - 1]))
			link_e--;

		/* remove optional angle brackets around the link */
		if (data[link_b] == '<') link_b++;
		if (data[link_e - 1] == '>') link_e--;

		/* building escaped link and title */
		if (link_e > link_b) {
			link = rndr_newbuf(rndr, BUFFER_SPAN);
			sd_bufput(link, data + link_b, link_e - link_b);
		}

		if (title_e > title_b) {
			title = rndr_newbuf(rndr, BUFFER_SPAN);
			sd_bufput(title, data + title_b, title_e - title_b);
		}

		i++;
	}

	/* reference style link */
	else if (i < size && data[i] == '[') {
		struct sd_buf id = { 0, 0, 0, 0 };
		struct link_ref *lr;

		/* looking for the id */
		i++;
		link_b = i;
		while (i < size && data[i] != ']') i++;
		if (i >= size) goto cleanup;
		link_e = i;

		/* finding the link_ref */
		if (link_b == link_e) {
			if (text_has_nl) {
				struct sd_buf *b = rndr_newbuf(rndr, BUFFER_SPAN);
				size_t j;

				for (j = 1; j < txt_e; j++) {
					if (data[j] != '\n')
						sd_bufputc(b, data[j]);
					else if (data[j - 1] != ' ')
						sd_bufputc(b, ' ');
				}

				id.data = b->data;
				id.size = b->size;
			} else {
				id.data = data + 1;
				id.size = txt_e - 1;
			}
		} else {
			id.data = data + link_b;
			id.size = link_e - link_b;
		}

		lr = find_link_ref(rndr->refs, id.data, id.size);
		if (!lr)
			goto cleanup;

		/* keeping link and title from link_ref */
		link = lr->link;
		title = lr->title;
		i++;
	}

	/* shortcut reference style link */
	else {
		struct sd_buf id = { 0, 0, 0, 0 };
		struct link_ref *lr;

		/* crafting the id */
		if (text_has_nl) {
			struct sd_buf *b = rndr_newbuf(rndr, BUFFER_SPAN);
			size_t j;

			for (j = 1; j < txt_e; j++) {
				if (data[j] != '\n')
					sd_bufputc(b, data[j]);
				else if (data[j - 1] != ' ')
					sd_bufputc(b, ' ');
			}

			id.data = b->data;
			id.size = b->size;
		} else {
			id.data = data + 1;
			id.size = txt_e - 1;
		}

		/* finding the link_ref */
		lr = find_link_ref(rndr->refs, id.data, id.size);
		if (!lr)
			goto cleanup;

		/* keeping link and title from link_ref */
		link = lr->link;
		title = lr->title;

		/* rewinding the whitespace */
		i = txt_e + 1;
	}

	/* building content: img alt is escaped, link content is parsed */
	if (txt_e > 1) {
		content = rndr_newbuf(rndr, BUFFER_SPAN);
		if (is_img) {
			sd_bufput(content, data + 1, txt_e - 1);
		} else {
			/* disable autolinking when parsing inline the
			 * content of a link */
			rndr->in_link_body = 1;
			parse_inline(content, rndr, data + 1, txt_e - 1);
			rndr->in_link_body = 0;
		}
	}

	if (link) {
		u_link = rndr_newbuf(rndr, BUFFER_SPAN);
		unscape_text(u_link, link);
	}

	/* calling the relevant rendering function */
	if (is_img) {
		if (ob->size && ob->data[ob->size - 1] == '!')
			ob->size -= 1;

		ret = rndr->cb.image(ob, u_link, title, content, rndr->opaque);
	} else {
		ret = rndr->cb.link(ob, u_link, title, content, rndr->opaque);
	}

	/* cleanup */
cleanup:
	rndr->work_bufs[BUFFER_SPAN].size = (int)org_work_size;
	return ret ? i : 0;
}

static size_t
char_superscript(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t offset, size_t size)
{
	size_t sup_start, sup_len;
	struct sd_buf *sup;

	if (!rndr->cb.superscript)
		return 0;

	if (size < 2)
		return 0;

	if (data[1] == '(') {
		sup_start = sup_len = 2;

		while (sup_len < size && data[sup_len] != ')' && data[sup_len - 1] != '\\')
			sup_len++;

		if (sup_len == size)
			return 0;
	} else {
		sup_start = sup_len = 1;

		while (sup_len < size && !_isspace(data[sup_len]))
			sup_len++;
	}

	if (sup_len - sup_start == 0)
		return (sup_start == 2) ? 3 : 0;

	sup = rndr_newbuf(rndr, BUFFER_SPAN);
	parse_inline(sup, rndr, data + sup_start, sup_len - sup_start);
	rndr->cb.superscript(ob, sup, rndr->opaque);
	rndr_popbuf(rndr, BUFFER_SPAN);

	return (sup_start == 2) ? sup_len + 1 : sup_len;
}

/*********************************
 * BLOCK-LEVEL PARSING FUNCTIONS *
 *********************************/

/* is_empty • returns the line length when it is empty, 0 otherwise */
static size_t
is_empty(uint8_t *data, size_t size)
{
	size_t i;

	for (i = 0; i < size && data[i] != '\n'; i++)
		if (data[i] != ' ')
			return 0;

	return i + 1;
}

/* is_hrule • returns whether a line is a horizontal rule */
static int
is_hrule(uint8_t *data, size_t size)
{
	size_t i = 0, n = 0;
	uint8_t c;

	/* skipping initial spaces */
	if (size < 3) return 0;
	if (data[0] == ' ') { i++;
	if (data[1] == ' ') { i++;
	if (data[2] == ' ') { i++; } } }

	/* looking at the hrule uint8_t */
	if (i + 2 >= size
	|| (data[i] != '*' && data[i] != '-' && data[i] != '_'))
		return 0;
	c = data[i];

	/* the whole line must be the char or whitespace */
	while (i < size && data[i] != '\n') {
		if (data[i] == c) n++;
		else if (data[i] != ' ')
			return 0;

		i++;
	}

	return n >= 3;
}

/* check if a line begins with a code fence; return the
 * width of the code fence */
static size_t
prefix_codefence(uint8_t *data, size_t size)
{
	size_t i = 0, n = 0;
	uint8_t c;

	/* skipping initial spaces */
	if (size < 3) return 0;
	if (data[0] == ' ') { i++;
	if (data[1] == ' ') { i++;
	if (data[2] == ' ') { i++; } } }

	/* looking at the hrule uint8_t */
	if (i + 2 >= size || !(data[i] == '~' || data[i] == '`'))
		return 0;

	c = data[i];

	/* the whole line must be the uint8_t or whitespace */
	while (i < size && data[i] == c) {
		n++; i++;
	}

	if (n < 3)
		return 0;

	return i;
}

/* check if a line is a code fence; return its size if it is */
static size_t
is_codefence(uint8_t *data, size_t size, struct sd_buf *syntax)
{
	size_t i = 0, syn_len = 0;
	uint8_t *syn_start;

	i = prefix_codefence(data, size);
	if (i == 0)
		return 0;

	while (i < size && data[i] == ' ')
		i++;

	syn_start = data + i;

	if (i < size && data[i] == '{') {
		i++; syn_start++;

		while (i < size && data[i] != '}' && data[i] != '\n') {
			syn_len++; i++;
		}

		if (i == size || data[i] != '}')
			return 0;

		/* strip all whitespace at the beginning and the end
		 * of the {} block */
		while (syn_len > 0 && _isspace(syn_start[0])) {
			syn_start++; syn_len--;
		}

		while (syn_len > 0 && _isspace(syn_start[syn_len - 1]))
			syn_len--;

		i++;
	} else {
		while (i < size && !_isspace(data[i])) {
			syn_len++; i++;
		}
	}

	if (syntax) {
		syntax->data = syn_start;
		syntax->size = syn_len;
	}

	while (i < size && data[i] != '\n') {
		if (!_isspace(data[i]))
			return 0;

		i++;
	}

	return i + 1;
}

/* is_atxheader • returns whether the line is a hash-prefixed header */
static int
is_atxheader(struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	if (data[0] != '#')
		return 0;

	if (rndr->ext_flags & MKDEXT_SPACE_HEADERS) {
		size_t level = 0;

		while (level < size && level < 6 && data[level] == '#')
			level++;

		if (level < size && data[level] != ' ')
			return 0;
	}

	return 1;
}

/* is_headerline • returns whether the line is a setext-style hdr underline */
static int
is_headerline(uint8_t *data, size_t size)
{
	size_t i = 0;

	/* test of level 1 header */
	if (data[i] == '=') {
		for (i = 1; i < size && data[i] == '='; i++);
		while (i < size && data[i] == ' ') i++;
		return (i >= size || data[i] == '\n') ? 1 : 0; }

	/* test of level 2 header */
	if (data[i] == '-') {
		for (i = 1; i < size && data[i] == '-'; i++);
		while (i < size && data[i] == ' ') i++;
		return (i >= size || data[i] == '\n') ? 2 : 0; }

	return 0;
}

static int
is_next_headerline(uint8_t *data, size_t size)
{
	size_t i = 0;

	while (i < size && data[i] != '\n')
		i++;

	if (++i >= size)
		return 0;

	return is_headerline(data + i, size - i);
}

/* prefix_quote • returns blockquote prefix length */
static size_t
prefix_quote(uint8_t *data, size_t size)
{
	size_t i = 0;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i < size && data[i] == '>') {
		if (i + 1 < size && data[i + 1] == ' ')
			return i + 2;

		return i + 1;
	}

	return 0;
}

/* prefix_code • returns prefix length for block code*/
static size_t
prefix_code(uint8_t *data, size_t size)
{
	if (size > 3 && data[0] == ' ' && data[1] == ' '
		&& data[2] == ' ' && data[3] == ' ') return 4;

	return 0;
}

/* prefix_oli • returns ordered list item prefix */
static size_t
prefix_oli(uint8_t *data, size_t size)
{
	size_t i = 0;

	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i >= size || data[i] < '0' || data[i] > '9')
		return 0;

	while (i < size && data[i] >= '0' && data[i] <= '9')
		i++;

	if (i + 1 >= size || data[i] != '.' || data[i + 1] != ' ')
		return 0;

	if (is_next_headerline(data + i, size - i))
		return 0;

	return i + 2;
}

/* prefix_uli • returns ordered list item prefix */
static size_t
prefix_uli(uint8_t *data, size_t size)
{
	size_t i = 0;

	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i + 1 >= size ||
		(data[i] != '*' && data[i] != '+' && data[i] != '-') ||
		data[i + 1] != ' ')
		return 0;

	if (is_next_headerline(data + i, size - i))
		return 0;

	return i + 2;
}


/* parse_block • parsing of one block, returning next uint8_t to parse */
static void parse_block(struct sd_buf *ob, struct sd_markdown *rndr,
			uint8_t *data, size_t size);


/* parse_blockquote • handles parsing of a blockquote fragment */
static size_t
parse_blockquote(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t beg, end = 0, pre, work_size = 0;
	uint8_t *work_data = 0;
	struct sd_buf *out = 0;

	out = rndr_newbuf(rndr, BUFFER_BLOCK);
	beg = 0;
	while (beg < size) {
		for (end = beg + 1; end < size && data[end - 1] != '\n'; end++);

		pre = prefix_quote(data + beg, end - beg);

		if (pre)
			beg += pre; /* skipping prefix */

		/* empty line followed by non-quote line */
		else if (is_empty(data + beg, end - beg) &&
				(end >= size || (prefix_quote(data + end, size - end) == 0 &&
				!is_empty(data + end, size - end))))
			break;

		if (beg < end) { /* copy into the in-place working buffer */
			/* sd_bufput(work, data + beg, end - beg); */
			if (!work_data)
				work_data = data + beg;
			else if (data + beg != work_data + work_size)
				memmove(work_data + work_size, data + beg, end - beg);
			work_size += end - beg;
		}
		beg = end;
	}

	parse_block(out, rndr, work_data, work_size);
	if (rndr->cb.blockquote)
		rndr->cb.blockquote(ob, out, rndr->opaque);
	rndr_popbuf(rndr, BUFFER_BLOCK);
	return end;
}

static size_t
parse_htmlblock(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, int do_render);

/* parse_blockquote • handles parsing of a regular paragraph */
static size_t
parse_paragraph(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t i = 0, end = 0;
	int level = 0;
	struct sd_buf work = {0};
    work.data = data;

	while (i < size) {
		for (end = i + 1; end < size && data[end - 1] != '\n'; end++) /* empty */;

		if (is_empty(data + i, size - i))
			break;

		if ((level = is_headerline(data + i, size - i)) != 0)
			break;

		if (is_atxheader(rndr, data + i, size - i) ||
			is_hrule(data + i, size - i) ||
			prefix_quote(data + i, size - i)) {
			end = i;
			break;
		}

		/*
		 * Early termination of a paragraph with the same logic
		 * as Markdown 1.0.0. If this logic is applied, the
		 * Markdown 1.0.3 test suite won't pass cleanly
		 *
		 * :: If the first character in a new line is not a letter,
		 * let's check to see if there's some kind of block starting
		 * here
		 */
		if ((rndr->ext_flags & MKDEXT_LAX_SPACING) && !isalnum(data[i])) {
			if (prefix_oli(data + i, size - i) ||
				prefix_uli(data + i, size - i)) {
				end = i;
				break;
			}

			/* see if an html block starts here */
			if (data[i] == '<' && rndr->cb.blockhtml &&
				parse_htmlblock(ob, rndr, data + i, size - i, 0)) {
				end = i;
				break;
			}

			/* see if a code fence starts here */
			if ((rndr->ext_flags & MKDEXT_FENCED_CODE) != 0 &&
				is_codefence(data + i, size - i, NULL) != 0) {
				end = i;
				break;
			}
		}

		i = end;
	}

	work.size = i;
	while (work.size && data[work.size - 1] == '\n')
		work.size--;

	if (!level) {
		struct sd_buf *tmp = rndr_newbuf(rndr, BUFFER_BLOCK);
		parse_inline(tmp, rndr, work.data, work.size);
		if (rndr->cb.paragraph)
			rndr->cb.paragraph(ob, tmp, rndr->opaque);
		rndr_popbuf(rndr, BUFFER_BLOCK);
	} else {
		struct sd_buf *header_work;

		if (work.size) {
			size_t beg;
			i = work.size;
			work.size -= 1;

			while (work.size && data[work.size] != '\n')
				work.size -= 1;

			beg = work.size + 1;
			while (work.size && data[work.size - 1] == '\n')
				work.size -= 1;

			if (work.size > 0) {
				struct sd_buf *tmp = rndr_newbuf(rndr, BUFFER_BLOCK);
				parse_inline(tmp, rndr, work.data, work.size);

				if (rndr->cb.paragraph)
					rndr->cb.paragraph(ob, tmp, rndr->opaque);

				rndr_popbuf(rndr, BUFFER_BLOCK);
				work.data += beg;
				work.size = i - beg;
			}
			else work.size = i;
		}

		header_work = rndr_newbuf(rndr, BUFFER_SPAN);
		parse_inline(header_work, rndr, work.data, work.size);

		if (rndr->cb.header)
			rndr->cb.header(ob, header_work, (int)level, rndr->opaque);

		rndr_popbuf(rndr, BUFFER_SPAN);
	}

	return end;
}

/* parse_fencedcode • handles parsing of a block-level code fragment */
static size_t
parse_fencedcode(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t beg, end;
	struct sd_buf *work = 0;
	struct sd_buf lang = { 0, 0, 0, 0 };

	beg = is_codefence(data, size, &lang);
	if (beg == 0) return 0;

	work = rndr_newbuf(rndr, BUFFER_BLOCK);

	while (beg < size) {
		size_t fence_end;
		struct sd_buf fence_trail = { 0, 0, 0, 0 };

		fence_end = is_codefence(data + beg, size - beg, &fence_trail);
		if (fence_end != 0 && fence_trail.size == 0) {
			beg += fence_end;
			break;
		}

		for (end = beg + 1; end < size && data[end - 1] != '\n'; end++);

		if (beg < end) {
			/* verbatim copy to the working buffer,
				escaping entities */
			if (is_empty(data + beg, end - beg))
				sd_bufputc(work, '\n');
			else sd_bufput(work, data + beg, end - beg);
		}
		beg = end;
	}

	if (work->size && work->data[work->size - 1] != '\n')
		sd_bufputc(work, '\n');

	if (rndr->cb.blockcode)
		rndr->cb.blockcode(ob, work, lang.size ? &lang : NULL, rndr->opaque);

	rndr_popbuf(rndr, BUFFER_BLOCK);
	return beg;
}

static size_t
parse_blockcode(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t beg, end, pre;
	struct sd_buf *work = 0;

	work = rndr_newbuf(rndr, BUFFER_BLOCK);

	beg = 0;
	while (beg < size) {
		for (end = beg + 1; end < size && data[end - 1] != '\n'; end++) {};
		pre = prefix_code(data + beg, end - beg);

		if (pre)
			beg += pre; /* skipping prefix */
		else if (!is_empty(data + beg, end - beg))
			/* non-empty non-prefixed line breaks the pre */
			break;

		if (beg < end) {
			/* verbatim copy to the working buffer,
				escaping entities */
			if (is_empty(data + beg, end - beg))
				sd_bufputc(work, '\n');
			else sd_bufput(work, data + beg, end - beg);
		}
		beg = end;
	}

	while (work->size && work->data[work->size - 1] == '\n')
		work->size -= 1;

	sd_bufputc(work, '\n');

	if (rndr->cb.blockcode)
		rndr->cb.blockcode(ob, work, NULL, rndr->opaque);

	rndr_popbuf(rndr, BUFFER_BLOCK);
	return beg;
}

/* parse_listitem • parsing of a single list item */
/*	assuming initial prefix is already removed */
static size_t
parse_listitem(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, int *flags)
{
	struct sd_buf *work = 0, *inter = 0;
	size_t beg = 0, end, pre, sublist = 0, orgpre = 0, i;
	int in_empty = 0, has_inside_empty = 0, in_fence = 0;

	/* keeping track of the first indentation prefix */
	while (orgpre < 3 && orgpre < size && data[orgpre] == ' ')
		orgpre++;

	beg = prefix_uli(data, size);
	if (!beg)
		beg = prefix_oli(data, size);

	if (!beg)
		return 0;

	/* skipping to the beginning of the following line */
	end = beg;
	while (end < size && data[end - 1] != '\n')
		end++;

	/* getting working buffers */
	work = rndr_newbuf(rndr, BUFFER_SPAN);
	inter = rndr_newbuf(rndr, BUFFER_SPAN);

	/* putting the first line into the working buffer */
	sd_bufput(work, data + beg, end - beg);
	beg = end;

	/* process the following lines */
	while (beg < size) {
		size_t has_next_uli = 0, has_next_oli = 0;

		end++;

		while (end < size && data[end - 1] != '\n')
			end++;

		/* process an empty line */
		if (is_empty(data + beg, end - beg)) {
			in_empty = 1;
			beg = end;
			continue;
		}

		/* calculating the indentation */
		i = 0;
		while (i < 4 && beg + i < end && data[beg + i] == ' ')
			i++;

		pre = i;

		if (rndr->ext_flags & MKDEXT_FENCED_CODE) {
			if (is_codefence(data + beg + i, end - beg - i, NULL) != 0)
				in_fence = !in_fence;
		}

		/* Only check for new list items if we are **not** inside
		 * a fenced code block */
		if (!in_fence) {
			has_next_uli = prefix_uli(data + beg + i, end - beg - i);
			has_next_oli = prefix_oli(data + beg + i, end - beg - i);
		}

		/* checking for ul/ol switch */
		if (in_empty && (
			((*flags & MKD_LIST_ORDERED) && has_next_uli) ||
			(!(*flags & MKD_LIST_ORDERED) && has_next_oli))){
			*flags |= MKD_LI_END;
			break; /* the following item must have same list type */
		}

		/* checking for a new item */
		if ((has_next_uli && !is_hrule(data + beg + i, end - beg - i)) || has_next_oli) {
			if (in_empty)
				has_inside_empty = 1;

			if (pre == orgpre) /* the following item must have */
				break;             /* the same indentation */

			if (!sublist)
				sublist = work->size;
		}
		/* joining only indented stuff after empty lines;
		 * note that now we only require 1 space of indentation
		 * to continue a list */
		else if (in_empty && pre == 0) {
			*flags |= MKD_LI_END;
			break;
		}
		else if (in_empty) {
			sd_bufputc(work, '\n');
			has_inside_empty = 1;
		}

		in_empty = 0;

		/* adding the line without prefix into the working buffer */
		sd_bufput(work, data + beg + i, end - beg - i);
		beg = end;
	}

	/* render of li contents */
	if (has_inside_empty)
		*flags |= MKD_LI_BLOCK;

	if (*flags & MKD_LI_BLOCK) {
		/* intermediate render of block li */
		if (sublist && sublist < work->size) {
			parse_block(inter, rndr, work->data, sublist);
			parse_block(inter, rndr, work->data + sublist, work->size - sublist);
		}
		else
			parse_block(inter, rndr, work->data, work->size);
	} else {
		/* intermediate render of inline li */
		if (sublist && sublist < work->size) {
			parse_inline(inter, rndr, work->data, sublist);
			parse_block(inter, rndr, work->data + sublist, work->size - sublist);
		}
		else
			parse_inline(inter, rndr, work->data, work->size);
	}

	/* render of li itself */
	if (rndr->cb.listitem)
		rndr->cb.listitem(ob, inter, *flags, rndr->opaque);

	rndr_popbuf(rndr, BUFFER_SPAN);
	rndr_popbuf(rndr, BUFFER_SPAN);
	return beg;
}


/* parse_list • parsing ordered or unordered list block */
static size_t
parse_list(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, int flags)
{
	struct sd_buf *work = 0;
	size_t i = 0, j;

	work = rndr_newbuf(rndr, BUFFER_BLOCK);

	while (i < size) {
		j = parse_listitem(work, rndr, data + i, size - i, &flags);
		i += j;

		if (!j || (flags & MKD_LI_END))
			break;
	}

	if (rndr->cb.list)
		rndr->cb.list(ob, work, flags, rndr->opaque);
	rndr_popbuf(rndr, BUFFER_BLOCK);
	return i;
}

/* parse_atxheader • parsing of atx-style headers */
static size_t
parse_atxheader(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t level = 0;
	size_t i, end, skip;

	while (level < size && level < 6 && data[level] == '#')
		level++;

	for (i = level; i < size && data[i] == ' '; i++);

	for (end = i; end < size && data[end] != '\n'; end++);
	skip = end;

	while (end && data[end - 1] == '#')
		end--;

	while (end && data[end - 1] == ' ')
		end--;

	if (end > i) {
		struct sd_buf *work = rndr_newbuf(rndr, BUFFER_SPAN);

		parse_inline(work, rndr, data + i, end - i);

		if (rndr->cb.header)
			rndr->cb.header(ob, work, (int)level, rndr->opaque);

		rndr_popbuf(rndr, BUFFER_SPAN);
	}

	return skip;
}


/* htmlblock_end • checking end of HTML block : </tag>[ \t]*\n[ \t*]\n */
/*	returns the length on match, 0 otherwise */
static size_t
htmlblock_end_tag(
	const char *tag,
	size_t tag_len,
	struct sd_markdown *rndr,
	uint8_t *data,
	size_t size)
{
	size_t i, w;

	/* checking if tag is a match */
	if (tag_len + 3 >= size ||
		strncasecmp((char *)data + 2, tag, tag_len) != 0 ||
		data[tag_len + 2] != '>')
		return 0;

	/* checking white lines */
	i = tag_len + 3;
	w = 0;
	if (i < size && (w = is_empty(data + i, size - i)) == 0)
		return 0; /* non-blank after tag */
	i += w;
	w = 0;

	if (i < size)
		w = is_empty(data + i, size - i);

	return i + w;
}

static size_t
htmlblock_end(const char *curtag,
	struct sd_markdown *rndr,
	uint8_t *data,
	size_t size,
	int start_of_line)
{
	size_t tag_size = strlen(curtag);
	size_t i = 1, end_tag;
	int block_lines = 0;

	while (i < size) {
		i++;
		while (i < size && !(data[i - 1] == '<' && data[i] == '/')) {
			if (data[i] == '\n')
				block_lines++;

			i++;
		}

		/* If we are only looking for unindented tags, skip the tag
		 * if it doesn't follow a newline.
		 *
		 * The only exception to this is if the tag is still on the
		 * initial line; in that case it still counts as a closing
		 * tag
		 */
		if (start_of_line && block_lines > 0 && data[i - 2] != '\n')
			continue;

		if (i + 2 + tag_size >= size)
			break;

		end_tag = htmlblock_end_tag(curtag, tag_size, rndr, data + i - 1, size - i + 1);
		if (end_tag)
			return i + end_tag - 1;
	}

	return 0;
}


/* parse_htmlblock • parsing of inline HTML block */
static size_t
parse_htmlblock(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size, int do_render)
{
	size_t i, j = 0, tag_end;
	const char *curtag = NULL;
	struct sd_buf work = {0};
    work.data = data;

	/* identification of the opening tag */
	if (size < 2 || data[0] != '<')
		return 0;

	i = 1;
	while (i < size && data[i] != '>' && data[i] != ' ')
		i++;

	if (i < size)
		curtag = find_block_tag((char *)data + 1, (int)i - 1);

	/* handling of special cases */
	if (!curtag) {

		/* HTML comment, laxist form */
		if (size > 5 && data[1] == '!' && data[2] == '-' && data[3] == '-') {
			i = 5;

			while (i < size && !(data[i - 2] == '-' && data[i - 1] == '-' && data[i] == '>'))
				i++;

			i++;

			if (i < size)
				j = is_empty(data + i, size - i);

			if (j) {
				work.size = i + j;
				if (do_render && rndr->cb.blockhtml)
					rndr->cb.blockhtml(ob, &work, rndr->opaque);
				return work.size;
			}
		}

		/* HR, which is the only self-closing block tag considered */
		if (size > 4 && (data[1] == 'h' || data[1] == 'H') && (data[2] == 'r' || data[2] == 'R')) {
			i = 3;
			while (i < size && data[i] != '>')
				i++;

			if (i + 1 < size) {
				i++;
				j = is_empty(data + i, size - i);
				if (j) {
					work.size = i + j;
					if (do_render && rndr->cb.blockhtml)
						rndr->cb.blockhtml(ob, &work, rndr->opaque);
					return work.size;
				}
			}
		}

		/* no special case recognised */
		return 0;
	}

	/* looking for an unindented matching closing tag */
	/*	followed by a blank line */
	tag_end = htmlblock_end(curtag, rndr, data, size, 1);

	/* if not found, trying a second pass looking for indented match */
	/* but not if tag is "ins" or "del" (following original Markdown.pl) */
	if (!tag_end && strcmp(curtag, "ins") != 0 && strcmp(curtag, "del") != 0) {
		tag_end = htmlblock_end(curtag, rndr, data, size, 0);
	}

	if (!tag_end)
		return 0;

	/* the end of the block has been found */
	work.size = tag_end;
	if (do_render && rndr->cb.blockhtml)
		rndr->cb.blockhtml(ob, &work, rndr->opaque);

	return tag_end;
}

static void
parse_table_row(
	struct sd_buf *ob,
	struct sd_markdown *rndr,
	uint8_t *data,
	size_t size,
	size_t columns,
	int *col_data,
	int header_flag)
{
	size_t i = 0, col;
	struct sd_buf *row_work = 0;

	if (!rndr->cb.table_cell || !rndr->cb.table_row)
		return;

	row_work = rndr_newbuf(rndr, BUFFER_SPAN);

	if (i < size && data[i] == '|')
		i++;

	for (col = 0; col < columns && i < size; ++col) {
		size_t cell_start, cell_end;
		struct sd_buf *cell_work;

		cell_work = rndr_newbuf(rndr, BUFFER_SPAN);

		while (i < size && _isspace(data[i]))
			i++;

		cell_start = i;

		while (i < size && data[i] != '|')
			i++;

		cell_end = i - 1;

		while (cell_end > cell_start && _isspace(data[cell_end]))
			cell_end--;

		parse_inline(cell_work, rndr, data + cell_start, 1 + cell_end - cell_start);
		rndr->cb.table_cell(row_work, cell_work, col_data[col] | header_flag, rndr->opaque);

		rndr_popbuf(rndr, BUFFER_SPAN);
		i++;
	}

	for (; col < columns; ++col) {
		struct sd_buf empty_cell = { 0, 0, 0, 0 };
		rndr->cb.table_cell(row_work, &empty_cell, col_data[col] | header_flag, rndr->opaque);
	}

	rndr->cb.table_row(ob, row_work, rndr->opaque);

	rndr_popbuf(rndr, BUFFER_SPAN);
}

static size_t
parse_table_header(
	struct sd_buf *ob,
	struct sd_markdown *rndr,
	uint8_t *data,
	size_t size,
	size_t *columns,
	int **column_data)
{
	int pipes;
	size_t i = 0, col, header_end, under_end;

	pipes = 0;
	while (i < size && data[i] != '\n')
		if (data[i++] == '|')
			pipes++;

	if (i == size || pipes == 0)
		return 0;

	header_end = i;

	while (header_end > 0 && _isspace(data[header_end - 1]))
		header_end--;

	if (data[0] == '|')
		pipes--;

	if (header_end && data[header_end - 1] == '|')
		pipes--;

	*columns = pipes + 1;
	*column_data = calloc(*columns, sizeof(int));

	/* Parse the header underline */
	i++;
	if (i < size && data[i] == '|')
		i++;

	under_end = i;
	while (under_end < size && data[under_end] != '\n')
		under_end++;

	for (col = 0; col < *columns && i < under_end; ++col) {
		size_t dashes = 0;

		while (i < under_end && data[i] == ' ')
			i++;

		if (data[i] == ':') {
			i++; (*column_data)[col] |= MKD_TABLE_ALIGN_L;
			dashes++;
		}

		while (i < under_end && data[i] == '-') {
			i++; dashes++;
		}

		if (i < under_end && data[i] == ':') {
			i++; (*column_data)[col] |= MKD_TABLE_ALIGN_R;
			dashes++;
		}

		while (i < under_end && data[i] == ' ')
			i++;

		if (i < under_end && data[i] != '|')
			break;

		if (dashes < 3)
			break;

		i++;
	}

	if (col < *columns)
		return 0;

	parse_table_row(
		ob, rndr, data,
		header_end,
		*columns,
		*column_data,
		MKD_TABLE_HEADER
	);

	return under_end + 1;
}

static size_t
parse_table(
	struct sd_buf *ob,
	struct sd_markdown *rndr,
	uint8_t *data,
	size_t size)
{
	size_t i;

	struct sd_buf *header_work = 0;
	struct sd_buf *body_work = 0;

	size_t columns;
	int *col_data = NULL;

	header_work = rndr_newbuf(rndr, BUFFER_SPAN);
	body_work = rndr_newbuf(rndr, BUFFER_BLOCK);

	i = parse_table_header(header_work, rndr, data, size, &columns, &col_data);
	if (i > 0) {

		while (i < size) {
			size_t row_start;
			int pipes = 0;

			row_start = i;

			while (i < size && data[i] != '\n')
				if (data[i++] == '|')
					pipes++;

			if (pipes == 0 || i == size) {
				i = row_start;
				break;
			}

			parse_table_row(
				body_work,
				rndr,
				data + row_start,
				i - row_start,
				columns,
				col_data, 0
			);

			i++;
		}

		if (rndr->cb.table)
			rndr->cb.table(ob, header_work, body_work, rndr->opaque);
	}

	free(col_data);
	rndr_popbuf(rndr, BUFFER_SPAN);
	rndr_popbuf(rndr, BUFFER_BLOCK);
	return i;
}

/* parse_block • parsing of one block, returning next uint8_t to parse */
static void
parse_block(struct sd_buf *ob, struct sd_markdown *rndr, uint8_t *data, size_t size)
{
	size_t beg, end, i;
	uint8_t *txt_data;
	beg = 0;

	if (rndr->work_bufs[BUFFER_SPAN].size +
		rndr->work_bufs[BUFFER_BLOCK].size > rndr->max_nesting)
		return;

	while (beg < size) {
		txt_data = data + beg;
		end = size - beg;

		if (is_atxheader(rndr, txt_data, end))
			beg += parse_atxheader(ob, rndr, txt_data, end);

		else if (data[beg] == '<' && rndr->cb.blockhtml &&
				(i = parse_htmlblock(ob, rndr, txt_data, end, 1)) != 0)
			beg += i;

		else if ((i = is_empty(txt_data, end)) != 0)
			beg += i;

		else if (is_hrule(txt_data, end)) {
			if (rndr->cb.hrule)
				rndr->cb.hrule(ob, rndr->opaque);

			while (beg < size && data[beg] != '\n')
				beg++;

			beg++;
		}

		else if ((rndr->ext_flags & MKDEXT_FENCED_CODE) != 0 &&
			(i = parse_fencedcode(ob, rndr, txt_data, end)) != 0)
			beg += i;

		else if ((rndr->ext_flags & MKDEXT_TABLES) != 0 &&
			(i = parse_table(ob, rndr, txt_data, end)) != 0)
			beg += i;

		else if (prefix_quote(txt_data, end))
			beg += parse_blockquote(ob, rndr, txt_data, end);

		else if (prefix_code(txt_data, end))
			beg += parse_blockcode(ob, rndr, txt_data, end);

		else if (prefix_uli(txt_data, end))
			beg += parse_list(ob, rndr, txt_data, end, 0);

		else if (prefix_oli(txt_data, end))
			beg += parse_list(ob, rndr, txt_data, end, MKD_LIST_ORDERED);

		else
			beg += parse_paragraph(ob, rndr, txt_data, end);
	}
}



/*********************
 * REFERENCE PARSING *
 *********************/

/* is_ref • returns whether a line is a reference or not */
static int
is_ref(const uint8_t *data, size_t beg, size_t end, size_t *last, struct link_ref **refs)
{
/*	int n; */
	size_t i = 0;
	size_t id_offset, id_end;
	size_t link_offset, link_end;
	size_t title_offset, title_end;
	size_t line_end;

	/* up to 3 optional leading spaces */
	if (beg + 3 >= end) return 0;
	if (data[beg] == ' ') { i = 1;
	if (data[beg + 1] == ' ') { i = 2;
	if (data[beg + 2] == ' ') { i = 3;
	if (data[beg + 3] == ' ') return 0; } } }
	i += beg;

	/* id part: anything but a newline between brackets */
	if (data[i] != '[') return 0;
	i++;
	id_offset = i;
	while (i < end && data[i] != '\n' && data[i] != '\r' && data[i] != ']')
		i++;
	if (i >= end || data[i] != ']') return 0;
	id_end = i;

	/* spacer: colon (space | tab)* newline? (space | tab)* */
	i++;
	if (i >= end || data[i] != ':') return 0;
	i++;
	while (i < end && data[i] == ' ') i++;
	if (i < end && (data[i] == '\n' || data[i] == '\r')) {
		i++;
		if (i < end && data[i] == '\r' && data[i - 1] == '\n') i++; }
	while (i < end && data[i] == ' ') i++;
	if (i >= end) return 0;

	/* link: whitespace-free sequence, optionally between angle brackets */
	if (data[i] == '<')
		i++;

	link_offset = i;

	while (i < end && data[i] != ' ' && data[i] != '\n' && data[i] != '\r')
		i++;

	if (data[i - 1] == '>') link_end = i - 1;
	else link_end = i;

	/* optional spacer: (space | tab)* (newline | '\'' | '"' | '(' ) */
	while (i < end && data[i] == ' ') i++;
	if (i < end && data[i] != '\n' && data[i] != '\r'
			&& data[i] != '\'' && data[i] != '"' && data[i] != '(')
		return 0;
	line_end = 0;
	/* computing end-of-line */
	if (i >= end || data[i] == '\r' || data[i] == '\n') line_end = i;
	if (i + 1 < end && data[i] == '\n' && data[i + 1] == '\r')
		line_end = i + 1;

	/* optional (space|tab)* spacer after a newline */
	if (line_end) {
		i = line_end + 1;
		while (i < end && data[i] == ' ') i++; }

	/* optional title: any non-newline sequence enclosed in '"()
					alone on its line */
	title_offset = title_end = 0;
	if (i + 1 < end
	&& (data[i] == '\'' || data[i] == '"' || data[i] == '(')) {
		i++;
		title_offset = i;
		/* looking for EOL */
		while (i < end && data[i] != '\n' && data[i] != '\r') i++;
		if (i + 1 < end && data[i] == '\n' && data[i + 1] == '\r')
			title_end = i + 1;
		else	title_end = i;
		/* stepping back */
		i -= 1;
		while (i > title_offset && data[i] == ' ')
			i -= 1;
		if (i > title_offset
		&& (data[i] == '\'' || data[i] == '"' || data[i] == ')')) {
			line_end = title_end;
			title_end = i; } }

	if (!line_end || link_end == link_offset)
		return 0; /* garbage after the link empty link */

	/* a valid ref has been found, filling-in return structures */
	if (last)
		*last = line_end;

	if (refs) {
		struct link_ref *ref;

		ref = add_link_ref(refs, data + id_offset, id_end - id_offset);
		if (!ref)
			return 0;

		ref->link = sd_bufnew(link_end - link_offset);
		sd_bufput(ref->link, data + link_offset, link_end - link_offset);

		if (title_end > title_offset) {
			ref->title = sd_bufnew(title_end - title_offset);
			sd_bufput(ref->title, data + title_offset, title_end - title_offset);
		}
	}

	return 1;
}

static void expand_tabs(struct sd_buf *ob, const uint8_t *line, size_t size)
{
	size_t  i = 0, tab = 0;

	while (i < size) {
		size_t org = i;

		while (i < size && line[i] != '\t') {
			i++; tab++;
		}

		if (i > org)
			sd_bufput(ob, line + org, i - org);

		if (i >= size)
			break;

		do {
			sd_bufputc(ob, ' '); tab++;
		} while (tab % 4);

		i++;
	}
}

/**********************
 * EXPORTED FUNCTIONS *
 **********************/

struct sd_markdown *
sd_markdown_new(
	unsigned int extensions,
	size_t max_nesting,
	const struct sd_callbacks *callbacks,
	void *opaque)
{
	struct sd_markdown *md = NULL;

	assert(max_nesting > 0 && callbacks);

	md = malloc(sizeof(struct sd_markdown));
	if (!md)
		return NULL;

	memcpy(&md->cb, callbacks, sizeof(struct sd_callbacks));

	stack_init(&md->work_bufs[BUFFER_BLOCK], 4);
	stack_init(&md->work_bufs[BUFFER_SPAN], 8);

	memset(md->active_char, 0x0, 256);

	if (md->cb.emphasis || md->cb.double_emphasis || md->cb.triple_emphasis) {
		md->active_char['*'] = MD_CHAR_EMPHASIS;
		md->active_char['_'] = MD_CHAR_EMPHASIS;
		if (extensions & MKDEXT_STRIKETHROUGH)
			md->active_char['~'] = MD_CHAR_EMPHASIS;
	}

	if (md->cb.codespan)
		md->active_char['`'] = MD_CHAR_CODESPAN;

	if (md->cb.linebreak)
		md->active_char['\n'] = MD_CHAR_LINEBREAK;

	if (md->cb.image || md->cb.link)
		md->active_char['['] = MD_CHAR_LINK;

	md->active_char['<'] = MD_CHAR_LANGLE;
	md->active_char['\\'] = MD_CHAR_ESCAPE;
	md->active_char['&'] = MD_CHAR_ENTITITY;

	if (extensions & MKDEXT_AUTOLINK) {
		md->active_char[':'] = MD_CHAR_AUTOLINK_URL;
		md->active_char['@'] = MD_CHAR_AUTOLINK_EMAIL;
		md->active_char['w'] = MD_CHAR_AUTOLINK_WWW;
	}

	if (extensions & MKDEXT_SUPERSCRIPT)
		md->active_char['^'] = MD_CHAR_SUPERSCRIPT;

	/* Extension data */
	md->ext_flags = extensions;
	md->opaque = opaque;
	md->max_nesting = max_nesting;
	md->in_link_body = 0;

	return md;
}

void
sd_markdown_render(struct sd_buf *ob, const uint8_t *document, size_t doc_size, struct sd_markdown *md)
{
#define MARKDOWN_GROW(x) ((x) + ((x) >> 1))
	static const char UTF8_BOM[] = {0xEF, 0xBB, 0xBF};

	struct sd_buf *text;
	size_t beg, end;

	text = sd_bufnew(64);
	if (!text)
		return;

	/* Preallocate enough space for our buffer to avoid expanding while copying */
	sd_bufgrow(text, doc_size);

	/* reset the references table */
	memset(&md->refs, 0x0, REF_TABLE_SIZE * sizeof(void *));

	/* first pass: looking for references, copying everything else */
	beg = 0;

	/* Skip a possible UTF-8 BOM, even though the Unicode standard
	 * discourages having these in UTF-8 documents */
	if (doc_size >= 3 && memcmp(document, UTF8_BOM, 3) == 0)
		beg += 3;

	while (beg < doc_size) /* iterating over lines */
		if (is_ref(document, beg, doc_size, &end, md->refs))
			beg = end;
		else { /* skipping to the next line */
			end = beg;
			while (end < doc_size && document[end] != '\n' && document[end] != '\r')
				end++;

			/* adding the line body if present */
			if (end > beg)
				expand_tabs(text, document + beg, end - beg);

			while (end < doc_size && (document[end] == '\n' || document[end] == '\r')) {
				/* add one \n per newline */
				if (document[end] == '\n' || (end + 1 < doc_size && document[end + 1] != '\n'))
					sd_bufputc(text, '\n');
				end++;
			}

			beg = end;
		}

	/* pre-grow the output buffer to minimize allocations */
	sd_bufgrow(ob, MARKDOWN_GROW(text->size));

	/* second pass: actual rendering */
	if (md->cb.doc_header)
		md->cb.doc_header(ob, md->opaque);

	if (text->size) {
		/* adding a final newline if not already present */
		if (text->data[text->size - 1] != '\n' &&  text->data[text->size - 1] != '\r')
			sd_bufputc(text, '\n');

		parse_block(ob, md, text->data, text->size);
	}

	if (md->cb.doc_footer)
		md->cb.doc_footer(ob, md->opaque);

	/* clean-up */
	sd_bufrelease(text);
	free_link_refs(md->refs);

	assert(md->work_bufs[BUFFER_SPAN].size == 0);
	assert(md->work_bufs[BUFFER_BLOCK].size == 0);
}

void
sd_markdown_free(struct sd_markdown *md)
{
	size_t i;

	for (i = 0; i < (size_t)md->work_bufs[BUFFER_SPAN].asize; ++i)
		sd_bufrelease(md->work_bufs[BUFFER_SPAN].item[i]);

	for (i = 0; i < (size_t)md->work_bufs[BUFFER_BLOCK].asize; ++i)
		sd_bufrelease(md->work_bufs[BUFFER_BLOCK].item[i]);

	stack_free(&md->work_bufs[BUFFER_SPAN]);
	stack_free(&md->work_bufs[BUFFER_BLOCK]);

	free(md);
}

void
sd_version(int *ver_major, int *ver_minor, int *ver_revision)
{
	*ver_major = SUNDOWN_VER_MAJOR;
	*ver_minor = SUNDOWN_VER_MINOR;
	*ver_revision = SUNDOWN_VER_REVISION;
}

// ENDREGION: MARKDOWN.C

// REGION: STACK.C

int
stack_grow(struct stack *st, size_t new_size)
{
	void **new_st;

	if (st->asize >= new_size)
		return 0;

	new_st = realloc(st->item, new_size * sizeof(void *));
	if (new_st == NULL)
		return -1;

	memset(new_st + st->asize, 0x0,
		(new_size - st->asize) * sizeof(void *));

	st->item = new_st;
	st->asize = new_size;

	if (st->size > new_size)
		st->size = new_size;

	return 0;
}

void
stack_free(struct stack *st)
{
	if (!st)
		return;

	free(st->item);

	st->item = NULL;
	st->size = 0;
	st->asize = 0;
}

int
stack_init(struct stack *st, size_t initial_size)
{
	st->item = NULL;
	st->size = 0;
	st->asize = 0;

	if (!initial_size)
		initial_size = 8;

	return stack_grow(st, initial_size);
}

void *
stack_pop(struct stack *st)
{
	if (!st->size)
		return NULL;

	return st->item[--st->size];
}

int
stack_push(struct stack *st, void *item)
{
	if (stack_grow(st, st->size * 2) < 0)
		return -1;

	st->item[st->size++] = item;
	return 0;
}

void *
stack_top(struct stack *st)
{
	if (!st->size)
		return NULL;

	return st->item[st->size - 1];
}

// ENDREGION: STACK.C

#endif // SD_IMPLEMENTATION

#ifndef SD_NO_HTML

//REGION: HTML.H

struct html_renderopt {
	struct {
		int header_count;
		int current_level;
		int level_offset;
	} toc_data;

	unsigned int flags;

	/* extra callbacks */
	void (*link_attributes)(struct sd_buf *ob, const struct sd_buf *url, void *self);
};

typedef enum {
	HTML_SKIP_HTML = (1 << 0),
	HTML_SKIP_STYLE = (1 << 1),
	HTML_SKIP_IMAGES = (1 << 2),
	HTML_SKIP_LINKS = (1 << 3),
	HTML_EXPAND_TABS = (1 << 4),
	HTML_SAFELINK = (1 << 5),
	HTML_TOC = (1 << 6),
	HTML_HARD_WRAP = (1 << 7),
	HTML_USE_XHTML = (1 << 8),
	HTML_ESCAPE = (1 << 9),
} html_render_mode;

typedef enum {
	HTML_TAG_NONE = 0,
	HTML_TAG_OPEN,
	HTML_TAG_CLOSE,
} html_tag;

int
sdhtml_is_tag(const uint8_t *tag_data, size_t tag_size, const char *tagname);

/*******************
 * MAIN SDHTML API *
 *******************/

extern void
sdhtml_renderer(struct sd_callbacks *callbacks, struct html_renderopt *options_ptr, unsigned int render_flags);

extern void
sdhtml_toc_renderer(struct sd_callbacks *callbacks, struct html_renderopt *options_ptr);

extern void
sdhtml_smartypants(struct sd_buf *ob, const uint8_t *text, size_t size);

//ENDREGION: HTML.H

//REGION: HOUDINI.H

#ifdef HOUDINI_USE_LOCALE
#	define _isxdigit(c) isxdigit(c)
#	define _isdigit(c) isdigit(c)
#else
/*
 * Helper _isdigit methods -- do not trust the current locale
 * */
#	define _isxdigit(c) (strchr("0123456789ABCDEFabcdef", (c)) != NULL)
#	define _isdigit(c) ((c) >= '0' && (c) <= '9')
#endif

/********************
 * MAIN HOUDINI API *
 ********************/

extern void houdini_escape_html(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_html0(struct sd_buf *ob, const uint8_t *src, size_t size, int secure);
extern void houdini_unescape_html(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_xml(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_uri(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_url(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_href(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_unescape_uri(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_unescape_url(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_escape_js(struct sd_buf *ob, const uint8_t *src, size_t size);
extern void houdini_unescape_js(struct sd_buf *ob, const uint8_t *src, size_t size);

//ENDREGION: HOUDINI.H

#ifdef SD_IMPLEMENTATION

// REGION: HTML.C

#define USE_XHTML(opt) (opt->flags & HTML_USE_XHTML)

int
sdhtml_is_tag(const uint8_t *tag_data, size_t tag_size, const char *tagname)
{
	size_t i;
	int closed = 0;

	if (tag_size < 3 || tag_data[0] != '<')
		return HTML_TAG_NONE;

	i = 1;

	if (tag_data[i] == '/') {
		closed = 1;
		i++;
	}

	for (; i < tag_size; ++i, ++tagname) {
		if (*tagname == 0)
			break;

		if (tag_data[i] != *tagname)
			return HTML_TAG_NONE;
	}

	if (i == tag_size)
		return HTML_TAG_NONE;

	if (isspace(tag_data[i]) || tag_data[i] == '>')
		return closed ? HTML_TAG_CLOSE : HTML_TAG_OPEN;

	return HTML_TAG_NONE;
}

static inline void escape_html(struct sd_buf *ob, const uint8_t *source, size_t length)
{
	houdini_escape_html0(ob, source, length, 0);
}

static inline void escape_href(struct sd_buf *ob, const uint8_t *source, size_t length)
{
	houdini_escape_href(ob, source, length);
}

/********************
 * GENERIC RENDERER *
 ********************/
static int
rndr_autolink(struct sd_buf *ob, const struct sd_buf *link, enum mkd_autolink type, void *opaque)
{
	struct html_renderopt *options = opaque;

	if (!link || !link->size)
		return 0;

	if ((options->flags & HTML_SAFELINK) != 0 &&
		!sd_autolink_issafe(link->data, link->size) &&
		type != MKDA_EMAIL)
		return 0;

	SD_BUFPUTSL(ob, "<a href=\"");
	if (type == MKDA_EMAIL)
		SD_BUFPUTSL(ob, "mailto:");
	escape_href(ob, link->data, link->size);

	if (options->link_attributes) {
		sd_bufputc(ob, '\"');
		options->link_attributes(ob, link, opaque);
		sd_bufputc(ob, '>');
	} else {
		SD_BUFPUTSL(ob, "\">");
	}

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */
	if (sd_bufprefix(link, "mailto:") == 0) {
		escape_html(ob, link->data + 7, link->size - 7);
	} else {
		escape_html(ob, link->data, link->size);
	}

	SD_BUFPUTSL(ob, "</a>");

	return 1;
}

static void
rndr_blockcode(struct sd_buf *ob, const struct sd_buf *text, const struct sd_buf *lang, void *opaque)
{
	if (ob->size) sd_bufputc(ob, '\n');

	if (lang && lang->size) {
		size_t i, cls;
		SD_BUFPUTSL(ob, "<pre><code class=\"");

		for (i = 0, cls = 0; i < lang->size; ++i, ++cls) {
			while (i < lang->size && isspace(lang->data[i]))
				i++;

			if (i < lang->size) {
				size_t org = i;
				while (i < lang->size && !isspace(lang->data[i]))
					i++;

				if (lang->data[org] == '.')
					org++;

				if (cls) sd_bufputc(ob, ' ');
				escape_html(ob, lang->data + org, i - org);
			}
		}

		SD_BUFPUTSL(ob, "\">");
	} else
		SD_BUFPUTSL(ob, "<pre><code>");

	if (text)
		escape_html(ob, text->data, text->size);

	SD_BUFPUTSL(ob, "</code></pre>\n");
}

static void
rndr_blockquote(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (ob->size) sd_bufputc(ob, '\n');
	SD_BUFPUTSL(ob, "<blockquote>\n");
	if (text) sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</blockquote>\n");
}

static int
rndr_codespan(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	SD_BUFPUTSL(ob, "<code>");
	if (text) escape_html(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</code>");
	return 1;
}

static int
rndr_strikethrough(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (!text || !text->size)
		return 0;

	SD_BUFPUTSL(ob, "<del>");
	sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</del>");
	return 1;
}

static int
rndr_double_emphasis(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (!text || !text->size)
		return 0;

	SD_BUFPUTSL(ob, "<strong>");
	sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</strong>");

	return 1;
}

static int
rndr_emphasis(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (!text || !text->size) return 0;
	SD_BUFPUTSL(ob, "<em>");
	if (text) sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</em>");
	return 1;
}

static int
rndr_linebreak(struct sd_buf *ob, void *opaque)
{
	struct html_renderopt *options = opaque;
	sd_bufputs(ob, USE_XHTML(options) ? "<br/>\n" : "<br>\n");
	return 1;
}

static void
rndr_header(struct sd_buf *ob, const struct sd_buf *text, int level, void *opaque)
{
	struct html_renderopt *options = opaque;

	if (ob->size)
		sd_bufputc(ob, '\n');

	if (options->flags & HTML_TOC)
		sd_bufprintf(ob, "<h%d id=\"toc_%d\">", level, options->toc_data.header_count++);
	else
		sd_bufprintf(ob, "<h%d>", level);

	if (text) sd_bufput(ob, text->data, text->size);
	sd_bufprintf(ob, "</h%d>\n", level);
}

static int
rndr_link(struct sd_buf *ob, const struct sd_buf *link, const struct sd_buf *title, const struct sd_buf *content, void *opaque)
{
	struct html_renderopt *options = opaque;

	if (link != NULL && (options->flags & HTML_SAFELINK) != 0 && !sd_autolink_issafe(link->data, link->size))
		return 0;

	SD_BUFPUTSL(ob, "<a href=\"");

	if (link && link->size)
		escape_href(ob, link->data, link->size);

	if (title && title->size) {
		SD_BUFPUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size);
	}

	if (options->link_attributes) {
		sd_bufputc(ob, '\"');
		options->link_attributes(ob, link, opaque);
		sd_bufputc(ob, '>');
	} else {
		SD_BUFPUTSL(ob, "\">");
	}

	if (content && content->size) sd_bufput(ob, content->data, content->size);
	SD_BUFPUTSL(ob, "</a>");
	return 1;
}

static void
rndr_list(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque)
{
	if (ob->size) sd_bufputc(ob, '\n');
	sd_bufput(ob, flags & MKD_LIST_ORDERED ? "<ol>\n" : "<ul>\n", 5);
	if (text) sd_bufput(ob, text->data, text->size);
	sd_bufput(ob, flags & MKD_LIST_ORDERED ? "</ol>\n" : "</ul>\n", 6);
}

static void
rndr_listitem(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque)
{
	SD_BUFPUTSL(ob, "<li>");
	if (text) {
		size_t size = text->size;
		while (size && text->data[size - 1] == '\n')
			size--;

		sd_bufput(ob, text->data, size);
	}
	SD_BUFPUTSL(ob, "</li>\n");
}

static void
rndr_paragraph(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	struct html_renderopt *options = opaque;
	size_t i = 0;

	if (ob->size) sd_bufputc(ob, '\n');

	if (!text || !text->size)
		return;

	while (i < text->size && isspace(text->data[i])) i++;

	if (i == text->size)
		return;

	SD_BUFPUTSL(ob, "<p>");
	if (options->flags & HTML_HARD_WRAP) {
		size_t org;
		while (i < text->size) {
			org = i;
			while (i < text->size && text->data[i] != '\n')
				i++;

			if (i > org)
				sd_bufput(ob, text->data + org, i - org);

			/*
			 * do not insert a line break if this newline
			 * is the last character on the paragraph
			 */
			if (i >= text->size - 1)
				break;

			rndr_linebreak(ob, opaque);
			i++;
		}
	} else {
		sd_bufput(ob, &text->data[i], text->size - i);
	}
	SD_BUFPUTSL(ob, "</p>\n");
}

static void
rndr_raw_block(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	size_t org, sz;
	if (!text) return;
	sz = text->size;
	while (sz > 0 && text->data[sz - 1] == '\n') sz--;
	org = 0;
	while (org < sz && text->data[org] == '\n') org++;
	if (org >= sz) return;
	if (ob->size) sd_bufputc(ob, '\n');
	sd_bufput(ob, text->data + org, sz - org);
	sd_bufputc(ob, '\n');
}

static int
rndr_triple_emphasis(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (!text || !text->size) return 0;
	SD_BUFPUTSL(ob, "<strong><em>");
	sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</em></strong>");
	return 1;
}

static void
rndr_hrule(struct sd_buf *ob, void *opaque)
{
	struct html_renderopt *options = opaque;
	if (ob->size) sd_bufputc(ob, '\n');
	sd_bufputs(ob, USE_XHTML(options) ? "<hr/>\n" : "<hr>\n");
}

static int
rndr_image(struct sd_buf *ob, const struct sd_buf *link, const struct sd_buf *title, const struct sd_buf *alt, void *opaque)
{
	struct html_renderopt *options = opaque;
	if (!link || !link->size) return 0;

	SD_BUFPUTSL(ob, "<img src=\"");
	escape_href(ob, link->data, link->size);
	SD_BUFPUTSL(ob, "\" alt=\"");

	if (alt && alt->size)
		escape_html(ob, alt->data, alt->size);

	if (title && title->size) {
		SD_BUFPUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size); }

	sd_bufputs(ob, USE_XHTML(options) ? "\"/>" : "\">");
	return 1;
}

static int
rndr_raw_html(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	struct html_renderopt *options = opaque;

	/* HTML_ESCAPE overrides SKIP_HTML, SKIP_STYLE, SKIP_LINKS and SKIP_IMAGES
	* It doens't see if there are any valid tags, just escape all of them. */
	if((options->flags & HTML_ESCAPE) != 0) {
		escape_html(ob, text->data, text->size);
		return 1;
	}

	if ((options->flags & HTML_SKIP_HTML) != 0)
		return 1;

	if ((options->flags & HTML_SKIP_STYLE) != 0 &&
		sdhtml_is_tag(text->data, text->size, "style"))
		return 1;

	if ((options->flags & HTML_SKIP_LINKS) != 0 &&
		sdhtml_is_tag(text->data, text->size, "a"))
		return 1;

	if ((options->flags & HTML_SKIP_IMAGES) != 0 &&
		sdhtml_is_tag(text->data, text->size, "img"))
		return 1;

	sd_bufput(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(struct sd_buf *ob, const struct sd_buf *header, const struct sd_buf *body, void *opaque)
{
	if (ob->size) sd_bufputc(ob, '\n');
	SD_BUFPUTSL(ob, "<table><thead>\n");
	if (header)
		sd_bufput(ob, header->data, header->size);
	SD_BUFPUTSL(ob, "</thead><tbody>\n");
	if (body)
		sd_bufput(ob, body->data, body->size);
	SD_BUFPUTSL(ob, "</tbody></table>\n");
}

static void
rndr_tablerow(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	SD_BUFPUTSL(ob, "<tr>\n");
	if (text)
		sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(struct sd_buf *ob, const struct sd_buf *text, int flags, void *opaque)
{
	if (flags & MKD_TABLE_HEADER) {
		SD_BUFPUTSL(ob, "<th");
	} else {
		SD_BUFPUTSL(ob, "<td");
	}

	switch (flags & MKD_TABLE_ALIGNMASK) {
	case MKD_TABLE_ALIGN_CENTER:
		SD_BUFPUTSL(ob, " align=\"center\">");
		break;

	case MKD_TABLE_ALIGN_L:
		SD_BUFPUTSL(ob, " align=\"left\">");
		break;

	case MKD_TABLE_ALIGN_R:
		SD_BUFPUTSL(ob, " align=\"right\">");
		break;

	default:
		SD_BUFPUTSL(ob, ">");
	}

	if (text)
		sd_bufput(ob, text->data, text->size);

	if (flags & MKD_TABLE_HEADER) {
		SD_BUFPUTSL(ob, "</th>\n");
	} else {
		SD_BUFPUTSL(ob, "</td>\n");
	}
}

static int
rndr_superscript(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (!text || !text->size) return 0;
	SD_BUFPUTSL(ob, "<sup>");
	sd_bufput(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(struct sd_buf *ob, const struct sd_buf *text, void *opaque)
{
	if (text)
		escape_html(ob, text->data, text->size);
}

static void
toc_header(struct sd_buf *ob, const struct sd_buf *text, int level, void *opaque)
{
	struct html_renderopt *options = opaque;

	/* set the level offset if this is the first header
	 * we're parsing for the document */
	if (options->toc_data.current_level == 0) {
		options->toc_data.level_offset = level - 1;
	}
	level -= options->toc_data.level_offset;

	if (level > options->toc_data.current_level) {
		while (level > options->toc_data.current_level) {
			SD_BUFPUTSL(ob, "<ul>\n<li>\n");
			options->toc_data.current_level++;
		}
	} else if (level < options->toc_data.current_level) {
		SD_BUFPUTSL(ob, "</li>\n");
		while (level < options->toc_data.current_level) {
			SD_BUFPUTSL(ob, "</ul>\n</li>\n");
			options->toc_data.current_level--;
		}
		SD_BUFPUTSL(ob,"<li>\n");
	} else {
		SD_BUFPUTSL(ob,"</li>\n<li>\n");
	}

	sd_bufprintf(ob, "<a href=\"#toc_%d\">", options->toc_data.header_count++);
	if (text)
		escape_html(ob, text->data, text->size);
	SD_BUFPUTSL(ob, "</a>\n");
}

static int
toc_link(struct sd_buf *ob, const struct sd_buf *link, const struct sd_buf *title, const struct sd_buf *content, void *opaque)
{
	if (content && content->size)
		sd_bufput(ob, content->data, content->size);
	return 1;
}

static void
toc_finalize(struct sd_buf *ob, void *opaque)
{
	struct html_renderopt *options = opaque;

	while (options->toc_data.current_level > 0) {
		SD_BUFPUTSL(ob, "</li>\n</ul>\n");
		options->toc_data.current_level--;
	}
}

void
sdhtml_toc_renderer(struct sd_callbacks *callbacks, struct html_renderopt *options)
{
	static const struct sd_callbacks cb_default = {
		NULL,
		NULL,
		NULL,
		toc_header,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,

		NULL,
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		NULL,
		NULL,
		toc_link,
		NULL,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,

		NULL,
		NULL,

		NULL,
		toc_finalize,
	};

	memset(options, 0x0, sizeof(struct html_renderopt));
	options->flags = HTML_TOC;

	memcpy(callbacks, &cb_default, sizeof(struct sd_callbacks));
}

void
sdhtml_renderer(struct sd_callbacks *callbacks, struct html_renderopt *options, unsigned int render_flags)
{
	static const struct sd_callbacks cb_default = {
		rndr_blockcode,
		rndr_blockquote,
		rndr_raw_block,
		rndr_header,
		rndr_hrule,
		rndr_list,
		rndr_listitem,
		rndr_paragraph,
		rndr_table,
		rndr_tablerow,
		rndr_tablecell,

		rndr_autolink,
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		rndr_image,
		rndr_linebreak,
		rndr_link,
		rndr_raw_html,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,

		NULL,
		rndr_normal_text,

		NULL,
		NULL,
	};

	/* Prepare the options pointer */
	memset(options, 0x0, sizeof(struct html_renderopt));
	options->flags = render_flags;

	/* Prepare the callbacks */
	memcpy(callbacks, &cb_default, sizeof(struct sd_callbacks));

	if (render_flags & HTML_SKIP_IMAGES)
		callbacks->image = NULL;

	if (render_flags & HTML_SKIP_LINKS) {
		callbacks->link = NULL;
		callbacks->autolink = NULL;
	}

	if (render_flags & HTML_SKIP_HTML || render_flags & HTML_ESCAPE)
		callbacks->blockhtml = NULL;
}

//ENDREGION: HTML.C

//REGION HOUDINI_HTML_E.C

#define ESCAPE_GROW_FACTOR(x) (((x) * 12) / 10) /* this is very scientific, yes */

/**
 * According to the OWASP rules:
 *
 * & --> &amp;
 * < --> &lt;
 * > --> &gt;
 * " --> &quot;
 * ' --> &#x27;     &apos; is not recommended
 * / --> &#x2F;     forward slash is included as it helps end an HTML entity
 *
 */
static const char HTML_ESCAPE_TABLE[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 4, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 6, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const char *HTML_ESCAPES[] = {
        "",
        "&quot;",
        "&amp;",
        "&#39;",
        "&#47;",
        "&lt;",
        "&gt;"
};

void
houdini_escape_html0(struct sd_buf *ob, const uint8_t *src, size_t size, int secure)
{
	size_t i = 0, org, esc = 0;

	sd_bufgrow(ob, ESCAPE_GROW_FACTOR(size));

	while (i < size) {
		org = i;
		while (i < size && (esc = HTML_ESCAPE_TABLE[src[i]]) == 0)
			i++;

		if (i > org)
			sd_bufput(ob, src + org, i - org);

		/* escaping */
		if (i >= size)
			break;

		/* The forward slash is only escaped in secure mode */
		if (src[i] == '/' && !secure) {
			sd_bufputc(ob, '/');
		} else {
			sd_bufputs(ob, HTML_ESCAPES[esc]);
		}

		i++;
	}
}

void
houdini_escape_html(struct sd_buf *ob, const uint8_t *src, size_t size)
{
	houdini_escape_html0(ob, src, size, 1);
}

//ENDREGION HOUDINI_HTML_E.C

//REGION HOUDINI_HREF_E.C

static const char HREF_SAFE[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void
houdini_escape_href(struct sd_buf *ob, const uint8_t *src, size_t size)
{
	static const char hex_chars[] = "0123456789ABCDEF";
	size_t  i = 0, org;
	char hex_str[3];

	sd_bufgrow(ob, ESCAPE_GROW_FACTOR(size));
	hex_str[0] = '%';

	while (i < size) {
		org = i;
		while (i < size && HREF_SAFE[src[i]] != 0)
			i++;

		if (i > org)
			sd_bufput(ob, src + org, i - org);

		/* escaping */
		if (i >= size)
			break;

		switch (src[i]) {
		/* amp appears all the time in URLs, but needs
		 * HTML-entity escaping to be inside an href */
		case '&': 
			SD_BUFPUTSL(ob, "&amp;");
			break;

		/* the single quote is a valid URL character
		 * according to the standard; it needs HTML
		 * entity escaping too */
		case '\'':
			SD_BUFPUTSL(ob, "&#x27;");
			break;
		
		/* the space can be escaped to %20 or a plus
		 * sign. we're going with the generic escape
		 * for now. the plus thing is more commonly seen
		 * when building GET strings */
#if 0
		case ' ':
			sd_bufputc(ob, '+');
			break;
#endif

		/* every other character goes with a %XX escaping */
		default:
			hex_str[1] = hex_chars[(src[i] >> 4) & 0xF];
			hex_str[2] = hex_chars[src[i] & 0xF];
			sd_bufput(ob, hex_str, 3);
		}

		i++;
	}
}

//ENDREGION HOUDINI_HREF_E.C

//REGION HTML_SMARTYPANTS.C

struct smartypants_data {
	int in_squote;
	int in_dquote;
};

static size_t smartypants_cb__ltag(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__dquote(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__amp(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__period(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__number(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__dash(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__parens(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__squote(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__backtick(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);
static size_t smartypants_cb__escape(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size);

static size_t (*smartypants_cb_ptrs[])
	(struct sd_buf *, struct smartypants_data *, uint8_t, const uint8_t *, size_t) =
{
	NULL,					/* 0 */
	smartypants_cb__dash,	/* 1 */
	smartypants_cb__parens,	/* 2 */
	smartypants_cb__squote, /* 3 */
	smartypants_cb__dquote, /* 4 */
	smartypants_cb__amp,	/* 5 */
	smartypants_cb__period,	/* 6 */
	smartypants_cb__number,	/* 7 */
	smartypants_cb__ltag,	/* 8 */
	smartypants_cb__backtick, /* 9 */
	smartypants_cb__escape, /* 10 */
};

static const uint8_t smartypants_cb_chars[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 4, 0, 0, 0, 5, 3, 2, 0, 0, 0, 0, 1, 6, 0,
	0, 7, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0,
	9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static inline int
word_boundary(uint8_t c)
{
	return c == 0 || isspace(c) || ispunct(c);
}

static int
smartypants_quotes(struct sd_buf *ob, uint8_t previous_char, uint8_t next_char, uint8_t quote, int *is_open)
{
	char ent[8];

	if (*is_open && !word_boundary(next_char))
		return 0;

	if (!(*is_open) && !word_boundary(previous_char))
		return 0;

	snprintf(ent, sizeof(ent), "&%c%cquo;", (*is_open) ? 'r' : 'l', quote);
	*is_open = !(*is_open);
	sd_bufputs(ob, ent);
	return 1;
}

static size_t
smartypants_cb__squote(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 2) {
		uint8_t t1 = (uint8_t)tolower(text[1]);

		if (t1 == '\'') {
			if (smartypants_quotes(ob, previous_char, size >= 3 ? text[2] : 0, 'd', &smrt->in_dquote))
				return 1;
		}

		if ((t1 == 's' || t1 == 't' || t1 == 'm' || t1 == 'd') &&
			(size == 3 || word_boundary(text[2]))) {
			SD_BUFPUTSL(ob, "&rsquo;");
			return 0;
		}

		if (size >= 3) {
			uint8_t t2 = (uint8_t)tolower(text[2]);

			if (((t1 == 'r' && t2 == 'e') ||
				(t1 == 'l' && t2 == 'l') ||
				(t1 == 'v' && t2 == 'e')) &&
				(size == 4 || word_boundary(text[3]))) {
				SD_BUFPUTSL(ob, "&rsquo;");
				return 0;
			}
		}
	}

	if (smartypants_quotes(ob, previous_char, size > 0 ? text[1] : 0, 's', &smrt->in_squote))
		return 0;

	sd_bufputc(ob, text[0]);
	return 0;
}

static size_t
smartypants_cb__parens(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 3) {
		uint8_t t1 = (uint8_t)tolower(text[1]);
		uint8_t t2 = (uint8_t)tolower(text[2]);

		if (t1 == 'c' && t2 == ')') {
			SD_BUFPUTSL(ob, "&copy;");
			return 2;
		}

		if (t1 == 'r' && t2 == ')') {
			SD_BUFPUTSL(ob, "&reg;");
			return 2;
		}

		if (size >= 4 && t1 == 't' && t2 == 'm' && text[3] == ')') {
			SD_BUFPUTSL(ob, "&trade;");
			return 3;
		}
	}

	sd_bufputc(ob, text[0]);
	return 0;
}

static size_t
smartypants_cb__dash(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 3 && text[1] == '-' && text[2] == '-') {
		SD_BUFPUTSL(ob, "&mdash;");
		return 2;
	}

	if (size >= 2 && text[1] == '-') {
		SD_BUFPUTSL(ob, "&ndash;");
		return 1;
	}

	sd_bufputc(ob, text[0]);
	return 0;
}

static size_t
smartypants_cb__amp(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 6 && memcmp(text, "&quot;", 6) == 0) {
		if (smartypants_quotes(ob, previous_char, size >= 7 ? text[6] : 0, 'd', &smrt->in_dquote))
			return 5;
	}

	if (size >= 4 && memcmp(text, "&#0;", 4) == 0)
		return 3;

	sd_bufputc(ob, '&');
	return 0;
}

static size_t
smartypants_cb__period(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 3 && text[1] == '.' && text[2] == '.') {
		SD_BUFPUTSL(ob, "&hellip;");
		return 2;
	}

	if (size >= 5 && text[1] == ' ' && text[2] == '.' && text[3] == ' ' && text[4] == '.') {
		SD_BUFPUTSL(ob, "&hellip;");
		return 4;
	}

	sd_bufputc(ob, text[0]);
	return 0;
}

static size_t
smartypants_cb__backtick(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size >= 2 && text[1] == '`') {
		if (smartypants_quotes(ob, previous_char, size >= 3 ? text[2] : 0, 'd', &smrt->in_dquote))
			return 1;
	}

	return 0;
}

static size_t
smartypants_cb__number(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (word_boundary(previous_char) && size >= 3) {
		if (text[0] == '1' && text[1] == '/' && text[2] == '2') {
			if (size == 3 || word_boundary(text[3])) {
				SD_BUFPUTSL(ob, "&frac12;");
				return 2;
			}
		}

		if (text[0] == '1' && text[1] == '/' && text[2] == '4') {
			if (size == 3 || word_boundary(text[3]) ||
				(size >= 5 && tolower(text[3]) == 't' && tolower(text[4]) == 'h')) {
				SD_BUFPUTSL(ob, "&frac14;");
				return 2;
			}
		}

		if (text[0] == '3' && text[1] == '/' && text[2] == '4') {
			if (size == 3 || word_boundary(text[3]) ||
				(size >= 6 && tolower(text[3]) == 't' && tolower(text[4]) == 'h' && tolower(text[5]) == 's')) {
				SD_BUFPUTSL(ob, "&frac34;");
				return 2;
			}
		}
	}

	sd_bufputc(ob, text[0]);
	return 0;
}

static size_t
smartypants_cb__dquote(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (!smartypants_quotes(ob, previous_char, size > 0 ? text[1] : 0, 'd', &smrt->in_dquote))
		SD_BUFPUTSL(ob, "&quot;");

	return 0;
}

static size_t
smartypants_cb__ltag(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	static const char *skip_tags[] = {
	  "pre", "code", "var", "samp", "kbd", "math", "script", "style"
	};
	static const size_t skip_tags_count = 8;

	size_t tag, i = 0;

	while (i < size && text[i] != '>')
		i++;

	for (tag = 0; tag < skip_tags_count; ++tag) {
		if (sdhtml_is_tag(text, size, skip_tags[tag]) == HTML_TAG_OPEN)
			break;
	}

	if (tag < skip_tags_count) {
		for (;;) {
			while (i < size && text[i] != '<')
				i++;

			if (i == size)
				break;

			if (sdhtml_is_tag(text + i, size - i, skip_tags[tag]) == HTML_TAG_CLOSE)
				break;

			i++;
		}

		while (i < size && text[i] != '>')
			i++;
	}

	sd_bufput(ob, text, i + 1);
	return i;
}

static size_t
smartypants_cb__escape(struct sd_buf *ob, struct smartypants_data *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	if (size < 2)
		return 0;

	switch (text[1]) {
	case '\\':
	case '"':
	case '\'':
	case '.':
	case '-':
	case '`':
		sd_bufputc(ob, text[1]);
		return 1;

	default:
		sd_bufputc(ob, '\\');
		return 0;
	}
}

#if 0
static struct {
    uint8_t c0;
    const uint8_t *pattern;
    const uint8_t *entity;
    int skip;
} smartypants_subs[] = {
    { '\'', "'s>",      "&rsquo;",  0 },
    { '\'', "'t>",      "&rsquo;",  0 },
    { '\'', "'re>",     "&rsquo;",  0 },
    { '\'', "'ll>",     "&rsquo;",  0 },
    { '\'', "'ve>",     "&rsquo;",  0 },
    { '\'', "'m>",      "&rsquo;",  0 },
    { '\'', "'d>",      "&rsquo;",  0 },
    { '-',  "--",       "&mdash;",  1 },
    { '-',  "<->",      "&ndash;",  0 },
    { '.',  "...",      "&hellip;", 2 },
    { '.',  ". . .",    "&hellip;", 4 },
    { '(',  "(c)",      "&copy;",   2 },
    { '(',  "(r)",      "&reg;",    2 },
    { '(',  "(tm)",     "&trade;",  3 },
    { '3',  "<3/4>",    "&frac34;", 2 },
    { '3',  "<3/4ths>", "&frac34;", 2 },
    { '1',  "<1/2>",    "&frac12;", 2 },
    { '1',  "<1/4>",    "&frac14;", 2 },
    { '1',  "<1/4th>",  "&frac14;", 2 },
    { '&',  "&#0;",      0,       3 },
};
#endif

void
sdhtml_smartypants(struct sd_buf *ob, const uint8_t *text, size_t size)
{
	size_t i;
	struct smartypants_data smrt = {0, 0};

	if (!text)
		return;

	sd_bufgrow(ob, size);

	for (i = 0; i < size; ++i) {
		size_t org;
		uint8_t action = 0;

		org = i;
		while (i < size && (action = smartypants_cb_chars[text[i]]) == 0)
			i++;

		if (i > org)
			sd_bufput(ob, text + org, i - org);

		if (i < size) {
			i += smartypants_cb_ptrs[(int)action]
				(ob, &smrt, i ? text[i - 1] : 0, text + i, size - i);
		}
	}
}

//ENDREGION HTML_SMARTYPANTS.C

#endif // SD_IMPLEMENTATION

#endif // SD_NO_HTML

#ifdef __cplusplus
}
#endif

#endif // SUNDOWN_SINGLE_HEADER

/* vim: set filetype=c: */
