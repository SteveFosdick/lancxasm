#include "dstring.h"

#include <stdio.h>
#include <stdlib.h>

#define MIN_SIZE 8

__attribute__((noreturn))
static void dstr_nomem(size_t bytes)
{
	fprintf(stderr, "Out of memory trying to allocate %lu bytes\n", (unsigned long)bytes);
	abort();
}

void dstr_empty(struct dstring *dstr, size_t bytes)
{
	if (bytes < MIN_SIZE)
		bytes = MIN_SIZE;
	if (!(dstr->str = malloc(bytes)))
		dstr_nomem(bytes);
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
				grab = 8;
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
