#define _GNU_SOURCE
#include "lancxasm.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static bool list_skip_cond = false;
static const char *list_filename = NULL;
static const char *obj_filename = NULL;
static unsigned err_count, err_column;
static char *err_message = NULL;
static bool cond_skipping;
static unsigned cond_level;
static uint8_t cond_stack[32];

FILE *list_fp = NULL;
FILE *obj_fp = NULL;
unsigned code_list_level = 1;
unsigned src_list_level = 0;
bool no_cmos = false;
unsigned passno = 0;
uint16_t org, org_code, org_dsect, list_value;
bool in_dsect, in_ds, codefile;
struct dstring objcode;

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

int non_space(struct inctx *inp)
{
	int ch = *inp->lineptr;
	while (ch == ' ' || ch == '\t' || ch == 0xdd)
		ch = *++inp->lineptr;
	return ch;
}

static void asm_operation(struct inctx *inp, struct symbol *sym)
{
	int ch = non_space(inp);
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*') {
		char *ptr = inp->lineptr;
		do
			ch = *++ptr;
		while (ch != ' ' && ch != '\t' && ch != 0xdd && ch != '\n');
		size_t opsize = ptr - inp->lineptr;
		inp->lineptr = ptr;
		char opname[opsize], *nptr = opname + opsize;
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
		}
		else if (!strncmp(opname, "ELSE", opsize)) {
			if (!cond_level)
				asm_error(inp, "ELSE without IF");
			else if (!cond_stack[cond_level-1])
				cond_skipping = !cond_skipping;
		}
		else if (!strncmp(opname, "FI", opsize)) {
			if (!cond_level)
				asm_error(inp, "FI without IF");
			else
				cond_skipping = cond_stack[--cond_level];
		}
		else if (!cond_skipping) {
			if (opsize != 3 || !m6502_op(inp, opname))
				if (!pseudo_op(inp, opname, opsize, sym))
					asm_error(inp, "unrecognised opcode '%.*s'\n", opsize, opname);
		}
	}
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
			remain -= chars;
			tab = memchr(ptr, '\t', remain);
		}
		if (remain > 0)
			fwrite(ptr, remain, 1, list_fp);
	}
	if (err_message) {
		fprintf(list_fp, "+++ERROR at character %d: %s\n", err_column, err_message);
		free(err_message);
		err_message = NULL;
	}
	if (code_list_level >= 1 && objcode.used > 3 && (!codefile || code_list_level >= 2))
		list_extra(inp);
}

static void asm_line(struct inctx *inp)
{
	list_value = org;
	int ch = *inp->lineptr;
	if (ch >= 'a' && ch <= 'z')
		ch &= 0xdf;
	if (ch >= 'A' && ch <= 'Z')
		asm_operation(inp, symbol_enter(inp));
	else if (ch == ' ' || ch == '\t' || ch == 0xdd)
		asm_operation(inp, NULL);
	else if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "labels must start with a letter");
	if (passno && list_fp)
		list_line(inp);
	else if (err_message) {
		free(err_message);
		err_message = NULL;
	}
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
					if (list_fp)
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
