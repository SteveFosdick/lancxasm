#include "laxasm.h"
#include <string.h>

struct optab_ent {
	char mnemonic[4];
	uint8_t base, group;
};

/*
 * Instruction groups.
 *
 * 0x00: implied.
 * 0x01: PC-relative.
 * 0x02: ALU Group, ADC, AND, CMP, EOR, LDA, ORA, SBC
 * 0x03: STA (like ALU group, but no immediate mode).
 * 0x04: shifts and rotates.
 * 0x05: INC and DEC.
 * 0x06: BIT
 * 0x07: CPX, CPY
 * 0x08: LDX
 * 0x09: LDY
 * 0x0a: STX
 * 0x0b: STY
 * 0x0c: JMP
 * 0x0d: JSR
 */

static const struct optab_ent m6502_optab[] = {
	{ "ADC", 0x60, 0x02 },
	{ "AND", 0x20, 0x02 },
	{ "ASL", 0x00, 0x04 },
	{ "BCC", 0x90, 0x01 },
	{ "BCS", 0xb0, 0x01 },
	{ "BEQ", 0xf0, 0x01 },
	{ "BIT", 0x20, 0x06 },
	{ "BMI", 0x30, 0x01 },
	{ "BNE", 0xd0, 0x01 },
	{ "BPL", 0x10, 0x01 },
	{ "BRA", 0x80, 0x81 },
	{ "BRK", 0x00, 0x00 },
	{ "BVC", 0x50, 0x01 },
	{ "BVS", 0x70, 0x01 },
	{ "CLC", 0x18, 0x00 },
	{ "CLD", 0xd8, 0x00 },
	{ "CLI", 0x58, 0x00 },
	{ "CLR", 0x60, 0x8e },
	{ "CLV", 0xb8, 0x00 },
	{ "CMP", 0xc0, 0x02 },
	{ "CPX", 0xe0, 0x07 },
	{ "CPY", 0xc0, 0x07 },
	{ "DEA", 0x3a, 0x80 },
	{ "DEC", 0xc0, 0x05 },
	{ "DEX", 0xca, 0x00 },
	{ "DEY", 0x88, 0x00 },
	{ "EOR", 0x40, 0x02 },
	{ "INA", 0x1a, 0x80 },
	{ "INC", 0xe0, 0x05 },
	{ "INX", 0xe8, 0x00 },
	{ "INY", 0xc8, 0x00 },
	{ "JMP", 0x40, 0x0c },
	{ "JSR", 0x20, 0x0d },
	{ "LDA", 0xa0, 0x02 },
	{ "LDX", 0xa0, 0x08 },
	{ "LDY", 0xa0, 0x09 },
	{ "LSR", 0x40, 0x04 },
	{ "NOP", 0xea, 0x00 },
	{ "ORA", 0x00, 0x02 },
	{ "PHA", 0x48, 0x00 },
	{ "PHP", 0x08, 0x00 },
	{ "PHX", 0xda, 0x80 },
	{ "PHY", 0x5a, 0x80 },
	{ "PLA", 0x68, 0x00 },
	{ "PLP", 0x28, 0x00 },
	{ "PLX", 0xfa, 0x80 },
	{ "PLY", 0x7a, 0x80 },
	{ "ROL", 0x20, 0x04 },
	{ "ROR", 0x60, 0x04 },
	{ "RTI", 0x40, 0x00 },
	{ "RTS", 0x60, 0x00 },
	{ "SBC", 0xe0, 0x02 },
	{ "SEC", 0x38, 0x00 },
	{ "SED", 0xf8, 0x00 },
	{ "SEI", 0x78, 0x00 },
	{ "STA", 0x80, 0x03 },
	{ "STX", 0x80, 0x0a },
	{ "STY", 0x80, 0x0b },
	{ "STZ", 0x60, 0x8e },
	{ "TAX", 0xaa, 0x00 },
	{ "TAY", 0xa8, 0x00 },
	{ "TRB", 0x10, 0x8f },
	{ "TSB", 0x00, 0x8f },
	{ "TSX", 0xba, 0x00 },
	{ "TXA", 0x8a, 0x00 },
	{ "TXS", 0x9a, 0x00 },
	{ "TYA", 0x98, 0x00 },
};

