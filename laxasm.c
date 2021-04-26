#define _GNU_SOURCE
#include "laxasm.h"
#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <unistd.h>

static const char *list_filename = NULL;
static const char *obj_filename = NULL;
static unsigned err_count, err_column, cond_level, mac_count, mac_no;
static bool swift_sym = false, mac_expand = false;
static uint8_t cond_stack[32];

char *err_message = NULL, list_char;
FILE *obj_fp = NULL, *list_fp = NULL;
unsigned passno, scope_no, list_opts = 0;
unsigned page_len = 66, page_width = 132, cur_page, cur_line, tab_stops[MAX_TAB_STOPS];
const unsigned default_tabs[MAX_TAB_STOPS] = { 8, 16, 25, 33, 41, 49, 57, 65, 73, 81, 89, 97, 115, 123 };
uint16_t org, org_code, org_dsect, list_value, load_addr = 0, exec_addr = 0, addr_msw = 0;
bool no_cmos = false, in_dsect, in_ds, codefile, cond_skipping;
struct dstring objcode, title;
struct symbol *macsym = NULL;

void asm_error(struct inctx *inp, const char *fmt, ...)
{
	if (!err_message) {
		va_list ap;
		++err_count;
		err_column = inp->lineptr - inp->line.str;
		va_start(ap, fmt);
		vasprintf(&err_message, fmt, ap);
		va_end(ap);
		fprintf(stderr, "%s:%u:%d: %s\n", inp->name, inp->lineno, err_column, err_message);
	}
}

#include "charclass.h"

int non_space(struct inctx *inp)
{
	int ch = *inp->lineptr;
	while (asm_isspace(ch))
		ch = *++inp->lineptr;
	return ch;
}

static const char page_hdr[] = "Lancaster/ADE cross-assembler  ";

static void list_header(struct inctx *inp)
{
	fwrite(page_hdr, sizeof(page_hdr)-1, 1, list_fp);
	if (title.used)
		fwrite(title.str, title.used, 1, list_fp);
	char pageno[14];
	int size = snprintf(pageno, sizeof(pageno), "Page: %u", ++cur_page);
	int spaces = page_width - sizeof(page_hdr) - title.used - size;
	while (spaces-- > 0)
		putc(' ', list_fp);
	fwrite(pageno, size, 1, list_fp);
	fprintf(list_fp, "\nFile: %s\n\n", inp->name);
	cur_line = 3;
}

static void list_pagecheck(struct inctx *inp)
{
	if (!(list_opts & LISTO_PAGE)) {
		if (cur_line++ == 0)
			list_header(inp);
		else if (cur_line >= page_len) {
			putc((list_opts & LISTO_FF) ? '\f' : '\n', list_fp);
			list_header(inp);
		}
	}
}

static void list_extra(struct inctx *inp)
{
	unsigned togo = objcode.used - 3;
	unsigned addr = org + 3;
	uint8_t *bytes = (uint8_t *)objcode.str + 3;
	while (togo >= 3) {
		list_pagecheck(inp);
		fprintf(list_fp, "%04X: %02X %02X %02X\n", addr, bytes[0], bytes[1], bytes[2]);
		togo -= 3;
		addr += 3;
		bytes += 3;
	}
	if (togo > 0) {
		list_pagecheck(inp);
		switch(togo) {
			case 1:
				fprintf(list_fp, "%04X: %02X\n", addr, bytes[0]);
				break;
			case 2:
				fprintf(list_fp, "%04X: %02X %02X\n", addr, bytes[0], bytes[1]);
				break;
			default:
				fprintf(list_fp, "%04X: %02X %02X %02X\n", addr, bytes[0], bytes[1], bytes[2]);
				break;
		}
	}
}

