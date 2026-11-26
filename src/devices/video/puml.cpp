// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    puml.cpp

    PUML frontend: opcode table, builder and macros, validation,
    disassembly, the block cache and backend selection.

***************************************************************************/

#include "emu.h"
#include "puml.h"

#if !defined(MAME_NOASM)
#include "asmjit/asmjit/core.h"
#endif

#include <cstdarg>
#include <cstdio>


namespace puml {

namespace {

const op_info s_ops[] =
{
#define PUML_INFO(name, nsrc, dst, c, a64) { #name, nsrc, dst != 0, c != 0, a64 != 0 },
	PUML_OPCODES(PUML_INFO)
#undef PUML_INFO
};

std::string strfmt(const char *fmt, ...)
{
	char buf[512];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	return buf;
}

const char *const s_cond_names[] = { "eq", "ne", "ltu", "leu", "gtu", "geu", "lt", "le", "gt", "ge" };
const char *const s_conv_names[] = { "s32", "u32", "s64", "u64" };
const char *const s_size_names[] = { "u8", "u16", "u32", "u64", "s8", "s16" };
const char *const s_input_names[] = { "x", "y", "x0", "x1", "thread" };

u32 access_bytes(const inst &i)
{
	if (i.t == type::F32)
		return 4;
	if (i.t == type::F64)
		return 8;
	switch (i.cond)
	{
	case SZ_U8: case SZ_S8: return 1;
	case SZ_U16: case SZ_S16: return 2;
	case SZ_U32: return 4;
	default: return 8;
	}
}

bool is_float(type t) { return (t == type::F32) || (t == type::F64); }
bool is_int(type t) { return (t == type::I32) || (t == type::I64); }

} // anonymous namespace


const op_info &info(op o)
{
	return s_ops[unsigned(o)];
}

const char *type_name(type t)
{
	static const char *const names[] = { "i32", "i64", "f32", "f64", "p", "c4", "f4", "" };
	return names[unsigned(t)];
}


//**************************************************************************
//  DISASSEMBLY
//**************************************************************************

const block::symbol *block::find_symbol(u8 region, u32 offset) const
{
	for (const symbol &s : symbols)
		if ((s.region == region) && (offset >= s.offset) && (offset < s.offset + s.length))
			return &s;
	return nullptr;
}

static std::string format_opnd(const opnd &o, type t)
{
	switch (o.kind)
	{
	case okind::IREG: return strfmt("I%d", o.index);
	case okind::FREG: return strfmt("F%d", o.index);
	case okind::VREG: return strfmt("V%d", o.index);
	case okind::PREG: return strfmt("P%d", o.index);
	case okind::IMM:
		if (t == type::F32)
		{
			float f;
			const u32 bits = u32(o.value);
			std::memcpy(&f, &bits, 4);
			return strfmt("%.9g", double(f));
		}
		if (t == type::F64)
		{
			double d;
			std::memcpy(&d, &o.value, 8);
			return strfmt("%.17g", d);
		}
		return (o.value < 10) ? strfmt("%u", unsigned(o.value)) : strfmt("0x%llx", (unsigned long long)o.value);
	default: return "";
	}
}

std::string block::disassemble(const inst &i) const
{
	const op_info &oi = info(i.opcode);
	if (i.opcode == op::COMMENT)
		return "; " + ((i.src[0].value < comments.size()) ? comments[i.src[0].value] : std::string("?"));

	std::string mnem = oi.name;
	switch (i.opcode)
	{
	case op::CMP: case op::FCMP: mnem += std::string(".") + s_cond_names[i.cond % 10]; break;
	case op::FTOI: case op::ITOF: mnem += std::string(".") + s_conv_names[i.cond & 3]; break;
	case op::LD: case op::ST: if (is_int(i.t)) mnem += std::string(".") + s_size_names[i.cond % 6]; break;
	case op::INPUT: mnem += std::string(".") + s_input_names[i.cond % 5]; break;
	case op::SEXT: mnem += (i.cond == 16) ? ".16" : ".8"; break;
	default: break;
	}
	if ((i.t != type::NONE) && (i.t != type::P || i.opcode == op::SEL))
		mnem += std::string(".") + type_name(i.t);

	std::string text = strfmt("%-14s", mnem.c_str());
	std::string args;
	auto add = [&args] (const std::string &s) { if (!args.empty()) args += ", "; args += s; };

	if ((i.opcode == op::LD) || (i.opcode == op::ST))
	{
		const opnd &val = (i.opcode == op::LD) ? i.dst : i.src[2];
		std::string addr = regions[i.region].name.empty() ? strfmt("R%d", i.region) : regions[i.region].name;
		addr += "[";
		if (!i.src[0].is_none())
			addr += format_opnd(i.src[0], type::I32) + "+";
		const u32 off = u32(i.src[1].value);
		const symbol *sym = i.src[0].is_none() ? find_symbol(i.region, off) : nullptr;
		if (sym)
			addr += (off == sym->offset) ? sym->name : strfmt("%s+%u", sym->name.c_str(), off - sym->offset);
		else
			addr += strfmt("0x%x", off);
		addr += "]";
		if (i.opcode == op::LD)
		{
			add(format_opnd(val, i.t));
			add(addr);
		}
		else
		{
			add(addr);
			add(format_opnd(val, i.t));
		}
	}
	else if (i.opcode == op::CALL)
	{
		const u64 n = i.src[0].value;
		add((n < helpers.size()) ? helpers[n].name : strfmt("helper%u", unsigned(n)));
		add(strfmt("0x%llx", (unsigned long long)i.src[1].value));
		if (i.src[2].value < calls.size())
		{
			const call_info &ci = calls[i.src[2].value];
			auto set = [] (char kind, u32 mask) {
				if (mask == ~0U)
					return strfmt("%c*", kind);
				std::string s;
				for (int r = 0; r < 32; r++)
					if (BIT(mask, r))
						s += strfmt("%s%c%d", s.empty() ? "" : " ", kind, r);
				return s.empty() ? std::string("-") : s;
			};
			args += "  ; reads " + set('I', ci.read_i) + " " + set('F', ci.read_f) + ", writes " + set('I', ci.write_i) + " " + set('F', ci.write_f);
		}
	}
	else
	{
		// immediates take the operation's type, except shift counts and CMP sources
		const type immtype = (i.opcode == op::ITOF || i.opcode == op::FTOI) ? type::I32 : i.t;
		if (oi.dst)
			add(format_opnd(i.dst, i.t));
		for (int n = 0; n < oi.nsrc; n++)
			add(format_opnd(i.src[n], immtype));
	}
	return text + args;
}

std::string block::disassemble() const
{
	std::string result = strfmt("block %s\n", name.c_str());
	for (int r = 0; r < MAX_REGIONS; r++)
		if (regions[r].size)
			result += strfmt("  region %d %s: %u bytes%s\n", r, regions[r].name.c_str(), regions[r].size, regions[r].masked ? ", masked" : "");
	for (size_t h = 0; h < helpers.size(); h++)
		result += strfmt("  helper %u %s\n", unsigned(h), helpers[h].name.c_str());
	int depth = 1;
	for (size_t n = 0; n < code.size(); n++)
	{
		const inst &i = code[n];
		if ((i.opcode == op::ELSE) || (i.opcode == op::ENDIF))
			depth--;
		result += strfmt("%04X: %*s%s\n", unsigned(n), depth * 2, "", disassemble(i).c_str());
		if ((i.opcode == op::IF) || (i.opcode == op::ELSE))
			depth++;
	}
	return result;
}


//**************************************************************************
//  BUILDER
//**************************************************************************

int builder::alloc_i()
{
	if (m_next_i >= REG_I) { m_overflow = true; return REG_I - 1; }
	m_peak_i = std::max(m_peak_i, m_next_i + 1);
	return m_next_i++;
}

int builder::alloc_f()
{
	if (m_next_f >= REG_F) { m_overflow = true; return REG_F - 1; }
	m_peak_f = std::max(m_peak_f, m_next_f + 1);
	return m_next_f++;
}

int builder::alloc_p()
{
	if (m_next_p >= REG_P) { m_overflow = true; return REG_P - 1; }
	m_peak_p = std::max(m_peak_p, m_next_p + 1);
	return m_next_p++;
}

void builder::region(int n, const char *name, u32 size, bool masked)
{
	m_block.regions[n].name = name;
	m_block.regions[n].size = size;
	m_block.regions[n].masked = masked;
}

void builder::symbol(int region, u32 offset, u32 length, const char *name)
{
	m_block.symbols.push_back(block::symbol{ u8(region), offset, length, name });
}

int builder::add_helper(helper_fn fn, const char *name)
{
	m_block.helpers.push_back(puml::helper{ fn, name });
	return int(m_block.helpers.size() - 1);
}

inst &builder::emit(op o, type t, opnd dst, opnd a, opnd b, opnd c, u8 cond)
{
	inst i;
	i.opcode = o;
	i.t = t;
	i.cond = cond;
	i.dst = dst;
	i.src[0] = a;
	i.src[1] = b;
	i.src[2] = c;
	m_block.code.push_back(i);
	return m_block.code.back();
}

void builder::call(int index, u64 param, const call_info &regs)
{
	m_block.calls.push_back(regs);
	emit(op::CALL, type::NONE, opnd(), opnd::imm(index), opnd::imm(param), opnd::imm(m_block.calls.size() - 1));
}

void builder::comment(const std::string &text)
{
	m_block.comments.push_back(text);
	emit(op::COMMENT, type::NONE, opnd(), opnd::imm(m_block.comments.size() - 1));
}

void builder::fldi(int dst, float v)
{
	u32 bits;
	std::memcpy(&bits, &v, 4);
	emit(op::LDI, type::F32, opnd::f(dst), opnd::imm(bits));
}

void builder::fldi64(int dst, double v)
{
	u64 bits;
	std::memcpy(&bits, &v, 8);
	emit(op::LDI, type::F64, opnd::f(dst), opnd::imm(bits));
}

void builder::ld(int dst, int region, int reg, u32 offset, msize sz)
{
	inst &i = emit(op::LD, (sz == SZ_U64) ? type::I64 : type::I32, opnd::i(dst), (reg < 0) ? opnd() : opnd::i(reg), opnd::imm(offset), opnd(), sz);
	i.region = u8(region);
}

void builder::fld(int dst, int region, int reg, u32 offset, type t)
{
	inst &i = emit(op::LD, t, opnd::f(dst), (reg < 0) ? opnd() : opnd::i(reg), opnd::imm(offset));
	i.region = u8(region);
}

void builder::st(int region, int reg, u32 offset, int src, msize sz)
{
	inst &i = emit(op::ST, (sz == SZ_U64) ? type::I64 : type::I32, opnd(), (reg < 0) ? opnd() : opnd::i(reg), opnd::imm(offset), opnd::i(src), sz);
	i.region = u8(region);
}

// dst = (src < 0) ? 0 : (src > 1) ? 1 : src, with registers holding 0 and 1;
// the same as std::clamp for every input, NaN and -0 included
void builder::clamp01(int dst, int src, int zero, int one)
{
	emit(op::FCLAMP, type::F32, opnd::f(dst), opnd::f(src), opnd::f(zero), opnd::f(one));
}

// tiles of 2^w bytes by 2^h lines, stored row by row:
// (y >> h) * pitch << h  +  (xb >> w) << (w + h)  +  (y & mask_h) << w  +  (xb & mask_w)
void builder::tile_address(int dst, int xb, int y, int pitch, int w, int h)
{
	const regmark m = mark();
	const int t = alloc_i(), u = alloc_i();
	alui(op::SHR, t, y, h);
	alu(op::MUL, t, t, opnd::i(pitch));
	alui(op::SHL, t, t, h);
	alui(op::SHR, u, xb, w);
	alui(op::SHL, u, u, w + h);
	alu(op::ADD, t, t, opnd::i(u));
	alui(op::AND, u, y, (1U << h) - 1);
	alui(op::SHL, u, u, w);
	alu(op::ADD, t, t, opnd::i(u));
	alui(op::AND, u, xb, (1U << w) - 1);
	alu(op::ADD, dst, t, opnd::i(u));
	release(m);
}

void builder::linear_address(int dst, int xb, int y, int pitch)
{
	const regmark m = mark();
	const int t = alloc_i();
	alu(op::MUL, t, y, opnd::i(pitch));
	alu(op::ADD, dst, t, opnd::i(xb));
	release(m);
}

void builder::morton_address(int dst, int x, int y)
{
	emit(op::TMORTON, type::I32, opnd::i(dst), opnd::i(x), opnd::i(y));
}


//**************************************************************************
//  VALIDATION
//**************************************************************************

std::string validate(const block &b)
{
	int depth = 0, pixel = -1, order = -1;
	std::vector<bool> has_else;
	auto reg_ok = [] (const opnd &o) {
		switch (o.kind)
		{
		case okind::IREG: return o.index < REG_I;
		case okind::FREG: return o.index < REG_F;
		case okind::VREG: return o.index < REG_V;
		case okind::PREG: return o.index < REG_P;
		default: return true;
		}
	};
	auto want = [] (const opnd &o, okind k, bool imm_ok) {
		return (o.kind == k) || (imm_ok && o.is_imm());
	};

	for (size_t n = 0; n < b.code.size(); n++)
	{
		const inst &i = b.code[n];
		auto fail = [&] (const char *why) { return strfmt("%04X %s: %s", unsigned(n), b.disassemble(i).c_str(), why); };

		if (unsigned(i.opcode) >= unsigned(op::COUNT))
			return strfmt("%04X: bad opcode", unsigned(n));
		if (!reg_ok(i.dst) || !reg_ok(i.src[0]) || !reg_ok(i.src[1]) || !reg_ok(i.src[2]))
			return fail("register out of range");

		const okind rk = is_float(i.t) ? okind::FREG : okind::IREG;
		switch (i.opcode)
		{
		case op::PIXEL:
			if ((pixel >= 0) || depth)
				return fail("PIXEL must appear once, outside IF");
			pixel = int(n);
			break;
		case op::ORDER:
			if ((pixel < 0) || (order >= 0) || depth)
				return fail("ORDER must appear once, after PIXEL, outside IF");
			order = int(n);
			break;
		case op::IF:
			if (!want(i.src[0], okind::PREG, false))
				return fail("IF needs a predicate");
			depth++;
			has_else.push_back(false);
			break;
		case op::ELSE:
			if (!depth || has_else.back())
				return fail("ELSE without IF");
			has_else.back() = true;
			break;
		case op::ENDIF:
			if (!depth)
				return fail("ENDIF without IF");
			depth--;
			has_else.pop_back();
			break;
		case op::KILL:
			if (pixel < 0)
				return fail("KILL before PIXEL");
			if (!want(i.src[0], okind::PREG, false))
				return fail("KILL needs a predicate");
			break;
		case op::CALL:
			if ((i.src[0].value >= b.helpers.size()) || !b.helpers[i.src[0].value].fn)
				return fail("unknown helper");
			if (!i.src[2].is_imm() || (i.src[2].value >= b.calls.size()))
				return fail("CALL without call_info");
			break;
		case op::LDI:
			if (!i.src[0].is_imm() || (i.dst.kind != (is_float(i.t) ? okind::FREG : okind::IREG)))
				return fail("bad LDI");
			break;
		case op::MOV: case op::FABS: case op::FNEG: case op::FSQRT:
			if ((i.dst.kind != rk) || !want(i.src[0], rk, false))
				return fail("operand kinds don't match the type");
			if (i.opcode != op::MOV && !is_float(i.t))
				return fail("float operation on an integer type");
			break;
		case op::SEL:
			if (!want(i.src[0], okind::PREG, false))
				return fail("SEL needs a predicate");
			if (i.t == type::P)
			{
				if ((i.dst.kind != okind::PREG) || !want(i.src[1], okind::PREG, false) || !want(i.src[2], okind::PREG, false))
					return fail("predicate SEL needs predicates");
			}
			else if ((i.dst.kind != rk) || !want(i.src[1], rk, rk == okind::IREG) || !want(i.src[2], rk, rk == okind::IREG))
				return fail("operand kinds don't match the type");
			break;
		case op::BITCAST:
			if (!((i.dst.kind == okind::IREG && i.src[0].kind == okind::FREG) || (i.dst.kind == okind::FREG && i.src[0].kind == okind::IREG)))
				return fail("BITCAST moves between I and F");
			break;
		case op::ADD: case op::SUB: case op::MUL: case op::AND: case op::OR: case op::XOR:
		case op::SHL: case op::SHR: case op::SAR: case op::MINU: case op::MAXU: case op::MINS: case op::MAXS:
			if (!is_int(i.t) || (i.dst.kind != okind::IREG) || !want(i.src[0], okind::IREG, false) || !want(i.src[1], okind::IREG, true))
				return fail("integer operation needs I registers (second source may be immediate)");
			break;
		case op::SEXT: case op::CLZ:
			if (!is_int(i.t) || (i.dst.kind != okind::IREG) || !want(i.src[0], okind::IREG, false))
				return fail("integer operation needs I registers");
			break;
		case op::CMP: case op::TST:
			if (!is_int(i.t) || (i.dst.kind != okind::PREG) || !want(i.src[0], okind::IREG, false) || !want(i.src[1], okind::IREG, true) || (i.cond > C_GE))
				return fail("CMP/TST writes a predicate from I registers");
			break;
		case op::CSET:
			if ((i.dst.kind != okind::IREG) || !want(i.src[0], okind::PREG, false))
				return fail("CSET writes an I register from a predicate");
			break;
		case op::FCLAMP:
			if (!is_float(i.t) || (i.dst.kind != okind::FREG) || !want(i.src[0], okind::FREG, false) || !want(i.src[1], okind::FREG, false) || !want(i.src[2], okind::FREG, false))
				return fail("FCLAMP needs F registers");
			break;
		case op::FADD: case op::FSUB: case op::FMUL: case op::FDIV: case op::FMIN: case op::FMAX:
			if (!is_float(i.t) || (i.dst.kind != okind::FREG) || !want(i.src[0], okind::FREG, false) || !want(i.src[1], okind::FREG, false))
				return fail("float operation needs F registers");
			break;
		case op::CBILERP:
			if ((i.dst.kind != okind::IREG) || !want(i.src[0], okind::IREG, false) || (i.src[0].index + 3 >= REG_I)
					|| !want(i.src[1], okind::IREG, false) || !want(i.src[2], okind::IREG, false))
				return fail("CBILERP takes four consecutive I registers and two I weights");
			break;
		case op::FCMP:
			if (!is_float(i.t) || (i.dst.kind != okind::PREG) || !want(i.src[0], okind::FREG, false) || !want(i.src[1], okind::FREG, false) || (i.cond == C_LTU) || (i.cond == C_LEU) || (i.cond == C_GTU) || (i.cond == C_GEU) || (i.cond > C_GE))
				return fail("FCMP writes a predicate from F registers with a signed condition");
			break;
		case op::FTOI:
			if (!is_float(i.t) || (i.dst.kind != okind::IREG) || !want(i.src[0], okind::FREG, false) || (i.cond > CV_U64))
				return fail("FTOI converts an F register to an I register");
			break;
		case op::ITOF:
			if (!is_float(i.t) || (i.dst.kind != okind::FREG) || !want(i.src[0], okind::IREG, false) || (i.cond > CV_U64))
				return fail("ITOF converts an I register to an F register");
			break;
		case op::FCVT:
			if (!is_float(i.t) || (i.dst.kind != okind::FREG) || !want(i.src[0], okind::FREG, false))
				return fail("FCVT converts between F registers");
			break;
		case op::PAND: case op::POR: case op::PXOR:
			if ((i.dst.kind != okind::PREG) || !want(i.src[0], okind::PREG, false) || !want(i.src[1], okind::PREG, false))
				return fail("predicate operation needs P registers");
			break;
		case op::PNOT:
			if ((i.dst.kind != okind::PREG) || !want(i.src[0], okind::PREG, false))
				return fail("predicate operation needs P registers");
			break;
		case op::INPUT:
			if ((i.dst.kind != okind::IREG) || (i.cond > IN_THREAD))
				return fail("bad INPUT");
			break;
		case op::LD: case op::ST:
		{
			if ((i.opcode == op::ST) && (order < 0))
				return fail("store before ORDER");
			if ((i.region >= MAX_REGIONS) || !b.regions[i.region].size)
				return fail("undeclared region");
			const opnd &val = (i.opcode == op::LD) ? i.dst : i.src[2];
			if (val.kind != rk)
				return fail("value register doesn't match the type");
			if (is_int(i.t) && (i.cond > SZ_S16))
				return fail("bad access size");
			if (!i.src[0].is_none() && !want(i.src[0], okind::IREG, false))
				return fail("address register must be an I register");
			const u64 end = i.src[1].value + access_bytes(i);
			if (i.src[0].is_none() ? (end > b.regions[i.region].size) : !b.regions[i.region].masked)
				return fail("access outside the region (unmasked regions only take constant offsets)");
			break;
		}
		default:
			break;
		}
	}
	if (depth)
		return "IF without ENDIF";
	if (pixel < 0)
		return "no PIXEL";
	return std::string();
}


//**************************************************************************
//  FRONTEND
//**************************************************************************

//-------------------------------------------------
//  host capabilities
//-------------------------------------------------

std::string capabilities::names(u32 paths)
{
	static const struct { u32 bit; const char *name; } table[] =
	{
		{ A64_DOTPROD, "dotprod" }, { A64_I8MM, "i8mm" }, { A64_FP16, "fp16" }, { A64_FRINTTS, "frintts" },
		{ A64_LSE, "lse" }, { A64_SVE, "sve" }, { A64_SVE2, "sve2" },
		{ X64_AVX2, "avx2" }, { X64_BMI2, "bmi2" }, { X64_F16C, "f16c" }, { X64_AVX512F, "avx512f" },
		{ X64_AVX512BW, "avx512bw" }, { X64_GFNI, "gfni" }, { GPGPU, "gpgpu" }
	};
	std::string result;
	for (const auto &e : table)
		if (paths & e.bit)
			result += std::string(result.empty() ? "" : " ") + e.name;
	return result.empty() ? "none" : result;
}

capabilities detect_host()
{
	capabilities caps;
#if !defined(MAME_NOASM)
	const asmjit::CpuInfo &cpu = asmjit::CpuInfo::host();
	caps.cpu = strfmt("%s %s", cpu.vendor(), cpu.brand());
	caps.threads = cpu.hw_thread_count();
	auto it = cpu.features().iterator();
	while (it.has_next())
	{
		asmjit::String name;
		asmjit::Formatter::format_feature(name, cpu.arch(), it.next());
		caps.features.emplace_back(name.data(), name.size());
	}
#if defined(__aarch64__) || defined(_M_ARM64)
	const auto &a = cpu.features().arm();
	if (a.has_dotprod()) caps.detected |= capabilities::A64_DOTPROD;
	if (a.has_i8mm()) caps.detected |= capabilities::A64_I8MM;
	if (a.has_fp16()) caps.detected |= capabilities::A64_FP16;
	if (a.has_frintts()) caps.detected |= capabilities::A64_FRINTTS;
	if (a.has_lse()) caps.detected |= capabilities::A64_LSE;
	if (a.has_sve()) caps.detected |= capabilities::A64_SVE;
	if (a.has_sve2()) caps.detected |= capabilities::A64_SVE2;
#elif defined(__x86_64__) || defined(_M_X64)
	const auto &x = cpu.features().x86();
	if (x.has_avx2()) caps.detected |= capabilities::X64_AVX2;
	if (x.has_bmi2()) caps.detected |= capabilities::X64_BMI2;
	if (x.has_f16c()) caps.detected |= capabilities::X64_F16C;
	if (x.has_avx512_f()) caps.detected |= capabilities::X64_AVX512F;
	if (x.has_avx512_bw()) caps.detected |= capabilities::X64_AVX512BW;
	if (x.has_gfni()) caps.detected |= capabilities::X64_GFNI;
#endif
#else
	caps.cpu = "unknown (MAME_NOASM)";
#endif

	// compile-time features of this build, for the C backend's benefit
	std::string built = "built for";
#if defined(__aarch64__) || defined(_M_ARM64)
	built += " arm64";
#elif defined(__x86_64__) || defined(_M_X64)
	built += " amd64";
#else
	built += " another host";
#endif
#if defined(__ARM_NEON)
	built += " +NEON";
#endif
#if defined(__SSE4_1__)
	built += " +SSE4.1";
#elif defined(__SSE2__)
	built += " +SSE2";
#endif
#if defined(__AVX2__)
	built += " +AVX2";
#endif
#if defined(__FP_FAST_FMAF) || defined(__FP_FAST_FMA)
	built += " (hardware FMA: build the C++ reference with -ffp-contract=off for byte-exact verification)";
#endif
	caps.features.insert(caps.features.begin(), built);
	return caps;
}


//**************************************************************************
//  FRONTEND
//**************************************************************************

jit::jit(const config &cfg) : m_cfg(cfg), m_caps(detect_host())
{
	m_caps.enabled = m_caps.detected & ~m_cfg.disable_paths;

	m_c = make_c_backend();
	if (!m_cfg.force_c)
		m_native = make_arm64_backend();
	const backend &active = m_native ? *m_native : *m_c;

	// startup report: backend, CPU, features, optional paths
	log(strfmt("PUML: %s\n", active.name()));
#if !defined(MAME_NOASM) && (defined(__x86_64__) || defined(_M_X64))
	if (!m_cfg.force_c)
		log("PUML: amd64 backend not implemented yet, using the C backend\n");
#endif
	if (m_cfg.force_c && arm64_backend_available())
		log("PUML: C backend forced by -drc_use_c\n");
	log(strfmt("PUML: host CPU: %s, %u hardware threads\n", m_caps.cpu.c_str(), m_caps.threads));
	std::string line;
	for (const std::string &f : m_caps.features)
	{
		if (!line.empty() && (line.size() + f.size() > 100))
		{
			log("PUML: CPU features:" + line + "\n");
			line.clear();
		}
		line += " " + f;
	}
	if (!line.empty())
		log("PUML: CPU features:" + line + "\n");
	log(strfmt("PUML: optional paths detected: %s; enabled: %s; used by the %s: %s\n",
			capabilities::names(m_caps.detected).c_str(), capabilities::names(m_caps.enabled).c_str(),
			active.name(), capabilities::names(active.optional_paths() & m_caps.enabled).c_str()));
	if (m_cfg.request_paths & capabilities::GPGPU)
		log("PUML: GPGPU requested, but no GPGPU backend is compiled in; rendering on the CPU\n");

	if (m_cfg.log_ir)
	{
		m_ir_log = fopen(strfmt("puml_%s.asm", m_cfg.tag.c_str()).c_str(), "w");
		if (m_ir_log)
			fprintf(m_ir_log, "PUML IR for %s (%s)\n\n", m_cfg.tag.c_str(), active.name());
	}
	if (m_cfg.log_native && m_native)
	{
		m_native_log = fopen(strfmt("puml_arm64_%s.asm", m_cfg.tag.c_str()).c_str(), "w");
	}
}

jit::~jit()
{
	m_blocks.clear();
	if (m_ir_log)
		fclose(m_ir_log);
	if (m_native_log)
		fclose(m_native_log);
}

const char *jit::backend_name() const
{
	return m_native ? m_native->name() : m_c->name();
}

const compiled *jit::find(const std::string &key) const
{
	m_stats.lookups++;
	const auto it = m_blocks.find(key);
	if (it == m_blocks.end())
		return nullptr;
	m_stats.hits++;
	return it->second.get();
}

const compiled *jit::add(const std::string &key, block &&b)
{
	const std::string error = validate(b);
	if (!error.empty())
	{
		m_stats.refused++;
		log(strfmt("PUML: block %s failed validation: %s\n", b.name.c_str(), error.c_str()));
		if (m_ir_log)
			fprintf(m_ir_log, "; REJECTED: %s\n%s\n", error.c_str(), b.disassemble().c_str());
		return nullptr;
	}

	if (m_ir_log)
	{
		std::string hex;
		for (unsigned char ch : key)
			hex += strfmt("%02x", ch);
		fprintf(m_ir_log, "; key %s\n%s\n", hex.c_str(), b.disassemble().c_str());
		fflush(m_ir_log);
	}

	std::unique_ptr<compiled> result;
	std::string why;
	if (m_native)
	{
		if (m_native_log)
			fprintf(m_native_log, "\n; block %s\n", b.name.c_str());
		result = m_native->compile(b, m_caps, why, m_native_log);
		if (result)
			m_stats.native++;
		else
			log(strfmt("PUML: %s refused block %s (%s), using the C backend\n", m_native->name(), b.name.c_str(), why.c_str()));
		if (m_native_log)
			fflush(m_native_log);
	}
	if (!result)
	{
		result = m_c->compile(b, m_caps, why, nullptr);
		if (result && m_native)
			m_stats.fallback_c++;
	}
	if (!result)
	{
		m_stats.refused++;
		log(strfmt("PUML: no backend could compile block %s (%s)\n", b.name.c_str(), why.c_str()));
		return nullptr;
	}
	m_stats.compiled++;
	const compiled *ptr = result.get();
	m_blocks[key] = std::move(result);
	return ptr;
}

void jit::flush()
{
	m_blocks.clear();
	m_stats.flushes++;
}

void jit::log_statistics()
{
	log(strfmt("PUML: %llu blocks compiled (%llu native, %llu fell back to C), %llu refused, %llu flushes, %llu/%llu lookups hit\n",
			(unsigned long long)m_stats.compiled, (unsigned long long)m_stats.native, (unsigned long long)m_stats.fallback_c,
			(unsigned long long)m_stats.refused, (unsigned long long)m_stats.flushes,
			(unsigned long long)m_stats.hits, (unsigned long long)m_stats.lookups));
}

} // namespace puml
