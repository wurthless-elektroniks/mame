// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    pumlbearm64.cpp

    PUML arm64 backend, using asmjit.

    Register map (static per block, as in UML's arm64 backend):
        x19-x23, x28    I0-I5
        d8-d15          F0-F7
        w27             P0-P15, one bit each
        x24             frame pointer
        x25             context pointer
        w26             current X
        x3-x6           region 0-3 base pointers, w7 the region 0 mask
        x9-x13, x16     scratch (x16 also holds helper addresses)
        v16-v19         scratch
        x0-x2, x8, x14, x15, x17    I6-I12 (caller-saved)
        v0-v7, v20-v31              F8-F27 (caller-saved)
    The rest live in the frame.  A helper call spills the mapped registers
    the helper reads and the caller-saved ones the block uses, and reloads
    the ones it writes, those caller-saved ones and the region pointers.
    The frame holds every register when the block returns, as with the C
    backend.

***************************************************************************/

#include "emu.h"
#include "puml.h"

#if !defined(MAME_NOASM) && (defined(__aarch64__) || defined(_M_ARM64))

#include "asmjit/asmjit/a64.h"

#include <cstddef>
#include <cstdio>


namespace puml {

namespace {

using namespace asmjit;

const a64::Gp REG_FRAME = a64::x24;
const a64::Gp REG_CTX = a64::x25;
const a64::Gp REG_X = a64::w26;
const a64::Gp REG_PRED = a64::w27;
const a64::Gp REG_MASK0 = a64::w7;
constexpr u32 REG_BASE0 = 3;        // x3-x6

constexpr u32 CALLEE_I[] = { 19, 20, 21, 22, 23, 28 };
constexpr u32 CALLER_I[] = { 0, 1, 2, 8, 14, 15, 17 };
constexpr u32 CALLEE_F[] = { 8, 9, 10, 11, 12, 13, 14, 15 };
constexpr u32 CALLER_F[] = { 0, 1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

constexpr u32 frame_i(int n) { return u32(offsetof(frame, i) + 8 * n); }
constexpr u32 frame_f(int n) { return u32(offsetof(frame, f) + 8 * n); }
constexpr u32 frame_p(int n) { return u32(offsetof(frame, p) + n); }
constexpr u32 frame_x() { return u32(offsetof(frame, x)); }
constexpr u32 ctx_base(int n) { return u32(offsetof(ctx, base) + sizeof(u8 *) * n); }
constexpr u32 ctx_mask(int n) { return u32(offsetof(ctx, mask) + 4 * n); }

bool wide(type t) { return (t == type::I64) || (t == type::F64); }

class error_recorder : public ErrorHandler
{
public:
	std::string message;
	virtual void handle_error(Error err, const char *msg, BaseEmitter *origin) override
	{
		if (message.empty())
			message = msg ? msg : "asmjit error";
	}
};


class a64_compiled : public compiled
{
public:
	using entry_fn = void (*)(const ctx *, frame *);

	a64_compiled(JitRuntime &rt, entry_fn fn) : m_rt(rt), m_fn(fn) { backend_name = "arm64 backend"; }
	virtual ~a64_compiled() override { m_rt.release(m_fn); }
	virtual void run(const ctx &c, frame &f) const override { m_fn(&c, &f); }

private:
	JitRuntime &m_rt;
	entry_fn m_fn;
};


class emitter
{
public:
	emitter(a64::Assembler &a, const block &b);

	void generate();

private:
	struct if_labels { Label lfalse, lend; bool had_else; };

	static a64::Gp gp(u32 id, bool w64) { return w64 ? a64::x(id) : a64::w(id); }
	static a64::Vec fp(u32 id, bool w64) { return w64 ? a64::d(id) : a64::s(id); }

	// registers
	bool native_i(int n) const { return m_imap[n] >= 0; }
	bool native_f(int n) const { return m_fmap[n] >= 0; }
	void load_imm(const a64::Gp &r, u64 v);
	a64::Gp isrc(const opnd &o, bool w64, u32 scratch);
	a64::Gp idst(const opnd &o, bool w64, u32 scratch) { return native_i(o.index) ? gp(m_imap[o.index], w64) : gp(scratch, w64); }
	void iwrite(const opnd &o, const a64::Gp &r) { if (!native_i(o.index)) a.str(r.x(), a64::Mem(REG_FRAME, frame_i(o.index))); }
	a64::Vec fsrc(const opnd &o, bool w64, u32 scratch);
	a64::Vec fdst(const opnd &o, bool w64, u32 scratch) { return native_f(o.index) ? fp(m_fmap[o.index], w64) : fp(scratch, w64); }
	void fwrite(const opnd &o, const a64::Vec &r) { if (!native_f(o.index)) a.str(r, a64::Mem(REG_FRAME, frame_f(o.index))); }

	// predicates are bits of w27
	void pload(const opnd &o, const a64::Gp &w) { a.ubfx(w.w(), REG_PRED, o.index, 1); }
	void pstore(const opnd &o, const a64::Gp &w) { a.bfi(REG_PRED, w.w(), o.index, 1); }
	void ptest(const opnd &o) { a.tst(REG_PRED, u64(1) << o.index); }

	void spill(u32 imask, u32 fmask);
	void reload(u32 imask, u32 fmask);
	void load_regions();

	void emit(const inst &i);
	void emit_memory(const inst &i);

	a64::Assembler &a;
	const block &m_block;
	int m_imap[REG_I], m_fmap[REG_F];
	u32 m_save_i, m_save_f;             // mapped to caller-saved registers and used
	std::vector<if_labels> m_ifs;
	Label m_next;
};


emitter::emitter(a64::Assembler &a, const block &b) : a(a), m_block(b)
{
	std::fill(std::begin(m_imap), std::end(m_imap), -1);
	std::fill(std::begin(m_fmap), std::end(m_fmap), -1);
	int n = 0;
	for (u32 r : CALLEE_I)
		m_imap[n++] = r;
	for (u32 r : CALLER_I)
		m_imap[n++] = r;
	n = 0;
	for (u32 r : CALLEE_F)
		m_fmap[n++] = r;
	for (u32 r : CALLER_F)
		m_fmap[n++] = r;

	// caller-saved registers the block uses, which helper calls must preserve
	m_save_i = m_save_f = 0;
	auto note = [this] (const opnd &o) {
		if ((o.kind == okind::IREG) && (m_imap[o.index] >= 0) && (std::find(std::begin(CALLER_I), std::end(CALLER_I), u32(m_imap[o.index])) != std::end(CALLER_I)))
			m_save_i |= 1U << o.index;
		if ((o.kind == okind::FREG) && (m_fmap[o.index] >= 0) && (std::find(std::begin(CALLER_F), std::end(CALLER_F), u32(m_fmap[o.index])) != std::end(CALLER_F)))
			m_save_f |= 1U << o.index;
	};
	for (const inst &i : b.code)
	{
		note(i.dst);
		for (const opnd &o : i.src)
			note(o);
	}
}

void emitter::load_imm(const a64::Gp &r, u64 v)
{
	const bool w64 = r.is_gp64();
	if (!w64)
		v = u32(v);
	bool first = true;
	for (int shift = 0; shift < (w64 ? 64 : 32); shift += 16)
	{
		const u64 part = (v >> shift) & 0xffff;
		if (!part)
			continue;
		if (first)
			a.movz(r, part, a64::lsl(shift));
		else
			a.movk(r, part, a64::lsl(shift));
		first = false;
	}
	if (first)
		a.movz(r, 0);
}

a64::Gp emitter::isrc(const opnd &o, bool w64, u32 scratch)
{
	if (o.is_imm())
	{
		const a64::Gp r = gp(scratch, w64);
		load_imm(r, o.value);
		return r;
	}
	if (native_i(o.index))
		return gp(m_imap[o.index], w64);
	const a64::Gp r = gp(scratch, w64);
	a.ldr(r, a64::Mem(REG_FRAME, frame_i(o.index)));
	return r;
}

a64::Vec emitter::fsrc(const opnd &o, bool w64, u32 scratch)
{
	if (native_f(o.index))
		return fp(m_fmap[o.index], w64);
	const a64::Vec r = fp(scratch, w64);
	a.ldr(r, a64::Mem(REG_FRAME, frame_f(o.index)));
	return r;
}

void emitter::spill(u32 imask, u32 fmask)
{
	for (int n = 0; n < REG_I; n++)
		if (native_i(n) && BIT(imask, n))
			a.str(a64::x(m_imap[n]), a64::Mem(REG_FRAME, frame_i(n)));
	for (int n = 0; n < REG_F; n++)
		if (native_f(n) && BIT(fmask, n))
			a.str(a64::d(m_fmap[n]), a64::Mem(REG_FRAME, frame_f(n)));
}

void emitter::reload(u32 imask, u32 fmask)
{
	for (int n = 0; n < REG_I; n++)
		if (native_i(n) && BIT(imask, n))
			a.ldr(a64::x(m_imap[n]), a64::Mem(REG_FRAME, frame_i(n)));
	for (int n = 0; n < REG_F; n++)
		if (native_f(n) && BIT(fmask, n))
			a.ldr(a64::d(m_fmap[n]), a64::Mem(REG_FRAME, frame_f(n)));
}

void emitter::load_regions()
{
	for (int r = 0; r < 4; r++)
		if (m_block.regions[r].size)
			a.ldr(a64::x(REG_BASE0 + r), a64::Mem(REG_CTX, ctx_base(r)));
	if (m_block.regions[0].size)
		a.ldr(REG_MASK0, a64::Mem(REG_CTX, ctx_mask(0)));
}

void emitter::generate()
{
	// prologue: x29/x30, x19-x28 and d8-d15
	a.bind(a.new_named_label("entry"));
	a.sub(a64::sp, a64::sp, 160);
	a.stp(a64::x29, a64::x30, a64::Mem(a64::sp, 0));
	for (int n = 0; n < 10; n += 2)
		a.stp(a64::x(19 + n), a64::x(20 + n), a64::Mem(a64::sp, 16 + 8 * n));
	for (int n = 0; n < 8; n += 2)
		a.stp(a64::d(8 + n), a64::d(9 + n), a64::Mem(a64::sp, 96 + 8 * n));
	a.mov(a64::x29, a64::sp);
	a.mov(REG_CTX, a64::x0);
	a.mov(REG_FRAME, a64::x1);
	reload(~0U, ~0U);
	load_regions();
	a.movz(REG_PRED, 0);
	for (int n = 0; n < REG_P; n++)
	{
		a.ldrb(a64::w9, a64::Mem(REG_FRAME, frame_p(n)));
		a.orr(REG_PRED, REG_PRED, a64::w9, a64::lsl(n));
	}
	a.ldr(REG_X, a64::Mem(REG_CTX, offsetof(ctx, x0)));

	Label loop = a.new_named_label("pixel");
	Label done = a.new_named_label("done");
	m_next = a.new_named_label("next");

	size_t n = 0;
	for ( ; (n < m_block.code.size()) && (m_block.code[n].opcode != op::PIXEL); n++)
		emit(m_block.code[n]);

	// the pixel loop
	a.ldr(a64::w9, a64::Mem(REG_CTX, offsetof(ctx, x1)));
	a.cmp(REG_X, a64::w9);
	a.b(a64::CondCode::kGE, done);
	a.bind(loop);
	for (n++; n < m_block.code.size(); n++)
		emit(m_block.code[n]);
	a.bind(m_next);
	a.ldr(a64::w9, a64::Mem(REG_CTX, offsetof(ctx, x1)));
	a.add(REG_X, REG_X, 1);
	a.cmp(REG_X, a64::w9);
	a.b(a64::CondCode::kLT, loop);

	// epilogue: the frame gets every register
	a.bind(done);
	spill(~0U, ~0U);
	for (int r = 0; r < REG_P; r++)
	{
		a.ubfx(a64::w9, REG_PRED, r, 1);
		a.strb(a64::w9, a64::Mem(REG_FRAME, frame_p(r)));
	}
	for (int r = 0; r < 8; r += 2)
		a.ldp(a64::d(8 + r), a64::d(9 + r), a64::Mem(a64::sp, 96 + 8 * r));
	for (int r = 0; r < 10; r += 2)
		a.ldp(a64::x(19 + r), a64::x(20 + r), a64::Mem(a64::sp, 16 + 8 * r));
	a.ldp(a64::x29, a64::x30, a64::Mem(a64::sp, 0));
	a.add(a64::sp, a64::sp, 160);
	a.ret(a64::x30);
}

void emitter::emit_memory(const inst &i)
{
	const bool fl = (i.t == type::F32) || (i.t == type::F64);
	const bool w64 = wide(i.t);
	const u32 bytes = (i.t == type::F32) ? 4 : (i.t == type::F64) ? 8 :
			((i.cond == SZ_U8) || (i.cond == SZ_S8)) ? 1 : ((i.cond == SZ_U16) || (i.cond == SZ_S16)) ? 2 : (i.cond == SZ_U32) ? 4 : 8;

	// region base: pinned for regions 0-3
	a64::Gp base = a64::x11;
	if (i.region < 4)
		base = a64::x(REG_BASE0 + i.region);
	else
		a.ldr(base, a64::Mem(REG_CTX, ctx_base(i.region)));

	const u32 imm = u32(i.src[1].value);
	a64::Mem mem;
	if (!i.src[0].is_none())
	{
		const a64::Gp r = isrc(i.src[0], false, 9);
		a64::Gp addr = r;
		if (imm)
		{
			load_imm(a64::w10, imm);
			a.add(a64::w9, r, a64::w10);
			addr = a64::w9;
		}
		if (i.region == 0)
			a.and_(a64::w9, addr, REG_MASK0);
		else
		{
			a.ldr(a64::w10, a64::Mem(REG_CTX, ctx_mask(i.region)));
			a.and_(a64::w9, addr, a64::w10);
		}
		mem = a64::Mem(base, a64::x9);
	}
	else if (!(imm % bytes) && (imm / bytes < 4096))
		mem = a64::Mem(base, int32_t(imm));
	else
	{
		load_imm(a64::x9, imm);
		mem = a64::Mem(base, a64::x9);
	}

	if (i.opcode == op::LD)
	{
		if (fl)
		{
			const a64::Vec d = fdst(i.dst, w64, 16);
			a.ldr(d, mem);
			fwrite(i.dst, d);
			return;
		}
		const a64::Gp d = idst(i.dst, w64, 12);
		switch (i.cond)
		{
		case SZ_U8: a.ldrb(d.w(), mem); break;
		case SZ_U16: a.ldrh(d.w(), mem); break;
		case SZ_U32: a.ldr(d.w(), mem); break;
		case SZ_S8: a.ldrsb(d, mem); break;
		case SZ_S16: a.ldrsh(d, mem); break;
		default: a.ldr(d.x(), mem); break;
		}
		iwrite(i.dst, d);
	}
	else
	{
		if (fl)
		{
			a.str(fsrc(i.src[2], w64, 16), mem);
			return;
		}
		const a64::Gp v = isrc(i.src[2], true, 12);
		switch (i.cond)
		{
		case SZ_U8: case SZ_S8: a.strb(v.w(), mem); break;
		case SZ_U16: case SZ_S16: a.strh(v.w(), mem); break;
		case SZ_U32: a.str(v.w(), mem); break;
		default: a.str(v.x(), mem); break;
		}
	}
}

a64::CondCode int_cond(u8 c)
{
	switch (c)
	{
	case C_EQ: return a64::CondCode::kEQ;
	case C_NE: return a64::CondCode::kNE;
	case C_LTU: return a64::CondCode::kLO;
	case C_LEU: return a64::CondCode::kLS;
	case C_GTU: return a64::CondCode::kHI;
	case C_GEU: return a64::CondCode::kHS;
	case C_LT: return a64::CondCode::kLT;
	case C_LE: return a64::CondCode::kLE;
	case C_GT: return a64::CondCode::kGT;
	default: return a64::CondCode::kGE;
	}
}

// after FCMP a, b: false whenever unordered, except NE (as in C)
a64::CondCode float_cond(u8 c)
{
	switch (c)
	{
	case C_EQ: return a64::CondCode::kEQ;
	case C_NE: return a64::CondCode::kNE;
	case C_LT: return a64::CondCode::kMI;
	case C_LE: return a64::CondCode::kLS;
	case C_GT: return a64::CondCode::kGT;
	default: return a64::CondCode::kGE;
	}
}

void emitter::emit(const inst &i)
{
	const type t = i.t;
	const bool w64 = wide(t);

	switch (i.opcode)
	{
	case op::NOP: case op::PIXEL: case op::ORDER:
		break;
	case op::COMMENT:
		if (a.logger())
			a.comment(m_block.disassemble(i).c_str());
		break;

	case op::IF:
	{
		if_labels l{ a.new_label(), a.new_label(), false };
		a.tbz(REG_PRED, i.src[0].index, l.lfalse);
		m_ifs.push_back(l);
		break;
	}
	case op::ELSE:
		a.b(m_ifs.back().lend);
		a.bind(m_ifs.back().lfalse);
		m_ifs.back().had_else = true;
		break;
	case op::ENDIF:
		if (!m_ifs.back().had_else)
			a.bind(m_ifs.back().lfalse);
		a.bind(m_ifs.back().lend);
		m_ifs.pop_back();
		break;
	case op::KILL:
		a.tbnz(REG_PRED, i.src[0].index, m_next);
		break;
	case op::INPUT:
	{
		const a64::Gp d = idst(i.dst, false, 11);
		switch (i.cond)
		{
		case IN_X: a.mov(d, REG_X); break;
		case IN_Y: a.ldr(d, a64::Mem(REG_CTX, offsetof(ctx, y))); break;
		case IN_X0: a.ldr(d, a64::Mem(REG_CTX, offsetof(ctx, x0))); break;
		case IN_X1: a.ldr(d, a64::Mem(REG_CTX, offsetof(ctx, x1))); break;
		default: a.ldr(d, a64::Mem(REG_CTX, offsetof(ctx, thread))); break;
		}
		iwrite(i.dst, d);
		break;
	}
	case op::CALL:
	{
		const helper &h = m_block.helpers[i.src[0].value];
		const call_info &ci = m_block.calls[i.src[2].value];
		if (a.logger())
			a.comment(("call " + h.name).c_str());
		spill(ci.read_i | m_save_i, ci.read_f | m_save_f);
		a.str(REG_X, a64::Mem(REG_FRAME, frame_x()));
		a.mov(a64::x0, REG_CTX);
		a.mov(a64::x1, REG_FRAME);
		load_imm(a64::x2, i.src[1].value);
		load_imm(a64::x16, u64(uintptr_t(h.fn)));
		a.blr(a64::x16);
		reload(ci.write_i | m_save_i, ci.write_f | m_save_f);
		load_regions();
		break;
	}

	case op::LDI:
		if (i.dst.kind == okind::FREG)
		{
			const a64::Vec d = fdst(i.dst, w64, 18);
			if (!i.src[0].value)
				a.fmov(d, w64 ? a64::xzr : a64::wzr);
			else
			{
				load_imm(gp(9, w64), i.src[0].value);
				a.fmov(d, gp(9, w64));
			}
			fwrite(i.dst, d);
		}
		else
		{
			const a64::Gp d = idst(i.dst, w64, 11);
			load_imm(d, i.src[0].value);
			iwrite(i.dst, d);
		}
		break;
	case op::MOV:
		if (i.dst.kind == okind::FREG)
		{
			const a64::Vec s = fsrc(i.src[0], true, 16), d = fdst(i.dst, true, 18);
			if (s.id() != d.id())
				a.fmov(d, s);
			fwrite(i.dst, d);
		}
		else
		{
			const a64::Gp s = isrc(i.src[0], w64, 9), d = idst(i.dst, w64, 11);
			if (s.id() != d.id() || !w64)
				a.mov(d, s);
			iwrite(i.dst, d);
		}
		break;
	case op::SEL:
		if (t == type::P)
		{
			pload(i.src[1], a64::w9);
			pload(i.src[2], a64::w10);
			ptest(i.src[0]);
			a.csel(a64::w9, a64::w9, a64::w10, a64::CondCode::kNE);
			pstore(i.dst, a64::w9);
		}
		else if (i.dst.kind == okind::FREG)
		{
			const a64::Vec x = fsrc(i.src[1], true, 16), y = fsrc(i.src[2], true, 17), d = fdst(i.dst, true, 18);
			ptest(i.src[0]);
			a.fcsel(d, x, y, a64::CondCode::kNE);
			fwrite(i.dst, d);
		}
		else
		{
			const a64::Gp x = isrc(i.src[1], w64, 9), y = isrc(i.src[2], w64, 10), d = idst(i.dst, w64, 11);
			ptest(i.src[0]);
			a.csel(d, x, y, a64::CondCode::kNE);
			iwrite(i.dst, d);
		}
		break;
	case op::BITCAST:
		if (i.dst.kind == okind::IREG)
		{
			const a64::Vec s = fsrc(i.src[0], w64, 16);
			const a64::Gp d = idst(i.dst, w64, 11);
			a.fmov(d, s);
			iwrite(i.dst, d);
		}
		else
		{
			const a64::Gp s = isrc(i.src[0], w64, 9);
			const a64::Vec d = fdst(i.dst, w64, 18);
			a.fmov(d, s);
			fwrite(i.dst, d);
		}
		break;

	case op::ADD: case op::SUB: case op::MUL: case op::AND: case op::OR: case op::XOR:
	case op::SHL: case op::SHR: case op::SAR: case op::MINU: case op::MAXU: case op::MINS: case op::MAXS:
	{
		const a64::Gp x = isrc(i.src[0], w64, 9), d = idst(i.dst, w64, 11);
		const unsigned bits = w64 ? 64 : 32;
		// constant shifts and small additions don't need the immediate in a register
		if (i.src[1].is_imm() && ((i.opcode == op::SHL) || (i.opcode == op::SHR) || (i.opcode == op::SAR)))
		{
			const unsigned sh = unsigned(i.src[1].value) & (bits - 1);
			if (i.opcode == op::SHL) a.lsl(d, x, sh);
			else if (i.opcode == op::SHR) a.lsr(d, x, sh);
			else a.asr(d, x, sh);
			iwrite(i.dst, d);
			break;
		}
		if (i.src[1].is_imm() && ((i.opcode == op::ADD) || (i.opcode == op::SUB)) && ((w64 ? i.src[1].value : u32(i.src[1].value)) < 4096))
		{
			if (i.opcode == op::ADD) a.add(d, x, unsigned(i.src[1].value));
			else a.sub(d, x, unsigned(i.src[1].value));
			iwrite(i.dst, d);
			break;
		}
		const a64::Gp y = isrc(i.src[1], w64, 10);
		switch (i.opcode)
		{
		case op::ADD: a.add(d, x, y); break;
		case op::SUB: a.sub(d, x, y); break;
		case op::MUL: a.mul(d, x, y); break;
		case op::AND: a.and_(d, x, y); break;
		case op::OR: a.orr(d, x, y); break;
		case op::XOR: a.eor(d, x, y); break;
		case op::SHL: a.lslv(d, x, y); break;
		case op::SHR: a.lsrv(d, x, y); break;
		case op::SAR: a.asrv(d, x, y); break;
		case op::MINU: a.cmp(x, y); a.csel(d, x, y, a64::CondCode::kLO); break;
		case op::MAXU: a.cmp(x, y); a.csel(d, x, y, a64::CondCode::kHI); break;
		case op::MINS: a.cmp(x, y); a.csel(d, x, y, a64::CondCode::kLT); break;
		default: a.cmp(x, y); a.csel(d, x, y, a64::CondCode::kGT); break;
		}
		iwrite(i.dst, d);
		break;
	}
	case op::SEXT: case op::CLZ:
	{
		const a64::Gp x = isrc(i.src[0], w64, 9), d = idst(i.dst, w64, 11);
		if (i.opcode == op::CLZ)
			a.clz(d, x);
		else if (i.cond == 16)
			a.sxth(d, x.w());
		else
			a.sxtb(d, x.w());
		iwrite(i.dst, d);
		break;
	}
	case op::CMP: case op::TST:
	{
		const a64::Gp x = isrc(i.src[0], w64, 9), y = isrc(i.src[1], w64, 10);
		if (i.opcode == op::TST)
		{
			a.tst(x, y);
			a.cset(a64::w12, a64::CondCode::kNE);
		}
		else
		{
			a.cmp(x, y);
			a.cset(a64::w12, int_cond(i.cond));
		}
		pstore(i.dst, a64::w12);
		break;
	}
	case op::CSET:
	{
		const a64::Gp d = idst(i.dst, w64, 11);
		pload(i.src[0], d);
		iwrite(i.dst, d);
		break;
	}

	case op::FADD: case op::FSUB: case op::FMUL: case op::FDIV: case op::FMIN: case op::FMAX:
	{
		const a64::Vec x = fsrc(i.src[0], w64, 16), y = fsrc(i.src[1], w64, 17), d = fdst(i.dst, w64, 18);
		switch (i.opcode)
		{
		case op::FADD: a.fadd(d, x, y); break;
		case op::FSUB: a.fsub(d, x, y); break;
		case op::FMUL: a.fmul(d, x, y); break;
		case op::FDIV: a.fdiv(d, x, y); break;
		case op::FMIN: a.fcmp(y, x); a.fcsel(d, y, x, a64::CondCode::kMI); break;     // (b < a) ? b : a
		default: a.fcmp(x, y); a.fcsel(d, y, x, a64::CondCode::kMI); break;            // (a < b) ? b : a
		}
		fwrite(i.dst, d);
		break;
	}
	case op::FCLAMP:
	{
		// t = (hi < v) ? hi : v, then (v < lo) ? lo : t
		const a64::Vec v = fsrc(i.src[0], w64, 16), lo = fsrc(i.src[1], w64, 17), hi = fsrc(i.src[2], w64, 18);
		const a64::Vec t = fp(19, w64);
		a.fcmp(hi, v);
		a.fcsel(t, hi, v, a64::CondCode::kMI);
		a.fcmp(v, lo);
		const a64::Vec d = fdst(i.dst, w64, 18);
		a.fcsel(d, lo, t, a64::CondCode::kMI);
		fwrite(i.dst, d);
		break;
	}
	case op::FABS: case op::FNEG: case op::FSQRT:
	{
		const a64::Vec x = fsrc(i.src[0], w64, 16), d = fdst(i.dst, w64, 18);
		if (i.opcode == op::FABS) a.fabs(d, x);
		else if (i.opcode == op::FNEG) a.fneg(d, x);
		else a.fsqrt(d, x);
		fwrite(i.dst, d);
		break;
	}
	case op::FCMP:
	{
		const a64::Vec x = fsrc(i.src[0], w64, 16), y = fsrc(i.src[1], w64, 17);
		a.fcmp(x, y);
		a.cset(a64::w12, float_cond(i.cond));
		pstore(i.dst, a64::w12);
		break;
	}
	case op::FTOI:
	{
		const bool r64 = (i.cond == CV_S64) || (i.cond == CV_U64);
		const a64::Vec x = fsrc(i.src[0], w64, 16);
		const a64::Gp d = idst(i.dst, r64, 11);
		if ((i.cond == CV_S32) || (i.cond == CV_S64))
			a.fcvtzs(d, x);
		else
			a.fcvtzu(d, x);
		iwrite(i.dst, d);
		break;
	}
	case op::ITOF:
	{
		const bool r64 = (i.cond == CV_S64) || (i.cond == CV_U64);
		const a64::Gp x = isrc(i.src[0], r64, 9);
		const a64::Vec d = fdst(i.dst, w64, 18);
		if ((i.cond == CV_S32) || (i.cond == CV_S64))
			a.scvtf(d, x);
		else
			a.ucvtf(d, x);
		fwrite(i.dst, d);
		break;
	}
	case op::FCVT:
	{
		const a64::Vec x = fsrc(i.src[0], !w64, 16), d = fdst(i.dst, w64, 18);
		a.fcvt(d, x);
		fwrite(i.dst, d);
		break;
	}

	case op::PAND: case op::POR: case op::PXOR:
		pload(i.src[0], a64::w9);
		pload(i.src[1], a64::w10);
		if (i.opcode == op::PAND) a.and_(a64::w9, a64::w9, a64::w10);
		else if (i.opcode == op::POR) a.orr(a64::w9, a64::w9, a64::w10);
		else a.eor(a64::w9, a64::w9, a64::w10);
		pstore(i.dst, a64::w9);
		break;
	case op::PNOT:
		if (i.dst.index == i.src[0].index)
			a.eor(REG_PRED, REG_PRED, u64(1) << i.dst.index);
		else
		{
			pload(i.src[0], a64::w9);
			a.eor(a64::w9, a64::w9, 1);
			pstore(i.dst, a64::w9);
		}
		break;

	case op::LD: case op::ST:
		emit_memory(i);
		break;

	default:
		break;      // refused before emission
	}
}


class a64_backend : public backend
{
public:
	virtual const char *name() const override { return "arm64 backend"; }

	// none of the optional paths has code yet; candidates are listed above
	virtual u32 optional_paths() const override { return 0; }

	virtual std::unique_ptr<compiled> compile(const block &b, const capabilities &caps, std::string &why, FILE *native_log) override
	{
		for (const inst &i : b.code)
		{
			if (!info(i.opcode).a64_impl)
			{
				why = std::string(info(i.opcode).name) + " not implemented in the arm64 backend";
				return nullptr;
			}
		}

		CodeHolder ch;
		ch.init(m_rt.environment(), m_rt.cpu_features());
		error_recorder errors;
		ch.set_error_handler(&errors);
		FileLogger logger(native_log);
		if (native_log)
		{
			logger.set_flags(FormatFlags::kHexOffsets | FormatFlags::kHexImms | FormatFlags::kMachineCode);
			logger.set_indentation(FormatIndentationGroup::kCode, 4);
			ch.set_logger(&logger);
		}
		a64::Assembler a(&ch);
		if (native_log)
			a.add_diagnostic_options(DiagnosticOptions::kValidateIntermediate);

		emitter e(a, b);
		e.generate();
		if (!errors.message.empty())
		{
			why = "asmjit: " + errors.message;
			return nullptr;
		}

		a64_compiled::entry_fn fn = nullptr;
		const Error err = m_rt.add(&fn, &ch);
		if (err != Error::kOk)
		{
			why = std::string("asmjit: ") + DebugUtils::error_as_string(err);
			return nullptr;
		}
		return std::make_unique<a64_compiled>(m_rt, fn);
	}

private:
	JitRuntime m_rt;
};

} // anonymous namespace


std::unique_ptr<backend> make_arm64_backend()
{
	return std::make_unique<a64_backend>();
}

bool arm64_backend_available()
{
	return true;
}

} // namespace puml

#else // no arm64 backend on this host

namespace puml {

std::unique_ptr<backend> make_arm64_backend()
{
	return nullptr;
}

bool arm64_backend_available()
{
	return false;
}

} // namespace puml

#endif