/*                                    IMP   REL   ALU   STA   shift I/D   BIT   CPX/Y LDX   LDY   STX   STY   JMP   JSR   STZ   TSB */
static const uint8_t m6502_imm[]  = { 0xff, 0xff, 0x09, 0xff, 0xff, 0xff, 0x69, 0x00, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t m6502_zp[]   = { 0xff, 0xff, 0x05, 0x05, 0x06, 0x06, 0x04, 0x04, 0x06, 0x04, 0x06, 0x04, 0xff, 0xff, 0x04, 0x04 };
static const uint8_t m6502_zpx[]  = { 0xff, 0xff, 0x15, 0x15, 0x16, 0x16, 0x94, 0xff, 0xff, 0x14, 0xff, 0x14, 0xff, 0xff, 0x14, 0xff };
static const uint8_t m6502_zpy[]  = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x16, 0xff, 0x16, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t m6502_abs[]  = { 0xff, 0xff, 0x0d, 0x0d, 0x0e, 0x0e, 0x0c, 0x0c, 0x0e, 0x0c, 0x0e, 0x0c, 0x0c, 0x00, 0x3c, 0x0c };
static const uint8_t m6502_absx[] = { 0xff, 0xff, 0x1d, 0x1d, 0x1e, 0x1e, 0x9c, 0xff, 0xff, 0x1c, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff };
static const uint8_t m6502_absy[] = { 0xff, 0xff, 0x19, 0x19, 0xff, 0xff, 0xff, 0xff, 0x1e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static const char cmos_only_in[] = "%s is a CMOS-only instruction";
static const char cmos_only_am[] = "%s addressing on %s is CMOS-only";
static const char invalid_am[]   = "%s addressing is not valid for %s";
static const char rel_range[]    = "%s branch of %d bytes is out of range by %d bytes";

static void m6502_one_byte(unsigned code)
{
	objcode.str[0] = code;
	objcode.used = 1;
}

static void m6502_two_byte(unsigned code, unsigned value)
{
	objcode.str[0] = code;
	objcode.str[1] = value;
	objcode.used = 2;
}

static void m6502_three_byte(unsigned code, unsigned value)
{
	objcode.str[0] = code;
	objcode.str[1] = value;
	objcode.str[2] = value >> 8;
	objcode.used = 3;
}

static void m6502_implied(struct inctx *inp, const struct optab_ent *opc)
{
	if ((opc->group & 0x7f) == 0)
		m6502_one_byte(opc->base);
	else
		asm_error(inp, "%s needs an operand", opc->mnemonic);
}

static void m6502_accumulator(struct inctx *inp, const struct optab_ent *opc)
{
	unsigned group = opc->group;
	if (group == 0x04)
		m6502_one_byte(opc->base + 0x0a);
	else if (group == 0x05) {
		if (no_cmos)
			asm_error(inp, "%s A is a CMOS-only instruction", opc->mnemonic);
		else
			m6502_one_byte(0xe0 - opc->base + 0x1a);
	}
	else
		asm_error(inp, invalid_am, "accumulator", opc->mnemonic);
}

static void m6502_immediate(struct inctx *inp, const struct optab_ent *opc)
{
	unsigned delta = m6502_imm[opc->group & 0x7f];
	if (delta != 0xff) {
		++inp->lineptr;
		m6502_two_byte(opc->base + delta, expression(inp, false));
	}
	else
		asm_error(inp, invalid_am, "immediate", opc->mnemonic);
}

static void m6502_auto_pick(struct inctx *inp, const struct optab_ent *opc, const uint8_t *grp8, const uint8_t *grp16, unsigned value, const char *mode)
{
	unsigned group = opc->group & 0x7f;
	unsigned delta = grp8[group];
	if (delta != 0xff && value < 0x100)
		m6502_two_byte(opc->base + (delta & 0x7f), value);
	else {
		delta = grp16[group];
		if (delta == 0xff)
			asm_error(inp, invalid_am, mode, opc->mnemonic);
		else if ((delta & 0x80) && no_cmos)
			asm_error(inp, cmos_only_am, mode, opc->mnemonic);
		else
			m6502_three_byte(opc->base + (delta & 0x7f), value);
	}
}

static void m6502_indirect(struct inctx *inp, const struct optab_ent *opc)
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
			if (non_space(inp) == ')') {
				unsigned group = opc->group & 0x7f;
				if (group == 0x02 || group == 0x03)
					m6502_two_byte(opc->base + 0x01, value);
				else if (group == 0x0c) {
					if (no_cmos)
						asm_error(inp, cmos_only_am, "indexed indirect", opc->mnemonic);
					else
						m6502_three_byte(0x7c, value);
				}
				else
					asm_error(inp, invalid_am, "indexed indirect", opc->mnemonic);
			}
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
			if (ch == 'Y' || ch == 'y') {
				unsigned group = opc->group & 0x7f;
				if (group == 0x02 || group == 0x03)
					m6502_two_byte(opc->base + 0x11, value);
				else
					asm_error(inp, invalid_am, "indirect indexed", opc->mnemonic);
			}
			else
				asm_error(inp, "only Y is used for indirect indexed addressing mode");
		}
		else {
			unsigned group = opc->group & 0x7f;
			if (group == 0x02 || group == 0x03) {
				if (no_cmos)
					asm_error(inp, "(non-indexed) indrect addressing mode is CMOS-only");
				else
					m6502_two_byte(opc->base + 0x12, value);
			}
			else if (group == 0x0c)
				m6502_three_byte(opc->base + 0x2c, value);
			else
				asm_error(inp, invalid_am, "(non-indexed) indrect", opc->mnemonic);
		}
	}
	else
		asm_error(inp, "syntax error");
}

