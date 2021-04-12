#include "lancxasm.h"
#include <stdlib.h>

static int expr_term(struct inctx *inp)
{
    int value, ch = *inp->lineptr;
    if (ch == '*')
		value = org;
	else if (ch == '\'') {
        bool ctrl = false;
        bool topset = false;
        ch = *++inp->lineptr;
        if (ch == '|') {
            ch = *++inp->lineptr;
            if (ch != '|') {
                ctrl = true;
                ch = *++inp->lineptr;
            }
        }
        if (ch == '^') {
            ch = *++inp->lineptr;
            if (ch != '^') {
                topset = true;
                ch = *++inp->lineptr;
            }
        }
        value = ch & 0x7f;
        if (ctrl)
            value &= 0x1f;
        else if (topset)
            value |= 0x80;
        ch = *++inp->lineptr;
        if (ch == '\'')
            ++inp->lineptr;
        else
            asm_error(inp, "missing ' in character constant");
    }
    else if (ch == '%')
        value = strtoul(inp->lineptr + 1, &inp->lineptr, 2);
    else if (ch == '$' || ch == '&')
        value = strtoul(inp->lineptr + 1, &inp->lineptr, 16);
    else if (ch >= '0' && ch <= '9')
        value = strtoul(inp->lineptr, &inp->lineptr, 10);
    else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
		value = symbol_lookup(inp);
	else {
		asm_error(inp, "invalid expression");
		value = org;
	}
	ch = *inp->lineptr;
	while (ch == ' ' || ch == '\t' || ch == 0xdd)
		ch = *++inp->lineptr;
	return value;
}

static int expr_bracket(struct inctx *inp)
{
	int ch = *inp->lineptr;
    if (ch == '(') {
        ++inp->lineptr;
        int value = expression(inp);
        if (*inp->lineptr == ')')
			do ch = *++inp->lineptr; while (ch == ' ' || ch == '\t' || ch == 0xdd);
        else
            asm_error(inp, "missing bracket");
        return value;
    }
    else
		return expr_term(inp);
}

static int expr_unary(struct inctx *inp)
{
    int ch = *inp->lineptr;
    if (ch == '-') {
        ++inp->lineptr;
        return -expr_bracket(inp);
    }
    else if (ch == '~') {
        ++inp->lineptr;
        return ~expr_bracket(inp);
    }
    else
        return expr_bracket(inp);
}

static int expr_bitwise(struct inctx *inp)
{
    int value = expr_unary(inp);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '&') {
            ++inp->lineptr;
            value &= expr_unary(inp);
        }
        else if (ch == '!') {
            ++inp->lineptr;
            value |= expr_unary(inp);
        }
        else
            return value;
    }
}

static int expr_compare(struct inctx *inp)
{
    int value = expr_bitwise(inp);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '=') {
            ++inp->lineptr;
            value = (value == expr_bitwise(inp)) ? -1 : 0;
        }
        else if (ch == '#') {
            ++inp->lineptr;
            value = (value != expr_bitwise(inp)) ? -1 : 0;
        }
        else if (ch == '>') {
            ++inp->lineptr;
            value = (value > expr_bitwise(inp)) ? -1 : 0;
        }
        else if (ch == '<') {
            ++inp->lineptr;
            value = (value < expr_bitwise(inp)) ? -1 : 0;
        }
        else
            return value;
    }
}

static int expr_muldiv(struct inctx *inp)
{
    int value = expr_compare(inp);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '*') {
            ++inp->lineptr;
            value *= expr_compare(inp);
        }
        else if (ch == '/') {
            ++inp->lineptr;
            value /= expr_compare(inp);
        }
        else
            return value;
    }
}

static int expr_addsub(struct inctx *inp)
{
    int value = expr_muldiv(inp);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '+') {
            ++inp->lineptr;
            value += expr_muldiv(inp);
        }
        else if (ch == '-') {
            ++inp->lineptr;
            value -= expr_muldiv(inp);
        }
        else
            return value;
    }
}

int expression(struct inctx *inp)
{
    int ch = non_space(inp);
    if (ch == '>') {
        ++inp->lineptr;
        return expr_addsub(inp) & 0xff;
    }
    else if (ch == '<') {
        ++inp->lineptr;
        return (expr_addsub(inp) >> 8) & 0xff;
    }
    else
        return expr_addsub(inp);
}
