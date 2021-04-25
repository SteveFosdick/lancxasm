#include "lancxasm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "charclass.h"

static void pseudo_equ(struct inctx *inp, struct symbol *sym)
{
	if (sym) {
		uint16_t value = expression(inp, passno);
		sym->value = value;
		list_value = value;
		list_char = '=';
	}
}

static void pseudo_org(struct inctx *inp, struct symbol *sym)
{
	list_value = org = expression(inp, true);
	list_char = ':';
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
		while ((ch = *++inp->lineptr) != endq && ch != '\n') {
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
		/* find the most local input context that is a file. */
		struct inctx *ctx = inp;
		while (ctx && !ctx->fp)
			ctx = ctx->parent;
		if (ctx) {
			fclose(ctx->fp);
			ctx->fp = fp;
			ctx->name = filename.str;
			ctx->lineno = 1;
		}
		else {
			fclose(fp);
			asm_error(inp, "failed to chain file, failed to find file-based context");
		}
	}
	else {
		asm_error(inp, "unable to open chained file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
		free(filename.str);
	}
}

static void pseudo_include(struct inctx *inp, struct symbol *sym)
{
	struct dstring filename;
	FILE *fp = parse_open(inp, &filename, "r");
	if (fp) {
		struct inctx incfile;
		incfile.parent = inp;
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
		if (fread(objcode.str, size, 1, fp) == 1) {
			objcode.used = size;
			codefile = true;
		}
		else
			asm_error(inp, "read error on code file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
		fclose(fp);
	}
	free(filename.str);
}

static const char *simple_str(struct inctx *inp, int ch)
{
	const char *end = inp->line.str + inp->line.used;
	int c2 = *--end;
	while (asm_isspace(c2) || c2 == '\n')
		c2 = *--end;
	if ((ch == '"' || ch == '\'') && c2 == ch) {
		++inp->lineptr;
		return end;
	}
	return end + 1;
}

static void pseudo_query(struct inctx *inp, struct symbol *sym)
{
	if (!passno && !err_message) {
		int ch = non_space(inp);
		if (ch != '\n') {
			struct inctx qtx;
			qtx.parent = inp;
			qtx.fp = NULL;
			qtx.name = "query";
			qtx.lineno = 0;
			qtx.line.str = NULL;
			qtx.line.allocated = 0;
			const char *out_end = simple_str(inp, ch);
			size_t outsize = out_end - inp->lineptr;
			for (;;) {
				fwrite(inp->lineptr, outsize, 1, stdout);
				fputs("? ", stdout);
				fflush(stdout);
				ssize_t bytes = dstr_getdelim(&qtx.line, '\n', stdin);
				if (bytes >= 0) {
					++qtx.lineno;
					if (bytes > 0) {
						qtx.lineptr = qtx.line.str;
						uint16_t value = expression(&qtx, true);
						if (err_message) {
							free(err_message);
							err_message = NULL;
						}
						else {
							sym->value = value;
							list_value = value;
							list_char = '=';
							return;
						}
					}
				}
			}
			if (qtx.line.allocated)
				free(qtx.line.str);
		}
	}
}

static void pseudo_endm(struct inctx *inp, struct symbol *sym)
{
	asm_error(inp, "no macro is being defined");
}

static void pseudo_lfcond(struct inctx *inp, struct symbol *sym)
{
	list_skip_cond = false;
}

static void pseudo_sfcond(struct inctx *inp, struct symbol *sym)
{
	list_skip_cond = true;
}

static void pseudo_page(struct inctx *inp, struct symbol *sym)
{
	page_len = expression(inp, true);
	int ch = non_space(inp);
	if (ch == ',') {
		++inp->lineptr;
		page_width = expression(inp, true);
	}
}

static void pseudo_skp(struct inctx *inp, struct symbol *sym)
{
	if (page_len) {
		int ch = non_space(inp);
		if (ch == 'H' || ch == 'h')
			cur_line = page_len;
		else
			cur_line += expression(inp, true);
	}
}

static void pseudo_ttl(struct inctx *inp, struct symbol *sym)
{
	const char *end = simple_str(inp, non_space(inp));
	title.used = 0;
	dstr_add_bytes(&title, inp->lineptr, end - inp->lineptr);
}

static void pseudo_width(struct inctx *inp, struct symbol *sym)
{
	page_width = expression(inp, true);
}

static void pseudo_disp(struct inctx *inp, struct symbol *sym)
{
	const char *end = simple_str(inp, non_space(inp));
	if (end > inp->lineptr) {
		const char *start = inp->lineptr;
		char *perc;
		while ((perc = memchr(start, '%', end - start)) && (end - perc) >= 4) {
			fwrite(start, perc - start, 1, stdout);
			const char *fmt;
			if ((perc[1] == 'D' || perc[1] == 'd') && perc[2] == '(')
				fmt = "%d";
			else if ((perc[1] == 'X' || perc[1] == 'x') && perc[2] == '(')
				fmt = "%x";
			else {
				putc('%', stdout);
				start = perc + 1;
				continue;
			}
			inp->lineptr = perc + 3;
			printf(fmt, expression(inp, true));
			start = inp->lineptr;
			if (*start != ')') {
				asm_error(inp, "bad expression in DISP");
				break;
			}
			++start;
		}
		if (end > start)
			fwrite(start, end - start, 1, stdout);
		putc('\n', stdout);
	}
}

static void pseudo_disp1(struct inctx *inp, struct symbol *sym)
{
	if (!passno)
		pseudo_disp(inp, sym);
}

static void pseudo_disp2(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		pseudo_disp(inp, sym);
}

static void pseudo_tabs(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch == '\n' || ch == ';' || ch == '\\' || ch == '*')
		memcpy(tab_stops, default_tabs, sizeof(tab_stops));
	else {
		int tab;
		for (tab = 0; tab < MAX_TAB_STOPS; ) {
			tab_stops[tab++] = expression(inp, true);
			ch = *inp->lineptr;
			if (ch != ',')
				break;
			++inp->lineptr;
		}
		if (ch == ',')
			asm_error(inp, "too many tab stops");
		else {
			while (tab < MAX_TAB_STOPS)
				tab_stops[tab++] = 0;
		}
	}
}

static void pseudo_load(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		load_addr = expression(inp, true);
}

static void pseudo_exec(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		exec_addr = expression(inp, true);
}

static void pseudo_msw(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		addr_msw = expression(inp, true);
}

static void pseudo_block(struct inctx *inp, struct symbol *sym)
{
	++scope_no;
}

struct op_type {
	char name[8];
	void (*func)(struct inctx *inp, struct symbol *sym);
};

static const struct op_type pseudo_ops[] = {
	{ "ASC",     pseudo_asc     },
	{ "BLOCK",   pseudo_block   },
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
	{ "DISP",    pseudo_disp    },
	{ "DISP1",   pseudo_disp1   },
	{ "DISP2",   pseudo_disp2   },
	{ "DSECT",   pseudo_dsect   },
	{ "DS",      pseudo_ds      },
	{ "DW",      pseudo_dfw     },
	{ "DFDB",    pseudo_dfdb    },
	{ "ENDM",    pseudo_endm    },
	{ "EQU",     pseudo_equ     },
	{ "EXEC",    pseudo_exec    },
	{ "HEX",     pseudo_hex     },
	{ "INCLUDE", pseudo_include },
	{ "INFO",    pseudo_disp2   },
	{ "LFCOND",  pseudo_lfcond  },
	{ "LOAD",    pseudo_load    },
	{ "LST",     pseudo_lst     },
	{ "MSW",     pseudo_msw     },
	{ "ORG",     pseudo_org     },
	{ "PAGE",    pseudo_page    },
	{ "QUERY",   pseudo_query   },
	{ "SFCOND",  pseudo_sfcond  },
	{ "SKP",     pseudo_skp     },
	{ "STR",     pseudo_str     },
	{ "TABS",    pseudo_tabs    },
	{ "TTL",     pseudo_ttl     },
	{ "WIDTH",   pseudo_width   }
};

bool pseudo_op(struct inctx *inp, const char *opname, size_t opsize, struct symbol *sym)
{
	const struct op_type *ptr = pseudo_ops;
	const struct op_type *end = pseudo_ops + sizeof(pseudo_ops) / sizeof(struct op_type);
	while (ptr < end) {
		if (!strcmp(opname, ptr->name)) {
			ptr->func(inp, sym);
			return true;
		}
		++ptr;
	}
	return false;
}
