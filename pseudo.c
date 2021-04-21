#include "lancxasm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void pseudo_equ(struct inctx *inp, struct symbol *sym)
{
	if (sym) {
		uint16_t value = expression(inp, passno);
		sym->value = value;
		list_value = value;
	}
}

static void pseudo_org(struct inctx *inp, struct symbol *sym)
{
	list_value = org = expression(inp, true);
	if (sym)
		sym->value = org;
}

static void pseudo_asc(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch == '"' || ch == '\'') {
		int endq = ch;
		while ((ch = *++inp->lineptr) != endq && ch != '\n') {
			if (ch == '|' || ch == '^') {
				int ch2 = *++inp->lineptr;
				if (ch2 == endq) {
					asm_error(inp, "bad character sequence");
					break;
				}
				if (ch2 != ch) {
					if (ch == '|')
						ch = ch2 & 0x1f;
					else
						ch = ch2 | 0x80;
				}
			}
			dstr_add_ch(&objcode, ch);
		}
		if (ch != endq)
			asm_error(inp, "missing closing quote");
	}
	else
		asm_error(inp, "missing opening quote");
}

static void pseudo_str(struct inctx *inp, struct symbol *sym)
{
	pseudo_asc(inp, sym);
	dstr_add_ch(&objcode, '\r');
}

static void pseudo_casc(struct inctx *inp, struct symbol *sym)
{
	size_t posn = objcode.used;
	dstr_add_ch(&objcode, 0);
	pseudo_asc(inp, sym);
	objcode.str[posn] = objcode.used - posn;
}

static void pseudo_cstr(struct inctx *inp, struct symbol *sym)
{
	size_t posn = objcode.used;
	dstr_add_ch(&objcode, 0);
	pseudo_asc(inp, sym);
	dstr_add_ch(&objcode, '\r');
	objcode.str[posn] = objcode.used - posn;
}

static void plant_bytes(struct inctx *inp, size_t count, uint16_t byte)
{
	dstr_grow(&objcode, count);
	memset(objcode.str + objcode.used, byte, count);
	objcode.used += count;
}

static void plant_words(struct inctx *inp, size_t count, uint16_t word)
{
	dstr_grow(&objcode, count << 1);
	char high = word >> 8;
	while (count--) {
		objcode.str[objcode.used++] = word;
		objcode.str[objcode.used++] = high;
	}
}

static void plant_dbytes(struct inctx *inp, size_t count, uint16_t word)
{
	dstr_grow(&objcode, count << 1);
	char high = word >> 8;
	while (count--) {
		objcode.str[objcode.used++] = high;
		objcode.str[objcode.used++] = word;
	}
}

static void plant_item(struct inctx *inp, int ch, void (*planter)(struct inctx *inp, size_t count, uint16_t value))
{
	if (ch == '[') {
		++inp->lineptr;
		size_t count = expression(inp, true);
		ch = non_space(inp);
		if (ch == ']') {
			++inp->lineptr;
			planter(inp, count, expression(inp, passno));
		}
		else
			asm_error(inp, "missing ]");
	}
	else
		planter(inp, 1, expression(inp, passno));
}

static void plant_data(struct inctx *inp, const char *desc, void (*planter)(struct inctx *inp, size_t count, uint16_t value))
{
	int ch;
	do {
		plant_item(inp, non_space(inp), planter);
		ch = *inp->lineptr++;
	} while (ch == ',');
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "bad %s expression", desc);
}	

static void pseudo_dfb(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "byte", plant_bytes);
}

static void pseudo_dfw(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "word", plant_words);
}

static void pseudo_dfdb(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "double-byte", plant_dbytes);
}

static void pseudo_ds(struct inctx *inp, struct symbol *sym)
{
	plant_bytes(inp, expression(inp, true), 0);
}

static void pseudo_data(struct inctx *inp, struct symbol *sym)
{
	int ch;
	do {
		ch = non_space(inp);
		if (ch == '"' || ch == '\'') {
			pseudo_asc(inp, sym);
			++inp->lineptr;
		}
		else
			plant_item(inp, ch, plant_bytes);
		ch = *inp->lineptr++;
	} while (ch == ',');
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "bad %s expression", "data");
}	

static unsigned hex_nyb(struct inctx *inp, int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	asm_error(inp, "bad hex digit '%c'", ch);
	return 0;
}

static void pseudo_hex(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch == '"' || ch == '\'') {
		int endq = ch;
		while ((ch = *++inp->lineptr) != endq || ch == '\n') {
			unsigned byte = hex_nyb(inp, ch);
			ch = *++inp->lineptr;
			if (ch == endq || ch == '\n') {
				plant_bytes(inp, 1, byte << 4);
				break;
			}
			plant_bytes(inp, 1, (byte << 4) | hex_nyb(inp, ch));
		}
		if (ch != endq)
			asm_error(inp, "missing closing quote");
	}
	else
		asm_error(inp, "missing opening quote");
}

static void pseudo_clst(struct inctx *inp, struct symbol *sym)
{
	code_list_level = expression(inp, true);
}

static void pseudo_lst(struct inctx *inp, struct symbol *sym)
{
	src_list_level = expression(inp, true);
}

