#include "laxasm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "charclass.h"

static enum action pseudo_equ(struct inctx *inp, struct symbol *sym)
{
	if (sym) {
		uint16_t value = expression(inp, passno);
		sym->value = value;
		list_value = value;
		list_char = '=';
	}
	return ACT_CONTINUE;
}

static enum action pseudo_org(struct inctx *inp, struct symbol *sym)
{
	list_value = org = expression(inp, true);
	list_char = ':';
	if (sym)
		sym->value = org;
	return ACT_CONTINUE;
}

static enum action pseudo_asc(struct inctx *inp, struct symbol *sym)
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
	return ACT_CONTINUE;
}

static enum action pseudo_str(struct inctx *inp, struct symbol *sym)
{
	pseudo_asc(inp, sym);
	dstr_add_ch(&objcode, '\r');
	return ACT_CONTINUE;
}

static enum action pseudo_dc(struct inctx *inp, struct symbol *sym)
{
	size_t used = objcode.used;
	pseudo_asc(inp, sym);
	if (objcode.used > used)
		objcode.str[objcode.used-1] |= 0x80;
	return ACT_CONTINUE;
}

static void plant_length(struct inctx *inp, size_t posn)
{
	size_t len = objcode.used - posn;
	if (len > 0xff)
		asm_error(inp, "string too long for single-byte count");
	objcode.str[posn] = len;
}

static enum action pseudo_casc(struct inctx *inp, struct symbol *sym)
{
	size_t posn = objcode.used;
	dstr_add_ch(&objcode, 0); /* length to be filled in later */
	pseudo_asc(inp, sym);
	plant_length(inp, posn);
	return ACT_CONTINUE;
}

static enum action pseudo_cstr(struct inctx *inp, struct symbol *sym)
{
	size_t posn = objcode.used;
	dstr_add_ch(&objcode, 0); /* length to be filled in later */
	pseudo_asc(inp, sym);
	dstr_add_ch(&objcode, '\r');
	plant_length(inp, posn);
	return ACT_CONTINUE;
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

static enum action pseudo_dfb(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "byte", plant_bytes);
	return ACT_CONTINUE;
}

static enum action pseudo_dfw(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "word", plant_words);
	return ACT_CONTINUE;
}

static enum action pseudo_dfdb(struct inctx *inp, struct symbol *sym)
{
	plant_data(inp, "double-byte", plant_dbytes);
	return ACT_CONTINUE;
}

static enum action pseudo_ds(struct inctx *inp, struct symbol *sym)
{
	plant_bytes(inp, expression(inp, true), 0);
	return ACT_CONTINUE;
}

static enum action pseudo_data(struct inctx *inp, struct symbol *sym)
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
	return ACT_CONTINUE;
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

static enum action pseudo_hex(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch == '"' || ch == '\'') {
		int endq = ch;
		while ((ch = *++inp->lineptr) != endq && ch != '\n') {
			unsigned byte = hex_nyb(inp, ch);
			if (err_message)
				break;
			ch = *++inp->lineptr;
			if (ch == endq || ch == '\n') {
				plant_bytes(inp, 1, byte << 4);
				break;
			}
			byte = (byte << 4) | hex_nyb(inp, ch);
			if (err_message)
				break;
			plant_bytes(inp, 1, byte);
		}
		if (ch != endq)
			asm_error(inp, "missing closing quote");
	}
	else
		asm_error(inp, "missing opening quote");
	return ACT_CONTINUE;
}

static enum action pseudo_clst(struct inctx *inp, struct symbol *sym)
{
	switch(expression(inp, true)) {
		case 0:
			list_opts &= ~(LISTO_ALLCODE|LISTO_CODEFILE);
			break;
		default:
			list_opts = (list_opts & ~LISTO_CODEFILE)|LISTO_ALLCODE;
			break;
		case 2:
			list_opts |= LISTO_ALLCODE|LISTO_CODEFILE;
	}
	return ACT_CONTINUE;
}

static enum action pseudo_lst(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch == 'O' || ch == 'o') {
		ch = inp->lineptr[1];
		if (ch == 'N' || ch == 'n') {
			/* LST ON */
			list_opts |= LISTO_ENABLED|LISTO_MACRO;
			return ACT_CONTINUE;
		}
		else if (ch == 'F' || ch == 'f') {
			ch = inp->lineptr[2];
			if (ch == 'F' || ch == 'f') {
				/* LST OFF */
				list_opts &= ~LISTO_ENABLED;
				return ACT_CONTINUE;
			}
		}
	}
	else if (ch == 'F' || ch == 'f') {
		ch = inp->lineptr[1];
		if (ch == 'U' || ch == 'u') {
			ch = inp->lineptr[2];
			if (ch == 'L' || ch == 'l') {
				ch = inp->lineptr[3];
				if (ch == 'L' || ch == 'l') {
					/* LST FULL */
					list_opts = (list_opts & ~LISTO_MACRO)|LISTO_ENABLED;
					return ACT_CONTINUE;
				}
			}
		}
	}
	else {
		switch(expression(inp, true)) {
			case 0:
				/* Equivalent to OFF */
				list_opts &= ~LISTO_ENABLED;
				break;
			case 1:
				/* Equivalent to ON */
				list_opts |= LISTO_ENABLED|LISTO_MACRO;
				break;
			default:
				/* Equivalent to FULL */
				list_opts = (list_opts & ~LISTO_MACRO)|LISTO_ENABLED;
		}
	}
	return ACT_CONTINUE;
}

