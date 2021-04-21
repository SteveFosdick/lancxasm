#define _GNU_SOURCE
#include "lancxasm.h"
#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <unistd.h>

static bool list_skip_cond = false;
static const char *list_filename = NULL;
static const char *obj_filename = NULL;
static unsigned err_count, err_column;
static unsigned cond_level;
static uint8_t cond_stack[32];

char *err_message = NULL;
FILE *list_fp = NULL;
FILE *obj_fp = NULL;
unsigned code_list_level = 1;
unsigned src_list_level = 0;
bool no_cmos = false;
unsigned passno = 0;
uint16_t org, org_code, org_dsect, list_value;
bool in_dsect, in_ds, codefile, cond_skipping;
struct dstring objcode;
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

static inline bool asm_isspace(int ch)
{
	return ch == ' ' || ch == '\t' || ch == 0xdd;
}

static inline bool asm_iscomment(int ch)
{
	return ch == ';' || ch == '\\' || ch == '*';
}

static inline bool asm_isendchar(int ch)
{
	return ch == '\n' || asm_isspace(ch) || asm_iscomment(ch);
}

int non_space(struct inctx *inp)
{
	int ch = *inp->lineptr;
	while (asm_isspace(ch))
		ch = *++inp->lineptr;
	return ch;
}

static void list_extra(struct inctx *inp)
{
	unsigned togo = objcode.used - 3;
	unsigned addr = org + 3;
	uint8_t *bytes = (uint8_t *)objcode.str + 3;
	while (togo >= 3) {
		fprintf(list_fp, "%c      %04X: %02X %02X %02X\n", inp->whence, addr, bytes[0], bytes[1], bytes[2]);
		togo -= 3;
		addr += 3;
		bytes += 3;
	}
	if (togo > 0) {
		fprintf(list_fp, "%c      %04X: ", inp->whence, addr);
		switch(togo) {
			case 1:
				fprintf(list_fp, "%02X\n", bytes[0]);
				break;
			case 2:
				fprintf(list_fp, "%02X %02X\n", bytes[0], bytes[1]);
				break;
			default:
				fprintf(list_fp, "%02X %02X %02X\n", bytes[0], bytes[1], bytes[2]);
				break;
		}
	}
}

