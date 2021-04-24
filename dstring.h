#ifndef DSTRING_INC
#define DSTRING_INC

#include <string.h>
#include <stdio.h>

struct dstring {
	size_t used;
	size_t allocated;
	char *str;
};

extern void dstr_empty(struct dstring *dstr, size_t bytes);
extern void dstr_grow(struct dstring *dstr, size_t bytes);
extern void dstr_add_ch(struct dstring *dstr, int ch);
extern void dstr_add_bytes(struct dstring *dstr, const char *src, size_t bytes);
extern void dstr_add_str(struct dstring *dstr, const char *src);
extern ssize_t dstr_getdelim(struct dstring *dstr, int delim, FILE *fp);

#endif
