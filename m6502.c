#include "lancxasm.h"
#include <string.h>

struct optab_ent {
	char mnemonic[4];
	uint16_t imp, acc, imm, zp, zpx, zpy, abs, absx, absy, ind, indx, indy, ind16, ind16x, rel;
};

#define X 0xffff

static const struct optab_ent optab[] = {
	{ "ADC",	 X,		 X,		0x69,	0x65,	0x75,	 X,		0x6D,	0x7D,	0x79,	0x172,	0x61,	0x71,	 X,		 X,		 X		},
    { "AND",	 X,		 X,		0x29,	0x25,	0x35,	 X,		0x2D,	0x3D,	0x39,	0x132,	0x21,	0x31,	 X,		 X,		 X		},
    { "ASL",	 X,		0x0A,	 X,		0x06,	0x16,	 X,		0x0E,	0x1E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "BCC",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x90	},
    { "BCS",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0xB0	},
    { "BEQ",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0xF0	},
    { "BIT",	 X,		 X,		0x189,	0x24,	0x134,	 X,		0x2C,	0x13C,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "BMI",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x30	},
    { "BNE",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0xD0	},
    { "BPL",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x10	},
    { "BRA",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x180	},
    { "BRK",	0x00,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "BVC",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x50	},
    { "BVS",	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		0x70	},
    { "CLC",	0x18,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CLD",	0xD8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CLI",	0x58,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CLR",	 X,		 X,		 X,		0x164,	0x174,	 X,		0x19C,	0x19E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CLV",	0xB8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CMP",	 X,		 X,		0xC9,	0xC5,	0xD5,	 X,		0xCD,	0xDD,	0xD9,	0x1D2,	0xC1,	0xD1,	 X,		 X,		 X		},
    { "CPX",	 X,		 X,		0xE0,	0xE4,	 X,		 X,		0xEC,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "CPY",	 X,		 X,		0xC0,	0xC4,	 X,		 X,		0xCC,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "DEA",	0x13A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "DEC",	 X,		0x13A,	 X,		0xC6,	0xD6,	 X,		0xCE,	0xDE,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "DEX",	0xCA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "DEY",	0x88,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "EOR",	 X,		 X,		0x49,	0x45,	0x55,	 X,		0x4D,	0x5D,	0x59,	0x152,	0x41,	0x51,	 X,		 X,		 X		},
    { "INA",	0x11A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "INC",	 X,		0x11A,	 X,		0xE6,	0xF6,	 X,		0xEE,	0xFE,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "INX",	0xE8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "INY",	0xC8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "JMP",	 X,		 X,		 X,		 X,		 X,		 X,		0x4C,	 X,		 X,		 X,		 X,		 X,		0x6C,	0x17C,	 X		},
    { "JSR",	 X,		 X,		 X,		 X,		 X,		 X,		0x20,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "LDA",	 X,		 X,		0xA9,	0xA5,	0xB5,	 X,		0xAD,	0xBD,	0xB9,	0x1B2,	0xA1,	0xB1,	 X,		 X,		 X		},
    { "LDX",	 X,		 X,		0xA2,	0xA6,	 X,		0xB6,	0xAE,	 X,		0xBE,	 X,		 X,		 X,		 X,		 X,		 X		},
    { "LDY",	 X,		 X,		0xA0,	0xA4,	0xB4,	 X,		0xAC,	0xBC,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "LSR",	 X,		0x4A,	 X,		0x46,	0x56,	 X,		0x4E,	0x5E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "NOP",	0xEA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "ORA",	 X,		 X,		0x09,	0x05,	0x15,	 X,		0x0D,	0x1D,	0x19,	0x112,	0x01,	0x11,	 X,		 X,		 X		},
    { "PHA",	0x48,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PHP",	0x08,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PHX",	0x1DA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PHY",	0x15A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PLA",	0x68,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PLP",	0x28,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PLX",	0x1FA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "PLY",	0x17A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "ROL",	 X,		0x2A,	 X,		0x26,	0x36,	 X,		0x2E,	0x3E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "ROR",	 X,		0x6A,	 X,		0x66,	0x76,	 X,		0x6E,	0x7E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "RTI",	0x40,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "RTS",	0x60,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "SBC",	 X,		 X,		0xE9,	0xE5,	0xF5,	 X,		0xED,	0xFD,	0xF9,	0x1F2,	0xE1,	0xF1,	 X,		 X,		 X		},
    { "SEC",	0x38,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "SED",	0xF8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "SEI",	0x78,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "STA",	 X,		 X,		 X,		0x85,	0x95,	 X,		0x8D,	0x9D,	0x99,	0x192,	0x81,	0x91,	 X,		 X,		 X		},
    { "STX",	 X,		 X,		 X,		0x86,	 X,		0x96,	0x8E,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "STY",	 X,		 X,		 X,		0x84,	0x94,	 X,		0x8C,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "STZ",	 X,		 X,		 X,		0x164,	0x174,	 X,		0x19C,	0x19E,	 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TAX",	0xAA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TAY",	0xA8,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TRB",	 X,		 X,		 X,		0x114,	 X,		 X,		0x11C,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TSB",	 X,		 X,		 X,		0x104,	 X,		 X,		0x10C,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TSX",	0xBA,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TXA",	0x8A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TXS",	0x9A,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		},
    { "TYA",	0x98,	 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X,		 X		}
};

#undef X

static void m6502_implied(struct inctx *inp, const struct optab_ent *ptr)
{
	uint16_t code = ptr->imp;
	if (code == 0xffff)
		code = ptr->acc;
	if (code == 0xffff)
		asm_error(inp, "Operation %s needs an operand", ptr->mnemonic);
	else if (code & 0x100 && no_cmos)
		asm_error(inp, "%s is a CMOS-only instruction", ptr->mnemonic);
	else {
		objcode.str[0] = code;
		objcode.used = 1;
	}
}

static void m6502_accumulator(struct inctx *inp, const struct optab_ent *ptr)
{
	uint16_t code = ptr->acc;
	if (code == 0xffff)
		asm_error(inp, "accumulator addressings is not valid for %s", ptr->mnemonic);
	else if (code & 0x100 && no_cmos)
		asm_error(inp, "%s is a CMOS-only instruction", ptr->mnemonic);
	else {
		objcode.str[0] = code;
		objcode.used = 1;
	}
}
	
static void m6502_two_byte(struct inctx *inp, uint16_t code, uint16_t value)
{
	if (code == 0xffff)
		asm_error(inp, "Invalid addressing mode for instruction");
	else if (code & 0x100 && no_cmos)
		asm_error(inp, "This is CMOS-only");
	else {
		objcode.str[0] = code;
		objcode.str[1] = value;
		objcode.used = 2;
	}
}

static void m6502_three_byte(struct inctx *inp, uint16_t code, uint16_t value)
{
	if (code == 0xffff)
		asm_error(inp, "Invalid addressing mode for instruction");
	else if (code & 0x100 && no_cmos)
		asm_error(inp, "This is CMOS-only");
	else {
		objcode.str[0] = code;
		objcode.str[1] = value;
		objcode.str[2] = value >> 8;
		objcode.used = 3;
	}
}

static void m6502_auto_pick(struct inctx *inp, uint16_t code8, uint16_t code16, uint16_t value)
{
	if (code8 != 0xffff && value < 0x100 && !(code8 & 0x100 && no_cmos)) {
		objcode.str[0] = code8;
		objcode.str[1] = value;
		objcode.used = 2;
	}
	else
		m6502_three_byte(inp, code16, value);
}

static void m6502_indirect(struct inctx *inp, const struct optab_ent *ptr)
{
	++inp->lineptr;
	uint16_t value = expression(inp, passno);
	int ch = *inp->lineptr;
	if (ch == ',') {
		/* should be indexed (by X) indirect. */
		++inp->lineptr;
		ch = non_space(inp);
		if (ch == 'X' || ch == 'x') {
			++inp->lineptr;
			if (non_space(inp) == ')')
				m6502_two_byte(inp, ptr->indx, value);
			else
				asm_error(inp, "missing closing bracket ')'");
		}
		else
			asm_error(inp, "only X is used for indexed indirect addressing mode");
	}
	else if (ch == ')') {
		/* is it indirect indexed (by Y)? */
		++inp->lineptr;
		if (non_space(inp) == ',') {
			++inp->lineptr;
			ch = non_space(inp);
			if (ch == 'Y' || ch == 'y')
				m6502_two_byte(inp, ptr->indy, value);
			else
				asm_error(inp, "only Y is used for indirect indexed addressing mode");
		}
		else
			m6502_auto_pick(inp, ptr->ind, ptr->ind16, value);
	}
	else
		asm_error(inp, "syntax error");
}

static void m6502_others(struct inctx *inp, const struct optab_ent *ptr)
{
	uint16_t value = expression(inp, passno);
	int ch = *inp->lineptr;
	if (ch == ',') {
		/* indexed addressing */
		++inp->lineptr;
		ch = non_space(inp);
		if (ch == 'X' || ch == 'x')
			m6502_auto_pick(inp, ptr->zpx, ptr->absx, value);
		else if (ch == 'Y' || ch == 'y')
			m6502_auto_pick(inp, ptr->zpy, ptr->absy, value);
 		else
			asm_error(inp, "invalid register for indexed addressing");
	}
	else {
		uint16_t code = ptr->rel;
		if (code != 0xffff && !(code & 0x100 && no_cmos)) {
			int offs = (int)value - (int)(org + 2);
			if (offs < -128)
				asm_error(inp, "backward branch of %d bytes is out of range by %d bytes", -offs, -offs - 128);
			else if (offs > 127)
				asm_error(inp, "forward branch of %d bytes is out of range by %d bytes", offs, offs - 127);
			objcode.str[0] = code;
			objcode.str[1] = offs;
			objcode.used = 2;
		}
		else
			m6502_auto_pick(inp, ptr->zp, ptr->abs, value);
	}
}

bool m6502_op(struct inctx *inp)
{
	if (opname.used == 3) {
		const struct optab_ent *ptr = optab;
		const struct optab_ent *end = optab + sizeof(optab) / sizeof(struct optab_ent);
		while (ptr < end) {
			if (!memcmp(opname.str, ptr->mnemonic, 3)) {
				int ch = non_space(inp);
				if (ch == '\n' || ch == ';' || ch == '\\' || ch == '*')
					m6502_implied(inp, ptr);
				else if (ch == '#') {
					++inp->lineptr;
					m6502_two_byte(inp, ptr->imm, expression(inp, passno));
				}
				else if (ch == '(')
					m6502_indirect(inp, ptr);
				else if (ch == 'A' || ch == 'a') {
					ch = inp->lineptr[1];
					if (ch == ' ' || ch == '\t' || ch == ';' || ch == '\\' || ch == '*' || ch == '\n')
						m6502_accumulator(inp, ptr);
					else
						m6502_others(inp, ptr);
				}
				else
					m6502_others(inp, ptr);
				return true;
			}
			++ptr;
		}
	}
	return false;
}
