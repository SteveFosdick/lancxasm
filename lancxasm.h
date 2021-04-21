#ifndef LANCXASM_INC
#define LANCXASM_INC

#include "dstring.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MIN_LINE 132

struct inctx {
	FILE *fp;
	const char *name;
	unsigned lineno;
	struct dstring line;
	char *lineptr;
	char whence;
};

struct macline {
	struct macline *next;
	size_t length;
	char text[1];
};

#define SCOPE_MACRO  0
#define SCOPE_GLOBAL 1

struct symbol {
	unsigned scope;
	char *name;
	union {
		int_least16_t value;
		struct macline *macro;
	};
	char name_str[1];
};

/* lancxasm.c */
extern char *err_message;
extern FILE *obj_fp, *list_fp;
extern unsigned code_list_level, src_list_level, passno;
extern uint16_t org, org_code, org_dsect, list_value;
extern bool no_cmos, list_skip_cond, in_dsect, in_ds, codefile, cond_skipping;
extern struct dstring objcode;
extern struct symbol *macsym;

__attribute__((format (printf, 2, 3)))
extern void asm_error(struct inctx *inp, const char *fmt, ...);
extern void asm_file(struct inctx *inp);
extern int non_space(struct inctx *inp);

/* symbols.c */
extern void *symbols;
extern int (*symbol_cmp)(const void *, const void *);
extern int symbol_cmp_ade(const void *a, const void *b);
extern int symbol_parse(struct inctx *inp);
extern struct symbol *(*symbol_enter)(struct inctx *inp, size_t label_size);
extern struct symbol *symbol_enter_pass1(struct inctx *inp, size_t label_size);
extern struct symbol *symbol_enter_pass2(struct inctx *inp, size_t label_size);
extern uint16_t symbol_lookup(struct inctx *inp, bool no_undef);
extern struct symbol *symbol_macfind(char *opname);
extern void symbol_print(void);

/* expression.c */
extern int expression(struct inctx *inp, bool no_undef);

/* m6502.c */
extern bool m6502_op(struct inctx *inp, const char *opname);

/* pseudo.c */
extern bool pseudo_op(struct inctx *inp, const char *opname, size_t opsize, struct symbol *sym);

#endif
	