static void list_line(struct inctx *inp)
{
	if (passno && list_fp && !(cond_skipping && (list_opts & LISTO_SKIPPED))) {
		if (list_opts & LISTO_ENABLED && !(list_opts & LISTO_MACRO && inp->whence == 'M')) {
			list_pagecheck(inp);
			uint8_t *bytes = (uint8_t *)objcode.str;
			switch(objcode.used) {
				case 0:
					fprintf(list_fp, "%04X%c          ", list_value & 0xffff, list_char);
					break;
				case 1:
					fprintf(list_fp, "%04X%c %02X       ", list_value & 0xffff, list_char, bytes[0]);
					break;
				case 2:
					fprintf(list_fp, "%04X%c %02X %02X    ", list_value & 0xffff, list_char, bytes[0], bytes[1]);
					break;
				default:
					fprintf(list_fp, "%04X%c %02X %02X %02X ", list_value & 0xffff, list_char, bytes[0], bytes[1], bytes[2]);
					break;
			}
			putc(cond_skipping ? 'S' : inp->whence, list_fp);
			if (!(list_opts & LISTO_LINE))
				fprintf(list_fp, "%5u", inp->lineno);
			if (*inp->line.str == '\n')
				 putc('\n', list_fp);
			else {
				putc(' ', list_fp);
				putc(' ', list_fp);
				unsigned col = 1;
				const char *ptr = inp->line.str;
				size_t remain = inp->line.used;
				const char *tab = memchr(ptr, '\t', remain);
				if (tab) {
					int tab_no = 0;
					do {
						size_t chars = tab - ptr;
						fwrite(ptr, chars, 1, list_fp);
						col += chars;
						unsigned tab_posn = 0;
						while (tab_no < MAX_TAB_STOPS) {
							tab_posn = tab_stops[tab_no];
							if (tab_posn > col || tab_posn == 0)
								break;
							++tab_no;
						}
						if (tab_posn == 0) {
							putc(' ', list_fp);
							col++;
						}
						else {
							while (col < tab_posn) {
								putc(' ', list_fp);
								++col;
							}
							++tab_no;
						}
						ptr = tab + 1;
						remain -= chars + 1;
						tab = memchr(ptr, '\t', remain);
					}
					while (tab);
				}
				if (remain > 0)
					fwrite(ptr, remain, 1, list_fp);
			}
		}
		if (err_message) {
			list_pagecheck(inp);
			fprintf(list_fp, "+++ERROR at character %d: %s\n", err_column, err_message);
		}
		if (objcode.used > 3 && (list_opts & LISTO_ALLCODE) && (!codefile || (list_opts & LISTO_CODEFILE)))
			list_extra(inp);
	}
}

static void asm_macdef(struct inctx *inp, int ch, size_t label_size)
{
	/* defining a MACRO - check for the end marker */
	const char *p = inp->lineptr;
	if ((ch == 'E' || ch == 'e') && (p[1] == 'N' || p[1] == 'n') && (p[2] == 'D' || p[2] == 'd') && (p[3] == 'M' || p[3] == 'm')) {
		if (!passno) {
			/* put the lines back in the right order */
			struct macline *current = macsym->macro;
			struct macline *prev = NULL, *after = NULL;
			while (current != NULL) {
				after = current->next;
				current->next = prev;
				prev = current;
				current = after;
			}
			macsym->macro = prev;
		}
		macsym = NULL; /* no longer defining */
	}
	else if (!passno) { /* macros only defined on pass one */
		struct macline *ml = malloc(sizeof(struct macline) + inp->line.used);
		if (ml) {
			ml->next = macsym->macro;
			macsym->macro = ml;
			ml->length = inp->line.used;
			memcpy(ml->text, inp->line.str, inp->line.used);
		}
		else
			asm_error(inp, "out of memory defining macro %s", macsym->name);
	}
	list_line(inp);
}

static void asm_macparse(struct inctx *inp, char *params[10])
{
	int pno = 0, ch = non_space(inp);
	params[pno] = inp->lineptr - 1;
	while (!asm_isendchar(ch)) {
		if (ch == ',') {
			if (++pno == 9) {
				asm_error(inp, "too many macro arguments");
				break;
			}
			params[pno] = inp->lineptr;
		}
		ch = *++inp->lineptr;
	}
	while (pno < 9)
		params[++pno] = inp->lineptr;
}

static void asm_line(struct inctx *inp);

