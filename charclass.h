#ifndef ASM_CHARCLASS
#define ASM_CHARCLASS

static inline bool asm_isspace(int ch)
{
	return ch == ' ' || ch == '\t' || ch == 0xdd;
}

static inline bool asm_iscomment(int ch)
{
	return ch == ';' || ch == '\\' || ch == '*';
}

static inline bool asm_iseol(int ch)
{
	return ch == '\n' || ch == '\r';
}

static inline bool asm_isendchar(int ch)
{
	return asm_iseol(ch) || asm_iscomment(ch);
}

#endif
