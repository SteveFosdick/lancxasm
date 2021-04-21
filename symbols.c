#include "lancxasm.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

static void *symbols = NULL;
static unsigned sym_max = 0;
static unsigned sym_count = 0;
static unsigned sym_col, sym_cols;

static int symbol_cmp_lancs(const void *a, const void *b)
{
	const struct symbol *sa = a;
	const struct symbol *sb = b;
	return strcmp(sa->name, sb->name);
}

int symbol_cmp_ade(const void *a, const void *b)
{
	const struct symbol *sa = a;
	const struct symbol *sb = b;
	return strncmp(sa->name, sb->name, 6);
}

int (*symbol_cmp)(const void *, const void *) = symbol_cmp_lancs;
struct symbol *(*symbol_enter)(struct inctx *inp);

static int symbol_parse(struct inctx *inp)
{
	int ch;

	do
		ch = *++inp->lineptr;
	while ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '$' || ch == '_');

	if (ch == ' ' || ch == '\t' || ch == 0xdd || ch == '\n' || ch == ':')
		return ch;
	return 0;
}

static void symbol_uppercase(const char *src, const char *end, char *dest)
{
	while (src < end) {
		int ch = *src++;
		if (ch >= 'a' && ch <= 'z')
			ch &= 0xdf;
		*dest++ = ch;
	}
	*dest = 0;
}

struct symbol *symbol_enter_pass1(struct inctx *inp)
{
	const char *lab_start = inp->lineptr;
	int tch = symbol_parse(inp);
	if (tch) {
		if (!cond_skipping) {
			const char *lab_end = inp->lineptr;
			size_t lab_size = lab_end - lab_start;
			struct symbol *sym = malloc(sizeof(struct symbol) + lab_size + 1);
			if (sym) {
				symbol_uppercase(lab_start, lab_end, sym->name);
				struct symbol **res = tsearch(sym, &symbols, symbol_cmp);
				if (!res)
					asm_error(inp, "out of memory allocating a symbol");
				else if (*res != sym) {
					asm_error(inp, "symbol %s already defined", sym->name);
					free(sym);
				}
				else {
					sym->value = org;
					++sym_count;
					if (lab_size > sym_max)
						sym_max = lab_size;
					if (tch == ':')
						++inp->lineptr;
					return sym;
				}
			}
			else
				asm_error(inp, "out of memory allocating a symbol");
		}
	}
	else
		asm_error(inp, "invalid character in label");
	return NULL;
}

struct symbol *symbol_enter_pass2(struct inctx *inp)
{
	const char *lab_start = inp->lineptr;
	int tch = symbol_parse(inp);
	if (tch) {
		if (!cond_skipping) {
			const char *lab_end = inp->lineptr;
			size_t lab_size = lab_end - lab_start;
			char label[lab_size+1];
			symbol_uppercase(lab_start, lab_end, label);
			struct symbol *sym = (struct symbol *)(label - offsetof(struct symbol, name));
			void *node = tfind(sym, &symbols, symbol_cmp);
			if (tch == ':')
				++inp->lineptr;
			if (node)
				return *(struct symbol **)node;
			else
				asm_error(inp, "symbol %s has disappeared between pass 1 and pass 2", label);
		}
	}
	else
		asm_error(inp, "invalid character in label");
	return NULL;
}

uint16_t symbol_lookup(struct inctx *inp, bool no_undef)
{
	const char *lab_start = inp->lineptr;
	symbol_parse(inp);
	const char *lab_end = inp->lineptr;
	size_t lab_size = lab_end - lab_start;
	char label[lab_size+1];
	symbol_uppercase(lab_start, lab_end, label);
	struct symbol *sym = (struct symbol *)(label - offsetof(struct symbol, name));
	void *node = tfind(sym, &symbols, symbol_cmp);
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
			fprintf(list_fp, "%-*s &%04X\n", sym_max, sym->name, sym->value);
			sym_col = 0;
		}
		else
			fprintf(list_fp, "%-*s &%04X  ", sym_max, sym->name, sym->value);
	}
}


void symbol_print(void)
{
	if (sym_max == 0)
		fputs("\nNo symbols defined\n", list_fp);
	else {
		fprintf(list_fp, "\n%d symbols defined\n\n", sym_count);
		sym_cols = MIN_LINE / (sym_max + 8);
		sym_col = 0;
		twalk(symbols, print_one);
		if (sym_col)
			putc('\n', list_fp);
	}
}