static void asm_macsubst(struct inctx *mtx, struct macline *ml, char *params[10], const char *at)
{
	const char *start = ml->text;
	const char *end = start + ml->length;
	mtx->line.used = 0;
	do {
		dstr_add_bytes(&mtx->line, start, at - start);
		int digit = *++at;
		if (digit == '0') {
			char count[6];
			size_t size = snprintf(count, sizeof(count), "%05d", mac_no);
			dstr_add_bytes(&mtx->line, count, size);
		}
		else if (digit >= '1' && digit <= '9') {
			digit -= '1';
			char *argb = params[digit]+1;
			char *arge = params[digit+1];
			if (arge > argb)
				dstr_add_bytes(&mtx->line, argb, arge - argb);
		}
		else
			asm_error(mtx, "invalid macro parameter @%c", digit);
		start = at + 1;
		at = memchr(start, '@', end - start);
	}
	while (at);
	if (start < end)
		dstr_add_bytes(&mtx->line, start, end - start);
	mtx->lineptr = mtx->line.str;
	asm_line(mtx);
}

static void asm_macexpand(struct inctx *inp, struct symbol *mac)
{
	if (mac->scope == SCOPE_MACRO) {
		unsigned save_mac_no = mac_no;
		bool save_mac_expand = mac_expand;
		mac_expand = true;
		mac_no = mac_count++;
		char *params[10];
		asm_macparse(inp, params);
		list_line(inp);

		/* Set up an input context for this macro */
		struct inctx mtx;
		mtx.parent = inp;
		mtx.fp = NULL;
		mtx.name = inp->name;
		mtx.lineno = inp->lineno;
		mtx.line.str = NULL;
		mtx.line.allocated = 0;
		mtx.whence = 'M';

		/* step through each line */
		for (struct macline *ml = mac->macro; ml; ml = ml->next) {
			/* does the line have args to be subsitited? */
			const char *at = memchr(ml->text, '@', ml->length);
			if (at)
				asm_macsubst(&mtx, ml, params, at);
			else {
				/* no, avoid a copy */
				char *save = mtx.line.str;
				mtx.line.str = mtx.lineptr = ml->text;
				mtx.line.used = ml->length;
				asm_line(&mtx);
				mtx.line.str = save;
			}
		}
		if (mtx.line.allocated)
			free(mtx.line.str);
		if (save_mac_expand)
			mac_no = save_mac_no;
		mac_expand = save_mac_expand;
	}
	else {
		asm_error(inp, "%s is a value, not a MACRO", mac->name);
		list_line(inp);
	}
}

#define IF_EXPR 0
#define IF_DEF  1
#define IF_NDEF 2

static void asm_if(struct inctx *inp, int iftype)
{
	if (cond_level == (sizeof(cond_stack)-1))
		asm_error(inp, "Too many levels of IF");
	else {
		cond_stack[cond_level++] = cond_skipping;
		if (!cond_skipping) {
			int value;
			if (iftype == IF_EXPR)
				value = expression(inp, true);
			else {
				if (symbol_lookup(inp, true) && iftype == IF_DEF)
					value = -1;
				else
					value = 0;
			}
			list_value = value;
			list_char = '=';
			cond_skipping = !value;
		}
	}
	list_line(inp);
}

static void asm_operation(struct inctx *inp, int ch, size_t label_size)
{
	if (asm_isendchar(ch))
		list_line(inp);
	else {
		char *ptr = inp->lineptr;
		do
			ch = *++ptr;
		while (!asm_isspace(ch) && !asm_isendchar(ch));
		size_t opsize = ptr - inp->lineptr;
		inp->lineptr = ptr;
		char opname[opsize+1], *nptr = opname + opsize;
		*nptr = 0;
		while (nptr > opname) {
			ch = *--ptr;
			if (ch >= 'a' && ch <= 'z')
				ch &= 0xdf;
			*--nptr = ch;
		}
		if (!cond_skipping && !strncmp(opname, "MACRO", opsize)) {
			if (macsym)
				asm_error(inp, "no nested MACROs, %s is being defined", macsym->name);
			else if (label_size) {
				if (*inp->lineptr == ':')
					asm_error(inp, "local scope MACROs not supported");
				else {
					struct symbol *sym = symbol_enter(inp, label_size, SCOPE_MACRO);
					if (sym) {
						macsym = sym;
						if (!passno)
							sym->macro = NULL;
					}
				}
			}
			else
				asm_error(inp, "a MACRO must have a label (name)");
			list_line(inp);
		}
		else {
			struct symbol *sym = NULL;
			if (!cond_skipping && label_size) {
				int scope = *inp->line.str == ':' ? scope_no : SCOPE_GLOBAL;
				if ((sym = symbol_enter(inp, label_size, scope)) && !passno)
					sym->value = org;
			}
			if (!strncmp(opname, "IF", opsize))
				asm_if(inp, IF_EXPR);
			else if (!strncmp(opname, "IFDEF", opsize))
				asm_if(inp, IF_DEF);
			else if (!strncmp(opname, "IFNDEF", opsize))
				asm_if(inp, IF_NDEF);
			else if (!strncmp(opname, "ELSE", opsize)) {
				if (!cond_level)
					asm_error(inp, "ELSE without IF");
				else if (!cond_stack[cond_level-1])
					cond_skipping = !cond_skipping;
				list_line(inp);
			}
			else if (!strncmp(opname, "FI", opsize) || !strncmp(opname, "FIN", opsize)) {
				if (!cond_level)
					asm_error(inp, "FI without IF");
				else
					cond_skipping = cond_stack[--cond_level];
				list_line(inp);
			}
			else if (cond_skipping || (opsize == 3 && m6502_op(inp, opname)) || pseudo_op(inp, opname, opsize, sym))
				list_line(inp);
			else {
				struct symbol sym;
				sym.scope = SCOPE_MACRO;
				sym.name = opname;
				struct symbol **node = tfind(&sym, &symbols, symbol_cmp);
				if (node)
					asm_macexpand(inp, *node);
				else {
					asm_error(inp, "unrecognised opcode '%.*s'", (int)opsize, opname);
					list_line(inp);
				}
			}
		}
	}
}

