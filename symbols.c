#include "lancxasm.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

void *symbols = NULL;

static unsigned sym_max = 0;
static unsigned sym_count = 0;
static unsigned sym_col, sym_cols;

static int symbol_cmp_lancs(const void *a, const void *b)
{
	const struct symbol *sa = a;
	const struct symbol *sb = b;
	int res = strcmp(sa->name, sb->name);
	if (!res)
		res = sa->scope - sb->scope;
	return res;
}

int symbol_cmp_ade(const void *a, const void *b)
{
	const struct symbol *sa = a;
	const struct symbol *sb = b;
	int res = strncmp(sa->name, sb->name, 6);
	if (!res)
		res = sa->scope - sb->scope;
	return res;
}

int (*symbol_cmp)(const void *, const void *) = symbol_cmp_lancs;
struct symbol *(*symbol_enter)(struct inctx *inp, size_t label_size, int scope);

int symbol_parse(struct inctx *inp)
{
	int ch;
	do
		ch = *++inp->lineptr;
	while ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '$' || ch == '_');
	return ch;
}	

static void symbol_uppercase(const char *src, size_t label_size, char *dest)
{
	while (label_size--) {
		int ch = *src++;
		if (ch >= 'a' && ch <= 'z')
			ch &= 0xdf;
		*dest++ = ch;
	}
	*dest = 0;
}

struct symbol *symbol_enter_pass1(struct inctx *inp, size_t label_size, int scope)
{
	struct symbol *sym = malloc(sizeof(struct symbol) + label_size + 1);
	if (sym) {
		sym->scope = scope;
		sym->name = sym->name_str;
		symbol_uppercase(inp->line.str, label_size, sym->name_str);
		struct symbol **res = tsearch(sym, &symbols, symbol_cmp);
		if (!res)
			asm_error(inp, "out of memory allocating a symbol");
		else if (*res != sym) {
			asm_error(inp, "symbol %s already defined", sym->name);
			free(sym);
		}
		else {
			++sym_count;
			if (label_size > sym_max)
				sym_max = label_size;
			return sym;
		}
	}
	return NULL;
}

struct symbol *symbol_enter_pass2(struct inctx *inp, size_t label_size, int scope)
{
	char label[label_size+1];
	symbol_uppercase(inp->line.str, label_size, label);
	struct symbol sym;
	sym.scope = scope;
	sym.name = label;
	void *node = tfind(&sym, &symbols, symbol_cmp);
	if (node)
		return *(struct symbol **)node;
	else
		asm_error(inp, "symbol %s has disappeared between pass 1 and pass 2", label);
	return NULL;
}

uint16_t symbol_lookup(struct inctx *inp, bool no_undef)
{
	const char *lab_start = inp->lineptr;
	symbol_parse(inp);
	size_t lab_size = inp->lineptr - lab_start;
	char label[lab_size+1];
	symbol_uppercase(lab_start, lab_size, label);
	struct symbol sym;
	sym.scope = SCOPE_GLOBAL;
	sym.name = label;
	void *node = tfind(&sym, &symbols, symbol_cmp);
	if (node) {
		struct symbol *sym = *(struct symbol **)node;
		return sym->value;
	}
	if (no_undef)
		asm_error(inp, "symbol %s not found", label);
	return org;
}

static void print_one(const void *nodep, VISIT which, int depth)
{
	if (which == leaf || which == postorder) {
		const struct symbol *sym = *(const struct symbol **)nodep;
		if (++sym_col == sym_cols) {
			if (sym->scope == SCOPE_MACRO)
				fprintf(list_fp, "%-*s MACRO\n", sym_max, sym->name);
			else
				fprintf(list_fp, "%-*s &%04X\n", sym_max, sym->name, sym->value);
			sym_col = 0;
		}
		else {
			if (sym->scope == SCOPE_MACRO)
				fprintf(list_fp, "%-*s MACRO  ", sym_max, sym->name);
			else
				fprintf(list_fp, "%-*s &%04X  ", sym_max, sym->name, sym->value);
		}
	}
}


void symbol_print(void)
{
	if (sym_max == 0)
		fputs("\nNo symbols defined\n", list_fp);
	else {
		fprintf(list_fp, "\n%d symbols defined\n\n", sym_count);
		sym_cols = page_width / (sym_max + 8);
		sym_col = 0;
		twalk(symbols, print_one);
		if (sym_col)
			putc('\n', list_fp);
	}
}
