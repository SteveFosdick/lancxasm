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

static size_t check_grow_buffer(struct inctx *inp, size_t size)
{
	size_t reqd = objsize + size;
	if (reqd > objalloc) {
		uint8_t *newbuf = realloc(objbytes, reqd);
		if (newbuf) {
			objbytes = newbuf;
			objalloc = reqd;
		}
		else {
			asm_error(inp, "out of memory for code buffer");
			return objalloc - objsize;
		}
	}
	return size;
}		

static void plant_bytes(struct inctx *inp, size_t count, uint16_t byte)
{
	size_t bytes = check_grow_buffer(inp, count);
	while (bytes--)
		objbytes[objsize++] = byte;
}

static void plant_words(struct inctx *inp, size_t count, uint16_t word)
{
	size_t bytes = check_grow_buffer(inp, count << 1) & ~1;
	while (bytes) {
		objbytes[objsize++] = word;
		objbytes[objsize++] = word >> 8;
		bytes -= 2;
	}
}

static void plant_dbytes(struct inctx *inp, size_t count, uint16_t word)
{
	size_t bytes = check_grow_buffer(inp, count << 1) & ~1;
	while (bytes) {
		objbytes[objsize++] = word >> 8;
		objbytes[objsize++] = word;
		bytes -= 2;
	}
}

static void plant_data(struct inctx *inp, const char *desc, void (*planter)(struct inctx *inp, size_t count, uint16_t value))
{
	int ch;
	do {
		ch = non_space(inp);
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

static FILE *parse_open(struct inctx *inp, char *filename, const char *mode)
{
	char *ptr = filename;
	int ch = non_space(inp);
	while (ch != '\n' && ch != ' ' && ch != '\t' && ch != 0xdd) {
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
		asm_error(inp, "unable to open chained file %s: %s", filename, strerror(errno));
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
		asm_error(inp, "unable to open include file %s: %s", filename, strerror(errno));
}

static void pseudo_code(struct inctx *inp, struct symbol *sym)
{
	char filename[LINE_MAX];
	FILE *fp = parse_open(inp, filename, "rb");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		rewind(fp);
		if (size > objalloc) {
			uint8_t *newbuf = realloc(objbytes, size);
			if (newbuf) {
				objbytes = newbuf;
				objalloc = size;
			}
			else {
				asm_error(inp, "no enough memory to read all of code file %s", filename);
				size = objalloc;
			}
		}
		if (fread(objbytes, size, 1, fp) == 1)
			objsize = size;
		else
			asm_error(inp, "read error on code file %s: %s", filename, strerror(errno));
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
	{ "DS",      pseudo_ds      },
	{ "DW",      pseudo_dfw     },
	{ "DFDB",    pseudo_dfdb    },
	{ "EQU",     pseudo_equ     },
	{ "HEX",     pseudo_hex     },
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