static void list_line(struct inctx *inp)
{
	if (passno && list_fp) {
		if (src_list_level && (src_list_level >= 2 || inp->whence != 'M')) {
			fprintf(list_fp, "%c%5u %04X: ", inp->whence, inp->lineno, list_value);
			uint8_t *bytes = (uint8_t *)objcode.str;
			switch(objcode.used) {
				case 0:
					fputs("        ", list_fp);
					break;
				case 1:
					fprintf(list_fp, "%02X      ", bytes[0]);
					break;
				case 2:
					fprintf(list_fp, "%02X %02X   ", bytes[0], bytes[1]);
					break;
				default:
					fprintf(list_fp, "%02X %02X %02X", bytes[0], bytes[1], bytes[2]);
					break;
			}
			if (*inp->line.str == '\n')
				putc('\n', list_fp);
			else {
				putc(' ', list_fp);
				unsigned col = 0;
				const char *ptr = inp->line.str;
				size_t remain = inp->line.used;
				const char *tab = memchr(ptr, '\t', remain);
				while (tab) {
					size_t chars = tab - ptr;
					fwrite(ptr, chars, 1, list_fp);
					col += chars;
					int spaces = 8 - (col % 8);
					col += spaces;
					while (spaces--)
						putc(' ', list_fp);
					ptr = tab + 1;
					remain -= chars + 1;
					tab = memchr(ptr, '\t', remain);
				}
				if (remain > 0)
					fwrite(ptr, remain, 1, list_fp);
			}
		}
		if (err_message) {
			fprintf(list_fp, "+++ERROR at character %d: %s\n", err_column, err_message);
			free(err_message);
			err_message = NULL;
		}
		if (src_list_level && code_list_level >= 1 && objcode.used > 3 && (!codefile || code_list_level >= 2))
			list_extra(inp);
	}
	else if (err_message) {
		free(err_message);
		err_message = NULL;
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

static void asm_line(struct inctx *inp);

static void asm_macexpand(struct inctx *inp, struct symbol *mac)
{
	if (mac->scope == SCOPE_MACRO) {
		list_line(inp);
		struct inctx mtx;
		mtx.name = inp->name;
		mtx.lineno = inp->lineno;
		mtx.line.allocated = 0;
		mtx.whence = 'M';
		for (struct macline *ml = mac->macro; ml; ml = ml->next) {
			mtx.line.str = mtx.lineptr = ml->text;
			mtx.line.used = ml->length;
			asm_line(&mtx);
		}
	}
	else {
		asm_error(inp, "%s is a value, not a MACRO", mac->name);
		list_line(inp);
	}
}

static void asm_operation(struct inctx *inp, int ch, size_t label_size)
{
	struct symbol *sym = label_size ? symbol_enter(inp, label_size) : NULL;
	if (asm_isendchar(ch))
		list_line(inp);
	else {
		char *ptr = inp->lineptr;
		do
			ch = *++ptr;
		while (!asm_isendchar(ch));
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
		if (!strncmp(opname, "IF", opsize)) {
			if (cond_level == (sizeof(cond_stack)-1))
				asm_error(inp, "Too many levels of IF");
			else {
				cond_stack[cond_level++] = cond_skipping;
				if (!cond_skipping)
					cond_skipping = !expression(inp, true);
			}
			list_line(inp);
		}
		else if (!strncmp(opname, "ELSE", opsize)) {
			if (!cond_level)
				asm_error(inp, "ELSE without IF");
			else if (!cond_stack[cond_level-1])
				cond_skipping = !cond_skipping;
			list_line(inp);
		}
		else if (!strncmp(opname, "FI", opsize)) {
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

static void asm_line(struct inctx *inp)
{
	list_value = org;
	size_t label_size = 0;
	int ch = *inp->lineptr;
	/* parse any label */
	if (!asm_isspace(ch)) {
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
			ch = symbol_parse(inp);
			if (macsym)
				while (ch == '@')
					ch = symbol_parse(inp);
			label_size = inp->lineptr - inp->line.str;
			if (ch == ':')
				++inp->lineptr;
			else if (!asm_isendchar(ch)) {
				asm_error(inp, "invalid character in label");
				return;
			}
		}
		else if (!asm_isendchar(ch)) {
			asm_error(inp, "labels must start with a letter");
			return;
		}
	}
	ch = non_space(inp);

	if (macsym)
		asm_macdef(inp, ch, label_size);
	else
		asm_operation(inp, ch, label_size);

	if (objcode.used) {
		org += objcode.used;
		if (passno && obj_fp)
			fwrite(objcode.str, objcode.used, 1, obj_fp);
		objcode.used = 0;
	}
}

void asm_file(struct inctx *inp)
{
	inp->lineno = 0;
	ssize_t bytes;
	while ((bytes = getline(&inp->line.str, &inp->line.allocated, inp->fp)) >= 0) {
		++inp->lineno;
		inp->line.used = bytes;
		inp->lineptr = inp->line.str;
		asm_line(inp);
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

    for (int argno = optind; argno < argc; argno++) {
		const char *fn = argv[argno];
		inp->name = fn;
		if ((inp->fp = fopen(fn, "r"))) {
			inp->whence = ' ';
			asm_file(inp);
		}
		else {
			fprintf(stderr, "lancxasm: unable to open source file '%s': %s\n", fn, strerror(errno));
			err_count++;
		}
	}
	if (cond_level) {
		fprintf(stderr, "lancxasm: %u level(s) of IF still in-force (missing FI) at end of pass %u\n", cond_level, passno+1);
		err_count++;
	}
}

int main(int argc, char **argv)
{
    int opt, status = 0;
    while ((opt = getopt(argc, argv, "ac:f:l:o:rs")) != -1) {
        switch(opt) {
            case 'a':
                symbol_cmp = symbol_cmp_ade;
                break;
            case 'c':
                code_list_level = atoi(optarg);
                break;
            case 'f':
                list_filename = optarg;
                break;
            case 'l':
                src_list_level = atoi(optarg);
                break;
            case 'o':
                obj_filename = optarg;
                break;
            case 'r':
                no_cmos = true;
                break;
            case 's':
                list_skip_cond = true;
                break;
            default:
                status = 1;
        }
    }
    if (status == 0) {
		struct inctx infile;
		dstr_empty(&infile.line, MIN_LINE);
		dstr_empty(&objcode, MIN_LINE);
		if (list_filename && (list_fp = fopen(list_filename, "w")) == NULL) {
			fprintf(stderr, "lancxasm: unable to open listing file '%s': %s\n", list_filename, strerror(errno));
			status = 2;
		}
		else {
			if (obj_filename && (obj_fp = fopen(obj_filename, "wb")) == NULL) {
				fprintf(stderr, "lancxasm: unable to open listing file '%s': %s\n", list_filename, strerror(errno));
				status = 3;
			}
			else {
				symbol_enter = symbol_enter_pass1;
				asm_pass(argc, argv, &infile);
				if (err_count) {
					fprintf(stderr, "lancxasm: %u errors, on pass 1, pass 2 skipped\n", err_count);
					status = 4;
				}
				else {
					passno = 1;
					symbol_enter = symbol_enter_pass2;
					asm_pass(argc, argv, &infile);
					if (err_count) {
						fprintf(stderr, "lancxasm: %u errors, on pass 2\n", err_count);
						status = 5;
					}
					if (list_fp && src_list_level)
						symbol_print();
				}
			}
			if (obj_fp)
				fclose(obj_fp);
		}
		if (list_fp)
			fclose(list_fp);
	}
    else
        fputs("Usage: lancxasm [ -a ] [ -c level ] [ -f list-file ] [ -l level ] [ -o obj-file ] [ -r ] [ -s ] <file> [ ... ]\n", stderr);
    return status;
}