static enum action pseudo_listo(struct inctx *inp, struct symbol *sym)
{
	if (passno ) {
		int value = expression(inp, true);
		if (!err_message)
			list_opts ^= value & 0x1ff;
	}
	return ACT_CONTINUE;
}

static enum action pseudo_dsect(struct inctx *inp, struct symbol *sym)
{
	if (in_dsect)
		asm_error(inp, "dsect cannot be nested");
	else {
		org_code = org;
		org = org_dsect;
		in_dsect = true;
	}
	return ACT_CONTINUE;
}

static enum action pseudo_dend(struct inctx *inp, struct symbol *sym)
{
	if (in_dsect) {
		org_dsect = org;
		org = org_code;
		in_dsect = false;
	}
	else
		asm_error(inp, "dend without dsect");
	return ACT_CONTINUE;
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

static enum action pseudo_chn(struct inctx *inp, struct symbol *sym)
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
			return ACT_CONTINUE;
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
	return ACT_STOP;
}

enum action pseudo_include(struct inctx *inp)
{
	enum action act;
	struct dstring filename;
	FILE *fp = parse_open(inp, &filename, "r");
	if (fp) {
		struct inctx incfile;
		dstr_empty(&incfile.line, MIN_LINE);
		dstr_empty(&incfile.wcond, 0);
		incfile.parent = inp;
		incfile.fp = fp;
		incfile.name = filename.str;
		incfile.whence = 'I';
		list_line(inp);
		act = asm_file(&incfile);
		if (incfile.line.allocated)
			free(incfile.line.str);
		if (incfile.wcond.allocated)
			free(incfile.wcond.str);
	}
	else {
		asm_error(inp, "unable to open include file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
		list_line(inp);
		act = ACT_STOP;
	}
	free(filename.str);
	return act;
}

static enum action pseudo_code(struct inctx *inp, struct symbol *sym)
{
	enum action act = ACT_CONTINUE;
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
		else {
			asm_error(inp, "read error on code file %.*s: %s", (int)filename.used, filename.str, strerror(errno));
			act = ACT_STOP;
		}
		fclose(fp);
	}
	free(filename.str);
	return act;
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

static enum action pseudo_query(struct inctx *inp, struct symbol *sym)
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
							return ACT_CONTINUE;
						}
					}
				}
			}
			if (qtx.line.allocated)
				free(qtx.line.str);
		}
	}
	return ACT_CONTINUE;
}

static enum action pseudo_endm(struct inctx *inp, struct symbol *sym)
{
	asm_error(inp, "no macro is being defined");
	return ACT_CONTINUE;
}

static enum action pseudo_lfcond(struct inctx *inp, struct symbol *sym)
{
	list_opts &= ~LISTO_SKIPPED;
	return ACT_CONTINUE;
}

static enum action pseudo_sfcond(struct inctx *inp, struct symbol *sym)
{
	list_opts |= LISTO_SKIPPED;
	return ACT_CONTINUE;
}

static enum action pseudo_page(struct inctx *inp, struct symbol *sym)
{
	page_len = expression(inp, true);
	int ch = non_space(inp);
	if (ch == ',') {
		++inp->lineptr;
		page_width = expression(inp, true);
	}
	return ACT_CONTINUE;
}

static enum action pseudo_skp(struct inctx *inp, struct symbol *sym)
{
	if (page_len) {
		int ch = non_space(inp);
		if (ch == 'H' || ch == 'h')
			cur_line = page_len;
		else
			cur_line += expression(inp, true);
	}
	return ACT_CONTINUE;
}

static enum action pseudo_ttl(struct inctx *inp, struct symbol *sym)
{
	const char *end = simple_str(inp, non_space(inp));
	title.used = 0;
	dstr_add_bytes(&title, inp->lineptr, end - inp->lineptr);
	return ACT_CONTINUE;
}

static enum action pseudo_width(struct inctx *inp, struct symbol *sym)
{
	page_width = expression(inp, true);
	return ACT_CONTINUE;
}

static enum action pseudo_disp(struct inctx *inp, struct symbol *sym)
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
	return ACT_CONTINUE;
}

static enum action pseudo_disp1(struct inctx *inp, struct symbol *sym)
{
	if (!passno)
		pseudo_disp(inp, sym);
	return ACT_CONTINUE;
}

static enum action pseudo_disp2(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		pseudo_disp(inp, sym);
	return ACT_CONTINUE;
}