static void asm_line(struct inctx *inp)
{
	list_value = org;
	list_char = ':';
	size_t label_size = 0;
	int ch = *inp->lineptr;
	/* parse any label */
	if (!asm_isspace(ch)) {
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ':') {
			ch = symbol_parse(inp);
			if (macsym)
				while (ch == '@')
					ch = symbol_parse(inp);
			label_size = inp->lineptr - inp->line.str;
			if (ch == ':')
				++inp->lineptr;
			else if (!asm_isspace(ch) && !asm_isendchar(ch)) {
				asm_error(inp, "invalid character in label");
				return;
			}
		}
		else if (!asm_isspace(ch) && !asm_isendchar(ch)) {
			asm_error(inp, "labels must start with a letter");
			return;
		}
	}
	ch = non_space(inp);

	if (macsym)
		asm_macdef(inp, ch, label_size);
	else
		asm_operation(inp, ch, label_size);

	if (err_message) {
		free(err_message);
		err_message = NULL;
	}

	if (objcode.used) {
		org += objcode.used;
		if (passno && obj_fp && !in_dsect)
			fwrite(objcode.str, objcode.used, 1, obj_fp);
		objcode.used = 0;
	}
}

void asm_file(struct inctx *inp)
{
	inp->lineno = 1;
	inp->line.used = 0;
	/* Read the first line one character at a time to detect the
	 * line ending in use.
	 */
	int ch = getc(inp->fp);
	if (ch != EOF) {
		do {
			if (ch == '\r' || ch == '\n') {
				dstr_add_ch(&inp->line, '\n');
				break;
			}
			dstr_add_ch(&inp->line, ch);
			ch = getc(inp->fp);
		} while (ch != EOF);

		inp->lineptr = inp->line.str;
		asm_line(inp);
		if (ch != EOF) {
			/* Switch to line at a time with new delimiter */
			while (dstr_getdelim(&inp->line, ch, inp->fp) >= 0) {
				++inp->lineno;
				inp->lineptr = inp->line.str;
				asm_line(inp);
			}
		}
	}
	fclose(inp->fp);
}

static void asm_pass(int argc, char **argv, struct inctx *inp)
{
    org = 0;
    org_code = 0;
    org_dsect = 0;
    in_dsect = false;
    codefile = false;
    cond_skipping = false;
    cond_level = 0;
    mac_count = 0;
    scope_no = SCOPE_LOCAL;

    for (int argno = optind; argno < argc; argno++) {
		const char *fn = argv[argno];
		inp->name = fn;
		if ((inp->fp = fopen(fn, "r")))
			asm_file(inp);
		else {
			fprintf(stderr, "laxasm: unable to open source file '%s': %s\n", fn, strerror(errno));
			err_count++;
		}
	}
	if (cond_level) {
		fprintf(stderr, "laxasm: %u level(s) of IF still in-force (missing FI) at end of pass %u\n", cond_level, passno+1);
		err_count++;
	}
}

