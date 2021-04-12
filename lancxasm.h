#ifndef LANCXASM_INC
#define LANCXASM_INC

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LINE_MAX 132

struct inctx {
	FILE *fp;
	const char *name;
	unsigned lineno;
	char *linebuf;
	char *lineptr;
	char *lineend;
	const char *errmsg;
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
extern uint16_t org, org_code, org_dsect;
extern bool in_dsect;
extern uint8_t objbytes[LINE_MAX];
extern unsigned objsize;
extern uint8_t *codefile;

extern void symbol_ade_mode(void);
extern struct symbol *symbol_enter(struct inctx *inp);
extern uint16_t symbol_lookup(struct inctx *inp);
extern void symbol_print(void);

extern void asm_error(struct inctx *inp, const char *msg);
extern void asm_file(struct inctx *inp);
extern int non_space(struct inctx *inp);
extern int expression(struct inctx *inp);
extern bool m6502_op(struct inctx *inp, const char *op);
extern bool pseudo_op(struct inctx *inp, const char *op, struct symbol *sym);

#endif
	
