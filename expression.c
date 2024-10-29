#include "laxasm.h"
#include <stdlib.h>

static int expr_term(struct inctx *inp, bool no_undef)
{
    int value, ch = non_space(inp);
    if (ch == '*') {
		value = org;
		++inp->lineptr;
	}
	else if (ch == '\'' || ch == '"') {
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
        if (ch == '\'' || ch == '"')
            ++inp->lineptr;
    }
    else if (ch == '%')
        value = strtoul(inp->lineptr + 1, &inp->lineptr, 2);
    else if (ch == '$' || ch == '&')
        value = strtoul(inp->lineptr + 1, &inp->lineptr, 16);
    else if (ch >= '0' && ch <= '9')
        value = strtoul(inp->lineptr, &inp->lineptr, 10);
    else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ':') {
		struct symbol *sym = symbol_lookup(inp, no_undef);
		value = sym ? sym->value : org;
	}
	else if (ch == '#')
		value = passno ? -1 : 0;
	else {
		asm_error(inp, "invalid expression");
		value = org;
	}
	ch = *inp->lineptr;
	while (ch == ' ' || ch == '\t' || ch == 0xdd)
		ch = *++inp->lineptr;
	return value;
}

static int expr_bracket(struct inctx *inp, bool no_undef)
{
	int ch = non_space(inp);
	if (ch == '(')
		ch= ')';
	else if (ch == '[')
		ch = ']';
	else
        return expr_term(inp, no_undef);
    ++inp->lineptr;
    int value = expression(inp, no_undef);
    if (*inp->lineptr == ch)
		do ch = *++inp->lineptr; while (ch == ' ' || ch == '\t' || ch == 0xdd);
    else
		asm_error(inp, "missing or mismatched bracket");
    return value;
}

static int expr_unary(struct inctx *inp, bool no_undef)
{
    int ch = non_space(inp);
    if (ch == '+')
		ch = *++inp->lineptr;
    if (ch == '-') {
        ++inp->lineptr;
        return -expr_bracket(inp, no_undef);
    }
    else if (ch == '~') {
        ++inp->lineptr;
        return expr_bracket(inp, no_undef) ^ 0xffff;
    }
    else
        return expr_bracket(inp, no_undef);
}

static int expr_bitwise(struct inctx *inp, bool no_undef)
{
    int value = expr_unary(inp, no_undef);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '&') {
            ++inp->lineptr;
            value &= expr_unary(inp, no_undef);
        }
        else if (ch == '!') {
            ++inp->lineptr;
            value |= expr_unary(inp, no_undef);
        }
        else
            return value;
    }
}

static int expr_compare(struct inctx *inp, bool no_undef)
{
    int value = expr_bitwise(inp, no_undef);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '=') {
            ++inp->lineptr;
            value = (value == expr_bitwise(inp, no_undef)) ? -1 : 0;
        }
        else if (ch == '#') {
            ++inp->lineptr;
            value = (value != expr_bitwise(inp, no_undef)) ? -1 : 0;
        }
        else if (ch == '>') {
            ch = *++inp->lineptr;
            int right = expr_bitwise(inp, no_undef);
            if (ch == '=') {
				++inp->lineptr;
				value = (value >= right) ? -1 : 0;
			}
			else
				value = (value > right) ? -1 : 0;
        }
        else if (ch == '<') {
            ch = *++inp->lineptr;
            int right = expr_bitwise(inp, no_undef);
            if (ch == '=') {
				++inp->lineptr;
				value = (value <= right) ? -1 : 0;
			}
			else
				value = (value < right) ? -1 : 0;
        }
        else
            return value;
    }
}

static int expr_muldiv(struct inctx *inp, bool no_undef)
{
    int value = expr_compare(inp, no_undef);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '*') {
            ++inp->lineptr;
            value *= expr_compare(inp, no_undef);
        }
        else if (ch == '/') {
            ++inp->lineptr;
            int right = expr_compare(inp, no_undef);
            if (right == 0)
				asm_error(inp, "Division by zero");
			else
				value /= right;
        }
        else if (ch == '<' && inp->lineptr[1] == '<') {
			inp->lineptr += 2;
			value <<= expr_compare(inp, no_undef);
		}
		else if (ch == '>' && inp->lineptr[1] == '>') {
			inp->lineptr += 2;
			value >>= expr_compare(inp, no_undef);
		}
		else
            return value;
    }
}

static int expr_addsub(struct inctx *inp, bool no_undef)
{
    int value = expr_muldiv(inp, no_undef);
    for (;;) {
        int ch = *inp->lineptr;
        if (ch == '+') {
            ++inp->lineptr;
            value += expr_muldiv(inp, no_undef);
        }
        else if (ch == '-') {
            ++inp->lineptr;
            value -= expr_muldiv(inp, no_undef);
        }
        else
            return value;
    }
}

int expression(struct inctx *inp, bool no_undef)
{
    int ch = non_space(inp);
    if (ch == '>') {
        ++inp->lineptr;
        return expr_addsub(inp, no_undef) & 0xff;
    }
    else if (ch == '<') {
        ++inp->lineptr;
        return (expr_addsub(inp, no_undef) >> 8) & 0xff;
    }
    else
        return expr_addsub(inp, no_undef);
}