static void pseudo_dsect(struct inctx *inp, struct symbol *sym)
{
	if (in_dsect)
		asm_error(inp, "dsect cannot be nested");
	else {
		org_code = org;
		org = org_dsect;
		in_dsect = true;
	}
}

static void pseudo_dend(struct inctx *inp, struct symbol *sym)
{
	if (in_dsect) {
		org_dsect = org;
		org = org_code;
		in_dsect = false;
	}
	else
		asm_error(inp, "dend without dsect");
}

static FILE *parse_open(struct inctx *inp, struct dstring *fn, const char *mode)
{
	dstr_empty(fn, 20);
	int ch = non_space(inp);
	while (ch != '\n' && ch != ' ' && ch != '\t' && ch != 0xdd) {
		dstr_add_ch(fn, ch);
		ch = *++inp->lineptr;
	}
	dstr_add_ch(fn, 0);
	return fopen(fn->str, mode);
}

static void pseudo_chn(struct inctx *inp, struct symbol *sym)
{
	struct dstring filename;
	FILE *fp = parse_open(inp, &filename, "r");
	if (fp) {
		fclose(inp->fp);
		inp->fp = fp;
		inp->name = filename.str;
		asm_file(inp);
	}
	else
		asm_error(inp, "unable to open chained file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
	free(filename.str);
}

static void pseudo_include(struct inctx *inp, struct symbol *sym)
{
	struct dstring filename;
	FILE *fp = parse_open(inp, &filename, "r");
	if (fp) {
		struct inctx incfile;
		incfile.fp = fp;
		incfile.name = filename.str;
		incfile.whence = 'I';
		asm_file(&incfile);
	}
	else
		asm_error(inp, "unable to open include file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
	free(filename.str);
}

static void pseudo_code(struct inctx *inp, struct symbol *sym)
{
	struct dstring filename;
	FILE *fp = parse_open(inp, &filename, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		rewind(fp);
		dstr_grow(&objcode, size);
		if (fread(objcode.str, size, 1, fp) == 1)
			objcode.used = size;
		else
			asm_error(inp, "read error on code file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
		fclose(fp);
	}
	free(filename.str);
}

static void pseudo_query(struct inctx *inp, struct symbol *sym)
{
	if (!passno && !err_message) {
		int ch = non_space(inp) ;
		if (ch != '\n') {
			struct inctx qtx;
			qtx.name = "query";
			qtx.lineno = 0;
			qtx.line.str = NULL;
			qtx.line.allocated = 0;
			char *out_end = inp->line.str + inp->line.used;
			int c2 = *--out_end;
			while (c2 == ' ' || c2 == '\t' || c2 == 0xdd || c2 == '\n')
				c2 = *--out_end;
			if ((ch == '"' || ch == '\'') && c2 == ch) {
				++inp->lineptr;
				--out_end;
			}
			size_t outsize = out_end - inp->lineptr + 1;
			for (;;) {
				fwrite(inp->lineptr, outsize, 1, stdout);
				fputs("? ", stdout);
				fflush(stdout);
				ssize_t bytes = getline(&qtx.line.str, &qtx.line.allocated, stdin);
				if (bytes >= 0) {
					++qtx.lineno;
					if (bytes > 0) {
						qtx.line.used = bytes;
						qtx.lineptr = qtx.line.str;
						uint16_t value = expression(&qtx, true);
						if (err_message) {
							free(err_message);
							err_message = NULL;
						}
						else {
							sym->value = value;
							list_value = value;
							return;
						}
					}
				}
			}
		}
	}
}

struct op_type {
	char name[8];
	void (*func)(struct inctx *inp, struct symbol *sym);
};

static const struct op_type pseudo_ops[] = {
	{ "ASC",     pseudo_asc     },
	{ "CASC",    pseudo_casc    },
	{ "CHN",     pseudo_chn     },
	{ "CLST",    pseudo_clst    },
	{ "CODE",    pseudo_code    },
	{ "CSTR",    pseudo_cstr    },
	{ "DATA",    pseudo_data    },
	{ "DDB",     pseudo_dfdb    },
	{ "DEND",    pseudo_dend    },
	{ "DFB",     pseudo_dfb     },
	{ "DFW",     pseudo_dfw     },
	{ "DSECT",   pseudo_dsect   },
	{ "DS",      pseudo_ds      },
	{ "DW",      pseudo_dfw     },
	{ "DFDB",    pseudo_dfdb    },
	{ "EQU",     pseudo_equ     },
	{ "HEX",     pseudo_hex     },
	{ "INCLUDE", pseudo_include },
	{ "LST",     pseudo_lst     },
	{ "ORG",     pseudo_org     },
	{ "QUERY",   pseudo_query   },
	{ "STR",     pseudo_str     }
};

bool pseudo_op(struct inctx *inp, const char *opname, size_t opsize, struct symbol *sym)
{
	const struct op_type *ptr = pseudo_ops;
	const struct op_type *end = pseudo_ops + sizeof(pseudo_ops) / sizeof(struct op_type);
	while (ptr < end) {
		if (!strncmp(opname, ptr->name, opsize)) {
			ptr->func(inp, sym);
			return true;
		}
		++ptr;
	}
	return false;
}