static enum action pseudo_tabs(struct inctx *inp, struct symbol *sym)
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
	return ACT_CONTINUE;
}

static enum action pseudo_load(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		load_addr = expression(inp, true);
	return ACT_CONTINUE;
}

static enum action pseudo_exec(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		exec_addr = expression(inp, true);
	return ACT_CONTINUE;
}

static enum action pseudo_msw(struct inctx *inp, struct symbol *sym)
{
	if (passno)
		addr_msw = expression(inp, true);
	return ACT_CONTINUE;
}

static enum action pseudo_block(struct inctx *inp, struct symbol *sym)
{
	++scope_no;
	return ACT_CONTINUE;
}

static enum action pseudo_repeat(struct inctx *inp, struct symbol *sym)
{
	if (inp->rpt_line) {
		asm_error(inp, "REPEAT or WHILE already active");
		return ACT_CONTINUE;
	}
	inp->rpt_line = inp->lineno;
	return ACT_RMARK;
}

static enum action pseudo_until(struct inctx *inp, struct symbol *sym)
{
	if (inp->rpt_line) {
		if (inp->wcond.used)
			asm_error(inp, "Expected WEND, to match WHILE, not UNTIL");
		else {
			int value = expression(inp, true);
			if (!value && !err_message)
				return ACT_RBACK;
			inp->rpt_line = 0;
		}
	}
	else
		asm_error(inp, "UNTIL without REPEAT");
	return ACT_CONTINUE;
}

static enum action pseudo_while(struct inctx *inp, struct symbol *sym)
{
	if (inp->rpt_line)
		asm_error(inp, "REPEAT or WHILE already active");
	else {
		non_space(inp);
		const char *start = inp->lineptr;
		int value = expression(inp, true);
		if (!value || err_message)
			inp->wend_skipping = true;
		else {
			dstr_add_bytes(&inp->wcond, start, inp->lineptr - start);
			inp->rpt_line = inp->lineno;
			return ACT_RMARK;
		}
	}
	return ACT_CONTINUE;
}

static enum action pseudo_stop(struct inctx *inp, struct symbol *sym)
{
	const char *end = simple_str(inp, non_space(inp));
	asm_error(inp, "STOP: %.*s", (int)(end - inp->lineptr), inp->lineptr);
	return ACT_STOP;
}

static enum action pseudo_assign(struct inctx *inp, struct symbol *sym)
{
	/* the case with a label is handled in laxasm.c */
	asm_error(inp, "Assignment (=) needs a label");
	return ACT_CONTINUE;
}

struct op_type {
	char name[8];
	enum action (*func)(struct inctx *inp, struct symbol *sym);
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
	{ "DB",      pseudo_dfb     },
	{ "DC",      pseudo_dc      },
	{ "DDB",     pseudo_dfdb    },
	{ "DEND",    pseudo_dend    },
	{ "DFB",     pseudo_dfb     },
	{ "DFS",     pseudo_ds      },
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
	{ "INFO",    pseudo_disp2   },
	{ "LFCOND",  pseudo_lfcond  },
	{ "LISTO",   pseudo_listo   },
	{ "LOAD",    pseudo_load    },
	{ "LST",     pseudo_lst     },
	{ "MSW",     pseudo_msw     },
	{ "ORG",     pseudo_org     },
	{ "PAGE",    pseudo_page    },
	{ "QUERY",   pseudo_query   },
	{ "REPEAT",  pseudo_repeat  },
	{ "SFCOND",  pseudo_sfcond  },
	{ "SKP",     pseudo_skp     },
	{ "STOP",    pseudo_stop    },
	{ "STR",     pseudo_str     },
	{ "TABS",    pseudo_tabs    },
	{ "TTL",     pseudo_ttl     },
	{ "UNTIL",   pseudo_until   },
	{ "WIDTH",   pseudo_width   },
	{ "WHILE",   pseudo_while   },
	{ "=",       pseudo_assign  }
};

enum action pseudo_op(struct inctx *inp, const char *opname, size_t opsize, struct symbol *sym)
{
	const struct op_type *ptr = pseudo_ops;
	const struct op_type *end = pseudo_ops + sizeof(pseudo_ops) / sizeof(struct op_type);
	while (ptr < end) {
		if (!strcmp(opname, ptr->name))
			return ptr->func(inp, sym);
		++ptr;
	}
	if (!strncmp(opname, "SYS", 3)) {
		const char *tail = opname + 3;
		if (!strcmp(tail, "CLI") || !strcmp(tail, "FX") || !strcmp(tail, "VDU") || !strcmp(tail, "VDU1") || !strcmp(tail, "VDU2")) {
			if (!passno)
				fprintf(stderr, "%s:%u:%d: warning: directive %s ignored\n", inp->name, inp->lineno, (int)(inp->lineptr - inp->line.str), opname);
			return ACT_CONTINUE;
		}
	}
	return ACT_NOTFOUND;
}
