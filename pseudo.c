#include "lancxasm.h"
#include <string.h>

static void pseudo_equ(struct inctx *inp, struct symbol *sym)
{
	if (sym)
		sym->value = expression(inp);
}

static void pseudo_org(struct inctx *inp, struct symbol *sym)
{
	org = expression(inp);
	if (sym)
		sym->value = org;
}

static uint8_t *parse_str(struct inctx *inp, uint8_t *dest)
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
			*dest++ = ch;
		}
		if (ch != endq)
			asm_error(inp, "missing closing quote");
	}
	else
		asm_error(inp, "missing opening quote");
	return dest;
}

static void pseudo_asc(struct inctx *inp, struct symbol *sym)
{
	uint8_t *end = parse_str(inp, objbytes);
	objsize = end - objbytes;
}

static void pseudo_str(struct inctx *inp, struct symbol *sym)
{
	uint8_t *end = parse_str(inp, objbytes);
	*end++ = '\r';
	objsize = end - objbytes;
}

static void pseudo_casc(struct inctx *inp, struct symbol *sym)
{
	uint8_t *end = parse_str(inp, objbytes+1);
	objsize = end - objbytes;
	objbytes[0] = objsize - 1;
}

static void pseudo_cstr(struct inctx *inp, struct symbol *sym)
{
	uint8_t *end = parse_str(inp, objbytes+1);
	*end++ = '\r';
	objsize = end - objbytes;
	objbytes[0] = objsize - 1;
}

static void pseudo_dfb(struct inctx *inp, struct symbol *sym)
{
	int ch;
	do {
		objbytes[objsize++] = expression(inp);
		ch = *inp->lineptr++;
	} while (ch == ',');
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "bad byte expression");
}

static void pseudo_dfw(struct inctx *inp, struct symbol *sym)
{
	int ch;
	do {
		uint16_t word = expression(inp);
		objbytes[objsize++] = word & 0xff;
		objbytes[objsize++] = word >> 8;
		ch = *inp->lineptr++;
	} while (ch == ',');
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "bad word expression");
}

static void pseudo_dfdb(struct inctx *inp, struct symbol *sym)
{
	int ch;
	do {
		uint16_t word = expression(inp);
		objbytes[objsize++] = word >> 8;
		objbytes[objsize++] = word & 0xff;
		ch = *inp->lineptr++;
	} while (ch == ',');
	if (ch != '\n' && ch != ';' && ch != '\\' && ch != '*')
		asm_error(inp, "bad double-byte expression");
}

static void pseudo_clst(struct inctx *inp, struct symbol *sym)
{
	code_list_level = expression(inp);
}

static void pseudo_lst(struct inctx *inp, struct symbol *sym)
{
	src_list_level = expression(inp);
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

static FILE *parse_open(struct inctx *inp, char *filename, const char *mode)
{
	char *ptr = filename;
	int ch = non_space(inp);
	while (ch != ' ' && ch != '\t' && ch != 0xdd) {
		*ptr++ = ch;
		ch = *++inp->lineptr;
	}
	*ptr = 0;
	return fopen(filename, mode);
}

static void pseudo_chn(struct inctx *inp, struct symbol *sym)
{
	char filename[LINE_MAX];
	FILE *fp = parse_open(inp, filename, "r");
	if (fp) {
		fclose(inp->fp);
		inp->fp = fp;
		inp->name = filename;
		asm_file(inp);
	}
	else
		asm_error(inp, "unable to open chained file");
}

static void pseudo_include(struct inctx *inp, struct symbol *sym)
{
	char filename[LINE_MAX];
	FILE *fp = parse_open(inp, filename, "r");
	if (fp) {
		struct inctx incfile;
		incfile.fp = fp;
		incfile.name = filename;
		incfile.whence = 'I';
		asm_file(&incfile);
	}
	else
		asm_error(inp, "unable to open chained file");
}

static void pseudo_code(struct inctx *inp, struct symbol *sym)
{
	char filename[LINE_MAX];
	FILE *fp = parse_open(inp, filename, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		rewind(fp);
		codefile = malloc(size);
		if (codefile) {
			if (fread(codefile, size, 1, fp) == 1) {
				objsize = size;
				memcpy(objbytes, codefile, 3);
			}
			else
				asm_error(inp, "read error on code file");
		}
		else
			asm_error(inp, "not enough memory for code file");
		fclose(fp);
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
	{ "DDB",     pseudo_dfdb    },
	{ "DEND",    pseudo_dend    },
	{ "DFB",     pseudo_dfb     },
	{ "DFW",     pseudo_dfw     },
	{ "DSECT",   pseudo_dsect   },
	{ "DW",      pseudo_dfw     },
	{ "DFDB",    pseudo_dfdb    },
	{ "EQU",     pseudo_equ     },
	{ "INCLUDE", pseudo_include },
	{ "LST",     pseudo_lst     },
	{ "ORG",     pseudo_org     },
	{ "STR",     pseudo_str     }
};

bool pseudo_op(struct inctx *inp, const char *op, struct symbol *sym)
{
	const struct op_type *ptr = pseudo_ops;
	const struct op_type *end = pseudo_ops + sizeof(pseudo_ops) / sizeof(struct op_type);
	while (ptr < end) {
		if (!strcmp(op, ptr->name)) {
			ptr->func(inp, sym);
			return true;
		}
		++ptr;
	}
	return false;
}
