#ifndef ASM_CHARCLASS
#define ASM_CHARCLASS

static inline bool asm_isspace(int ch)
{
	return ch == ' ' || ch == '\t';
}

static inline bool asm_iscomment(int ch)
{
	return ch == ';' || ch == '\\' || ch == '*';
}

static inline bool asm_isendchar(int ch)
{
	return ch == '\n' || asm_iscomment(ch);
}

#endif
