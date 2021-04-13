#ifndef LANCXASM_INC
#define LANCXASM_INC

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LINE_MAX 132

struct inctx {
	FILE *fp;
	const char *name;
	unsigned lineno;
	char *linebuf;
	char *lineptr;
	char *lineend;
	char whence;
};

struct symbol {
	uint16_t value;
	char name[LINE_MAX];
};

extern FILE *obj_fp;
extern FILE *list_fp;
extern unsigned code_list_level;
extern unsigned src_list_level;
extern bool no_cmos;
extern unsigned passno;
extern uint16_t org, org_code, org_dsect, list_value;
extern bool in_dsect, in_ds;
extern uint8_t *objbytes;
extern unsigned objalloc, objsize;

/* symbols.c */
extern int (*symbol_cmp)(const void *, const void *);
extern int symbol_cmp_ade(const void *a, const void *b);
extern struct symbol *(*symbol_enter)(struct inctx *inp);
extern struct symbol *symbol_enter_pass1(struct inctx *inp);
extern struct symbol *symbol_enter_pass2(struct inctx *inp);
extern uint16_t symbol_lookup(struct inctx *inp, bool no_undef);
extern void symbol_print(void);

/* lancxasm.c */
extern void asm_error(struct inctx *inp, const char *fmt, ...);
extern void asm_file(struct inctx *inp);
extern int non_space(struct inctx *inp);

/* expression.c */
extern int expression(struct inctx *inp, bool no_undef);

/* m6502.c */
extern bool m6502_op(struct inctx *inp, const char *op);

/* pseudo.c */
extern bool pseudo_op(struct inctx *inp, const char *op, struct symbol *sym);

#endif
	
