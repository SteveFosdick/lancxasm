#ifndef LANCXASM_INC
#define LANCXASM_INC

#include "dstring.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MIN_LINE 132
#define MAX_TAB_STOPS 14

#define LISTO_PAGE     0x001
#define LISTO_LINE     0x002
#define LISTO_FF       0x004
#define LISTO_SYMTAB   0x008
#define LISTO_MACRO    0x010
#define LISTO_ALLCODE  0x020
#define LISTO_CODEFILE 0x040
#define LISTO_SKIPPED  0x080
#define LISTO_ENABLED  0x100

struct macline {
	struct macline *next;
	size_t length;
	char text[1];
};

struct inctx {
	struct dstring line;
	struct dstring wcond;
	union {
		fpos_t fposn;
		struct macline *mpos;
	};
	struct inctx *parent;
	FILE *fp;
	const char *name;
	char *lineptr;
	unsigned lineno;
	unsigned rpt_line;
	unsigned next_line;
	char whence;
	char wend_skipping;
};

enum action {
	ACT_CONTINUE,
	ACT_NOTFOUND,
	ACT_RMARK,
	ACT_RBACK,
	ACT_STOP
};

#define SCOPE_MACRO  0
#define SCOPE_GLOBAL 1
#define SCOPE_LOCAL  2

struct symbol {
	int  scope;
	char *name;
	union {
		uint16_t value;
		struct macline *macro;
	};
	char name_str[1];
};

/* laxasm.c */
extern char *err_message, list_char;
extern FILE *obj_fp, *list_fp;
extern unsigned list_opts, passno, scope_no;
extern unsigned page_len, page_width, cur_page, cur_line, tab_stops[MAX_TAB_STOPS];
extern const unsigned default_tabs[MAX_TAB_STOPS];
extern uint16_t org, org_code, org_dsect, list_value, load_addr, exec_addr, addr_msw;
extern bool no_cmos, in_dsect, in_ds, codefile, cond_skipping;
extern struct dstring objcode, title;
extern struct symbol *macsym;

__attribute__((format (printf, 2, 3)))
extern void asm_error(struct inctx *inp, const char *fmt, ...);
extern void list_line(struct inctx *inp);
extern enum action asm_file(struct inctx *inp);
extern int non_space(struct inctx *inp);
extern void dump_ictx(struct inctx *inp, const char *when);

/* symbols.c */
extern void *symbols;
extern int (*symbol_cmp)(const void *, const void *);
extern int symbol_cmp_ade(const void *a, const void *b);
extern int symbol_parse(struct inctx *inp);
extern struct symbol *(*symbol_enter)(struct inctx *inp, size_t label_size, int scope, bool replace);
extern struct symbol *symbol_enter_pass1(struct inctx *inp, size_t label_size, int scope, bool replace);
extern struct symbol *symbol_enter_pass2(struct inctx *inp, size_t label_size, int scope, bool replace);
extern struct symbol *symbol_lookup(struct inctx *inp, bool no_undef);
//extern struct symbol *symbol_macfind(char *opname);
extern void symbol_print(void);
extern void symbol_swift(void);

/* expression.c */
extern int expression(struct inctx *inp, bool no_undef);

/* m6502.c */
extern bool m6502_op(struct inctx *inp, const char *opname);

/* pseudo.c */
extern enum action pseudo_op(struct inctx *inp, const char *opname, size_t opsize, struct symbol *sym);
extern enum action pseudo_include(struct inctx *inp);

#endif