static void m6502_others(struct inctx *inp, const struct optab_ent *opc)
{
	uint16_t value = expression(inp, passno);
	int ch = *inp->lineptr;
	if (ch == ',') {
		/* indexed addressing */
		++inp->lineptr;
		ch = non_space(inp);
		if (ch == 'X' || ch == 'x')
			m6502_auto_pick(inp, opc, m6502_zpx, m6502_absx, value, "indexed X");
		else if (ch == 'Y' || ch == 'y')
			m6502_auto_pick(inp, opc, m6502_zpy, m6502_absy, value, "indexed Y");
 		else
			asm_error(inp, "invalid register for indexed addressing");
	}
	else {
		if ((opc->group & 0x7f) == 0x01) {
			int offs = (int)value - (int)(org + 2);
			if (offs < -128)
				asm_error(inp, rel_range, "backward", -offs, -offs - 128);
			else if (offs > 127)
				asm_error(inp, rel_range, "forward", offs, offs - 127);
			m6502_two_byte(opc->base, offs);
		}
		else
			m6502_auto_pick(inp, opc, m6502_zp, m6502_abs, value, "absolute");
	}
}

#include "charclass.h"

bool m6502_op(struct inctx *inp, const char *opname)
{
	const struct optab_ent *opc = m6502_optab;
	const struct optab_ent *end = m6502_optab + sizeof(m6502_optab) / sizeof(struct optab_ent);
	while (opc < end) {
		if (!memcmp(opname, opc->mnemonic, 3)) {
			if ((opc->group & 0x80) && no_cmos)
				asm_error(inp, cmos_only_in, opc->mnemonic);
			else {
				int ch = non_space(inp);
				if (asm_isendchar(ch))
					m6502_implied(inp, opc);
				else if (ch == '#')
					m6502_immediate(inp, opc);
				else if (ch == '(')
					m6502_indirect(inp, opc);
				else if (ch == 'A' || ch == 'a') {
					ch = inp->lineptr[1];
					if (asm_isspace(ch) || asm_isendchar(ch))
						m6502_accumulator(inp, opc);
					else
						m6502_others(inp, opc);
				}
				else
					m6502_others(inp, opc);
			}
			return true;
		}
		++opc;
	}
	return false;
}
