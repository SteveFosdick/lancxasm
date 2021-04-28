#include "dstring.h"

#include <stdlib.h>

#define MIN_SIZE 8

__attribute__((noreturn))
static void dstr_nomem(size_t bytes)
{
	fprintf(stderr, "laxasm: Out of memory trying to allocate %lu bytes\n", (unsigned long)bytes);
	abort();
}

void dstr_empty(struct dstring *dstr, size_t bytes)
{
	char *str = NULL;
	if (bytes > 0) {
		if (bytes < MIN_SIZE)
			bytes = MIN_SIZE;
		if (!(str = malloc(bytes)))
			dstr_nomem(bytes);
	}
	dstr->str = str;
	dstr->used = 0;
	dstr->allocated = bytes;
}

void dstr_grow(struct dstring *dstr, size_t bytes)
{
	size_t reqd = dstr->used + bytes;
	if (reqd > dstr->allocated) {
		size_t grab = reqd;
		if (reqd < 256) {
			grab = dstr->allocated;
			if (!grab)
				grab = MIN_SIZE;
			while (grab < reqd)
				grab <<= 1;
		}
		if (!(dstr->str = realloc(dstr->str, grab)))
			dstr_nomem(grab);
		dstr->allocated = grab;
	}
}

void dstr_add_ch(struct dstring *dstr, int ch)
{
	dstr_grow(dstr, 1);
	dstr->str[dstr->used++] = ch;
}

void dstr_add_bytes(struct dstring *dstr, const char *src, size_t bytes)
{
	dstr_grow(dstr, bytes);
	memcpy(dstr->str + dstr->used, src, bytes);
	dstr->used += bytes;
}

void dstr_add_str(struct dstring *dstr, const char *src)
{
	dstr_add_bytes(dstr, src, strlen(src));
}

ssize_t dstr_getdelim(struct dstring *dstr, int delim, FILE *fp)
{
#ifdef __WIN32__
	dstr->used = 0;
	int ch = getc(fp);
	if (ch != EOF) {
		do {
			if (ch == 0xdd)
				ch = '\t';
			dstr_grow(dstr, 1);
			if (ch == delim) {
				dstr->str[dstr->used++] = '\n';
				break;
			}
			dstr->str[dstr->used++] = ch;
			ch = getc(fp);
		} while (ch != EOF);
		return dstr->used;
	}
	return -1;
#else
	ssize_t bytes = getdelim(&dstr->str, &dstr->allocated, delim, fp);
	if (bytes > 0) {
		dstr->str[bytes-1] = '\n';
		char *ww = memchr(dstr->str, 0xdd, bytes);
		if (ww) {
			char *end = dstr->str + bytes;
			do {
				*ww++ = '\t';
				ww = memchr(ww, 0xdd, end - ww);
			} while (ww);
		}
	}
	dstr->used = bytes;
	return bytes;
#endif
}