static const char hst_chars[] = "#$%&.?@^";
static const char bbc_chars[] = "?<;+/#=>";

int main(int argc, char **argv)
{
    int opt, status = 0;
    while ((opt = getopt(argc, argv, "adl:o:p:rw:ACFLMPST")) != -1) {
        switch(opt) {
            case 'a':
                symbol_cmp = symbol_cmp_ade;
                break;
            case 'd':
				swift_sym = true;
				break;
            case 'l':
                list_filename = optarg;
                list_opts |= LISTO_ENABLED;
                break;
            case 'o':
                obj_filename = optarg;
                break;
            case 'p':
				page_len = atoi(optarg);
				break;
            case 'r':
                no_cmos = true;
                break;
            case 'w':
				page_width = atoi(optarg);
				break;
			case 'A':
				list_opts |= LISTO_ALLCODE;
				break;
			case 'C':
				list_opts |= LISTO_CODEFILE;
				break;
			case 'F':
				list_opts |= LISTO_FF;
				break;
			case 'L':
				list_opts |= LISTO_LINE;
				break;
			case 'M':
				list_opts |= LISTO_MACRO;
				break;
			case 'P':
				list_opts |= LISTO_PAGE;
				break;
			case 'S':
				list_opts |= LISTO_SKIPPED;
				break;
			case 'T':
				list_opts |= LISTO_SYMTAB;
				break;
            default:
                status = 1;
        }
    }
    if (status == 0) {
		struct inctx infile;
		infile.parent = NULL;
		infile.whence = ' ';
		dstr_empty(&infile.line, MIN_LINE);
		dstr_empty(&objcode, MIN_LINE);
		if (list_filename && (list_fp = fopen(list_filename, "w")) == NULL) {
			fprintf(stderr, "laxasm: unable to open listing file '%s': %s\n", list_filename, strerror(errno));
			status = 2;
		}
		else {
			if (obj_filename && (obj_fp = fopen(obj_filename, "wb")) == NULL) {
				fprintf(stderr, "laxasm: unable to open object code file '%s': %s\n", list_filename, strerror(errno));
				status = 3;
			}
			else {
				memcpy(tab_stops, default_tabs, sizeof(tab_stops));
				symbol_enter = symbol_enter_pass1;
				asm_pass(argc, argv, &infile);
				if (err_count) {
					fprintf(stderr, "laxasm: %u errors, on pass 1, pass 2 skipped\n", err_count);
					status = 4;
				}
				else {
					passno = 1;
					symbol_enter = symbol_enter_pass2;
					asm_pass(argc, argv, &infile);
					if (err_count) {
						fprintf(stderr, "laxasm: %u errors, on pass 2\n", err_count);
						status = 5;
					}
					if (list_fp && !(list_opts & LISTO_SYMTAB))
						symbol_print();
					if (swift_sym)
						symbol_swift();
				}
			}
			if (obj_fp)
				fclose(obj_fp);
		}
		if (list_fp)
			fclose(list_fp);

		if (obj_filename && status == 0) {
			if (load_addr || exec_addr) {
				struct dstring inf_file;
				dstr_empty(&inf_file, 0);
				dstr_add_str(&inf_file, obj_filename);
				dstr_add_bytes(&inf_file, ".inf", 5);
				FILE *inf_fp = fopen(inf_file.str, "w");
				if (inf_fp) {
					uint32_t msw = addr_msw << 16;
					int ch;
					while ((ch = *obj_filename++)) {
						const char *ptr = strchr(hst_chars, ch);
						if (ptr)
							ch = bbc_chars[ptr - hst_chars];
						putc(ch, inf_fp);
					}
					fprintf(inf_fp, " %08X %08X\n", msw|load_addr, msw|exec_addr);
					fclose(inf_fp);
				}
				else {
					fprintf(stderr, "laxasm: unable to open INF file '%s': %s\n", inf_file.str, strerror(errno));
					status = 6;
				}
			}
		}
	}
    else
        fputs("Usage: laxasm [ -a ] [ -c level ] [ -f list-file ] [ -l level ] [ -o obj-file ] [ -r ] [ -s ] <file> [ ... ]\n", stderr);
    return status;
}
