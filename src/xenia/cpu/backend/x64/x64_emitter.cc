/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_emitter.h"

#include <gflags/gflags.h>

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/debug_info.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol_info.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/debugger.h"
#include "xenia/profiling.h"

DEFINE_bool(enable_debugprint_log, false,
            "Log debugprint traps to the active debugger");

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;
using namespace xe::cpu;

using namespace Xbyak;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;

static const size_t kMaxCodeSize = 1 * 1024 * 1024;

static const size_t kStashOffset = 32;
// static const size_t kStashOffsetHigh = 32 + 32;

const uint32_t X64Emitter::gpr_reg_map_[X64Emitter::GPR_COUNT] = {
    Operand::RBX, Operand::R12, Operand::R13, Operand::R14, Operand::R15,
};

const uint32_t X64Emitter::xmm_reg_map_[X64Emitter::XMM_COUNT] = {
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

X64Emitter::X64Emitter(X64Backend* backend, XbyakAllocator* allocator)
    : CodeGenerator(kMaxCodeSize, AutoGrow, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator) {
  if (FLAGS_enable_haswell_instructions) {
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tAVX2) ? kX64EmitAVX2 : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tFMA) ? kX64EmitFMA : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tLZCNT) ? kX64EmitLZCNT : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tBMI2) ? kX64EmitBMI2 : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tF16C) ? kX64EmitF16C : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tMOVBE) ? kX64EmitMovbe : 0;
  }

  if (!cpu_.has(Xbyak::util::Cpu::tAVX)) {
    XEFATAL(
        "Your CPU is too old to support Xenia. See the FAQ for system "
        "requirements at http://xenia.jp");
    return;
  }
}

X64Emitter::~X64Emitter() = default;

bool X64Emitter::Emit(FunctionInfo* function_info, HIRBuilder* builder,
                      uint32_t debug_info_flags, DebugInfo* debug_info,
                      void*& out_code_address, size_t& out_code_size,
                      std::vector<SourceMapEntry>& out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  // Reset.
  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  source_map_arena_.Reset();

  // Fill the generator with code.
  size_t stack_size = 0;
  if (!Emit(builder, stack_size)) {
    return false;
  }

  // Copy the final code to the cache and relocate it.
  out_code_size = getSize();
  out_code_address = Emplace(stack_size, function_info);

  // Stash source map.
  source_map_arena_.CloneContents(out_source_map);

  return true;
}

void* X64Emitter::Emplace(size_t stack_size, FunctionInfo* function_info) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  uint8_t* old_address = top_;
  void* new_address;
  if (function_info) {
    new_address = code_cache_->PlaceGuestCode(function_info->address(), top_,
                                              size_, stack_size, function_info);
  } else {
    new_address = code_cache_->PlaceHostCode(0, top_, size_, stack_size);
  }
  top_ = (uint8_t*)new_address;
  ready();
  top_ = old_address;
  reset();
  return new_address;
}

bool X64Emitter::Emit(HIRBuilder* builder, size_t& out_stack_size) {
  Xbyak::Label epilog_label;
  epilog_label_ = &epilog_label;

  // Calculate stack size. We need to align things to their natural sizes.
  // This could be much better (sort by type/etc).
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = GetTypeSize(slot->type);
    // Align to natural size.
    stack_offset = xe::align(stack_offset, type_size);
    slot->set_constant((uint32_t)stack_offset);
    stack_offset += type_size;
  }
  // Ensure 16b alignment.
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  // Function prolog.
  // Must be 16b aligned.
  // Windows is very strict about the form of this and the epilog:
  // http://msdn.microsoft.com/en-us/library/tawsa7cb.aspx
  // TODO(benvanik): save off non-volatile registers so we can use them:
  //     RBX, RBP, RDI, RSI, RSP, R12, R13, R14, R15
  //     Only want to do this if we actually use them, though, otherwise
  //     it just adds overhead.
  // IMPORTANT: any changes to the prolog must be kept in sync with
  //     X64CodeCache, which dynamically generates exception information.
  //     Adding or changing anything here must be matched!
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true((stack_size + 8) % 16 == 0);
  out_stack_size = stack_size;
  stack_size_ = stack_size;
  sub(rsp, (uint32_t)stack_size);
  mov(qword[rsp + StackLayout::GUEST_RCX_HOME], rcx);
  mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rdx);
  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], 0);

  // Safe now to do some tracing.
  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // We require 32-bit addresses.
    assert_true(uint64_t(debug_info_->trace_data().header()) < UINT_MAX);
    auto trace_header = debug_info_->trace_data().header();

    // Call count.
    lock();
    inc(qword[low_address(&trace_header->function_call_count)]);

    // Get call history slot.
    static_assert(debug::FunctionTraceData::kFunctionCallerHistoryCount == 4,
                  "bitmask depends on count");
    mov(rax, qword[low_address(&trace_header->function_call_count)]);
    and_(rax, B00000011);

    // Record call history value into slot (guest addr in RDX).
    mov(dword[RegExp(uint32_t(uint64_t(
                  low_address(&trace_header->function_caller_history)))) +
              rax * 4],
        edx);

    // Calling thread. Load ax with thread ID.
    EmitGetCurrentThreadId();
    lock();
    bts(qword[low_address(&trace_header->function_thread_use)], rax);
  }

  // Load membase.
  mov(rdx, qword[rcx + 8]);

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(label->name);
      label = label->next;
    }

    // Process instructions.
    const Instr* instr = block->instr_head;
    while (instr) {
      const Instr* new_tail = instr;
      if (!SelectSequence(*this, instr, &new_tail)) {
        // No sequence found!
        assert_always();
        XELOGE("Unable to process HIR opcode %s", instr->opcode->name);
        break;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // Function epilog.
  L(epilog_label);
  epilog_label_ = nullptr;
  EmitTraceUserCallReturn();
  mov(rcx, qword[rsp + StackLayout::GUEST_RCX_HOME]);
  add(rsp, (uint32_t)stack_size);
  ret();

  if (FLAGS_debug) {
    nop();
    nop();
    nop();
    nop();
    nop();
  }

  return true;
}

void X64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->source_offset = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize() + 1);

  if (FLAGS_debug) {
    nop();
    nop();
    mov(eax, entry->source_offset);
    nop();
    nop();
  }

  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
    auto trace_data = debug_info_->trace_data();
    uint32_t instruction_index =
        (entry->source_offset - trace_data.start_address()) / 4;
    lock();
    inc(qword[low_address(trace_data.instruction_execute_counts() +
                          instruction_index * 8)]);
  }
}

void X64Emitter::EmitGetCurrentThreadId() {
  // rcx must point to context. We could fetch from the stack if needed.
  mov(ax,
      word[rcx + processor_->frontend()->context_info()->thread_id_offset()]);
}

void X64Emitter::EmitTraceUserCallReturn() {}

void X64Emitter::DebugBreak() {
  // TODO(benvanik): notify debugger.
  db(0xCC);
}

uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  uint32_t str_ptr = uint32_t(thread_state->context()->r[3]);
  // uint16_t str_len = uint16_t(thread_state->context()->r[4]);
  auto str = thread_state->memory()->TranslateVirtual<const char*>(str_ptr);
  // TODO(benvanik): truncate to length?
  XELOGD("(DebugPrint) %s", str);

  if (FLAGS_enable_debugprint_log) {
    debugging::DebugPrint("(DebugPrint) %s\n", str);
  }

  return 0;
}

void X64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
      CallNative(TrapDebugPrint, 0);
      break;
    case 0:
    case 22:
      // Always trap?
      // TODO(benvanik): post software interrupt to debugger.
      if (FLAGS_break_on_debugbreak) {
        db(0xCC);
      }
      break;
    case 25:
      // ?
      break;
    default:
      XELOGW("Unknown trap type %d", trap_type);
      db(0xCC);
      break;
  }
}

void X64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  db(0xCC);
  assert_always();
}

// This is used by the X64ThunkEmitter's ResolveFunctionThunk.
extern "C" uint64_t ResolveFunction(void* raw_context,
                                    uint32_t target_address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);

  // TODO(benvanik): required?
  assert_not_zero(target_address);

  Function* fn = NULL;
  thread_state->processor()->ResolveFunction(target_address, &fn);
  assert_not_null(fn);
  auto x64_fn = static_cast<X64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(x64_fn->machine_code());

  return addr;
}

void X64Emitter::Call(const hir::Instr* instr, FunctionInfo* symbol_info) {
  assert_not_null(symbol_info);
  auto fn = reinterpret_cast<X64Function*>(symbol_info->function());
  // Resolve address to the function to call and store in rax.
  if (fn) {
    // TODO(benvanik): is it worth it to do this? It removes the need for
    // a ResolveFunction call, but makes the table less useful.
    assert_zero(uint64_t(fn->machine_code()) & 0xFFFFFFFF00000000);
    mov(eax, uint32_t(uint64_t(fn->machine_code())));
  } else {
    // Load the pointer to the indirection table maintained in X64CodeCache.
    // The target dword will either contain the address of the generated code
    // or a thunk to ResolveAddress.
    mov(ebx, symbol_info->address());
    mov(eax, dword[ebx]);
  }

  // Actually jump/call to rax.
  if (instr->flags & CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    mov(rdx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rdx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
  }
}

void X64Emitter::CallIndirect(const hir::Instr* instr, const Reg64& reg) {
  // Check if return.
  if (instr->flags & CALL_POSSIBLE_RETURN) {
    cmp(reg.cvt32(), dword[rsp + StackLayout::GUEST_RET_ADDR]);
    je(epilog_label(), CodeGenerator::T_NEAR);
  }

  // Load the pointer to the indirection table maintained in X64CodeCache.
  // The target dword will either contain the address of the generated code
  // or a thunk to ResolveAddress.
  if (reg.cvt32() != ebx) {
    mov(ebx, reg.cvt32());
  }
  mov(eax, dword[ebx]);

  // Actually jump/call to rax.
  if (instr->flags & CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    mov(rdx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rdx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
  }
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t symbol_info_ptr) {
  auto symbol_info = reinterpret_cast<FunctionInfo*>(symbol_info_ptr);
  XELOGW("undefined extern call to %.8X %s", symbol_info->address(),
         symbol_info->name().c_str());
  return 0;
}
void X64Emitter::CallExtern(const hir::Instr* instr,
                            const FunctionInfo* symbol_info) {
  if (symbol_info->behavior() == FunctionBehavior::kBuiltin &&
      symbol_info->builtin_handler()) {
    // rcx = context
    // rdx = target host function
    // r8  = arg0
    // r9  = arg1
    mov(rdx, reinterpret_cast<uint64_t>(symbol_info->builtin_handler()));
    mov(r8, reinterpret_cast<uint64_t>(symbol_info->builtin_arg0()));
    mov(r9, reinterpret_cast<uint64_t>(symbol_info->builtin_arg1()));
    auto thunk = backend()->guest_to_host_thunk();
    mov(rax, reinterpret_cast<uint64_t>(thunk));
    call(rax);
    ReloadECX();
    ReloadEDX();
    // rax = host return
  } else if (symbol_info->behavior() == FunctionBehavior::kExtern &&
             symbol_info->extern_handler()) {
    // rcx = context
    // rdx = target host function
    mov(rdx, reinterpret_cast<uint64_t>(symbol_info->extern_handler()));
    mov(r8, qword[rcx + offsetof(cpu::frontend::PPCContext, kernel_state)]);
    auto thunk = backend()->guest_to_host_thunk();
    mov(rax, reinterpret_cast<uint64_t>(thunk));
    call(rax);
    ReloadECX();
    ReloadEDX();
    // rax = host return
  } else {
    CallNative(UndefinedCallExtern, reinterpret_cast<uint64_t>(symbol_info));
  }
}

void X64Emitter::CallNative(void* fn) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context)) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0)) {
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                            uint64_t arg0) {
  mov(rdx, arg0);
  mov(rax, reinterpret_cast<uint64_t>(fn));
  call(rax);
  ReloadECX();
  ReloadEDX();
}

void X64Emitter::CallNativeSafe(void* fn) {
  // rcx = context
  // rdx = target function
  // r8  = arg0
  // r9  = arg1
  // r10 = arg2
  mov(rdx, reinterpret_cast<uint64_t>(fn));
  auto thunk = backend()->guest_to_host_thunk();
  mov(rax, reinterpret_cast<uint64_t>(thunk));
  call(rax);
  ReloadECX();
  ReloadEDX();
  // rax = host return
}

void X64Emitter::SetReturnAddress(uint64_t value) {
  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], value);
}

void X64Emitter::ReloadECX() {
  mov(rcx, qword[rsp + StackLayout::GUEST_RCX_HOME]);
}

void X64Emitter::ReloadEDX() {
  mov(rdx, qword[rcx + 8]);  // membase
}

// Len Assembly                                   Byte Sequence
// ============================================================================
// 2b  66 NOP                                     66 90H
// 3b  NOP DWORD ptr [EAX]                        0F 1F 00H
// 4b  NOP DWORD ptr [EAX + 00H]                  0F 1F 40 00H
// 5b  NOP DWORD ptr [EAX + EAX*1 + 00H]          0F 1F 44 00 00H
// 6b  66 NOP DWORD ptr [EAX + EAX*1 + 00H]       66 0F 1F 44 00 00H
// 7b  NOP DWORD ptr [EAX + 00000000H]            0F 1F 80 00 00 00 00H
// 8b  NOP DWORD ptr [EAX + EAX*1 + 00000000H]    0F 1F 84 00 00 00 00 00H
// 9b  66 NOP DWORD ptr [EAX + EAX*1 + 00000000H] 66 0F 1F 84 00 00 00 00 00H
void X64Emitter::nop(size_t length) {
  // TODO(benvanik): fat nop
  for (size_t i = 0; i < length; ++i) {
    db(0x90);
  }
}

bool X64Emitter::ConstantFitsIn32Reg(uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    return true;
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    return true;
  }
  return false;
}

void X64Emitter::MovMem64(const RegExp& addr, uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    mov(qword[addr], v);
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    mov(qword[addr], v);
  } else if (!(v >> 32)) {
    // All high bits are zero. It'd be nice if we had a way to load a 32bit
    // immediate without sign extending!
    // TODO(benvanik): this is super common, find a better way.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], 0);
  } else {
    // 64bit number that needs double movs.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], static_cast<uint32_t>(v >> 32));
  }
}

uint32_t X64Emitter::PlaceData(Memory* memory) {
  static const vec128_t xmm_consts[] = {
      /* XMMZero                */ vec128f(0.0f),
      /* XMMOne                 */ vec128f(1.0f),
      /* XMMNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
      /* XMMFFFF                */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0xFFFFFFFFu),
      /* XMMMaskX16Y16          */ vec128i(0x0000FFFFu, 0xFFFF0000u,
                                           0x00000000u, 0x00000000u),
      /* XMMFlipX16Y16          */ vec128i(0x00008000u, 0x00000000u,
                                           0x00000000u, 0x00000000u),
      /* XMMFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
      /* XMMNormalizeX16Y16     */ vec128f(
          1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
      /* XMM0001                */ vec128f(0.0f, 0.0f, 0.0f, 1.0f),
      /* XMM3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
      /* XMM3333                */ vec128f(3.0f, 3.0f, 3.0f, 3.0f),
      /* XMMSignMaskPS          */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
      /* XMMSignMaskPD          */ vec128i(0x00000000u, 0x80000000u,
                                           0x00000000u, 0x80000000u),
      /* XMMAbsMaskPS           */ vec128i(0x7FFFFFFFu, 0x7FFFFFFFu,
                                           0x7FFFFFFFu, 0x7FFFFFFFu),
      /* XMMAbsMaskPD           */ vec128i(0xFFFFFFFFu, 0x7FFFFFFFu,
                                           0xFFFFFFFFu, 0x7FFFFFFFu),
      /* XMMByteSwapMask        */ vec128i(0x00010203u, 0x04050607u,
                                           0x08090A0Bu, 0x0C0D0E0Fu),
      /* XMMByteOrderMask       */ vec128i(0x01000302u, 0x05040706u,
                                           0x09080B0Au, 0x0D0C0F0Eu),
      /* XMMPermuteControl15    */ vec128b(15),
      /* XMMPermuteByteMask     */ vec128b(0x1F),
      /* XMMPackD3DCOLORSat     */ vec128i(0x404000FFu),
      /* XMMPackD3DCOLOR        */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0x0C000408u),
      /* XMMUnpackD3DCOLOR      */ vec128i(0xFFFFFF0Eu, 0xFFFFFF0Du,
                                           0xFFFFFF0Cu, 0xFFFFFF0Fu),
      /* XMMPackFLOAT16_2       */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0x01000302u),
      /* XMMUnpackFLOAT16_2     */ vec128i(0x0D0C0F0Eu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0xFFFFFFFFu),
      /* XMMPackFLOAT16_4       */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0x05040706u, 0x01000302u),
      /* XMMUnpackFLOAT16_4     */ vec128i(0x09080B0Au, 0x0D0C0F0Eu,
                                           0xFFFFFFFFu, 0xFFFFFFFFu),
      /* XMMPackSHORT_2Min      */ vec128i(0x403F8001u),
      /* XMMPackSHORT_2Max      */ vec128i(0x40407FFFu),
      /* XMMPackSHORT_2         */ vec128i(0xFFFFFFFFu, 0xFFFFFFFFu,
                                           0xFFFFFFFFu, 0x01000504u),
      /* XMMUnpackSHORT_2       */ vec128i(0xFFFF0F0Eu, 0xFFFF0D0Cu,
                                           0xFFFFFFFFu, 0xFFFFFFFFu),
      /* XMMOneOver255          */ vec128f(1.0f / 255.0f),
      /* XMMMaskEvenPI16        */ vec128i(0x0000FFFFu, 0x0000FFFFu,
                                           0x0000FFFFu, 0x0000FFFFu),
      /* XMMShiftMaskEvenPI16   */ vec128i(0x0000000Fu, 0x0000000Fu,
                                           0x0000000Fu, 0x0000000Fu),
      /* XMMShiftMaskPS         */ vec128i(0x0000001Fu, 0x0000001Fu,
                                           0x0000001Fu, 0x0000001Fu),
      /* XMMShiftByteMask       */ vec128i(0x000000FFu, 0x000000FFu,
                                           0x000000FFu, 0x000000FFu),
      /* XMMSwapWordMask        */ vec128i(0x03030303u, 0x03030303u,
                                           0x03030303u, 0x03030303u),
      /* XMMUnsignedDwordMax    */ vec128i(0xFFFFFFFFu, 0x00000000u,
                                           0xFFFFFFFFu, 0x00000000u),
      /* XMM255                 */ vec128f(255.0f),
      /* XMMPI32                */ vec128i(32),
      /* XMMSignMaskI8          */ vec128i(0x80808080u, 0x80808080u,
                                           0x80808080u, 0x80808080u),
      /* XMMSignMaskI16         */ vec128i(0x80008000u, 0x80008000u,
                                           0x80008000u, 0x80008000u),
      /* XMMSignMaskI32         */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
      /* XMMSignMaskF32         */ vec128i(0x80000000u, 0x80000000u,
                                           0x80000000u, 0x80000000u),
      /* XMMShortMinPS          */ vec128f(SHRT_MIN),
      /* XMMShortMaxPS          */ vec128f(SHRT_MAX),
  };
  uint32_t ptr = memory->SystemHeapAlloc(sizeof(xmm_consts));
  std::memcpy(memory->TranslateVirtual(ptr), xmm_consts, sizeof(xmm_consts));
  return ptr;
}

Address X64Emitter::GetXmmConstPtr(XmmConst id) {
  // Load through fixed constant table setup by PlaceData.
  return ptr[rdx + backend_->emitter_data() + sizeof(vec128_t) * id];
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v) {
  // http://www.agner.org/optimize/optimizing_assembly.pdf
  // 13.4 Generating constants
  if (!v.low && !v.high) {
    // 0000...
    vpxor(dest, dest);
  } else if (v.low == ~0ull && v.high == ~0ull) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MovMem64(rsp + kStashOffset, v.low);
    MovMem64(rsp + kStashOffset + 8, v.high);
    vmovdqa(dest, ptr[rsp + kStashOffset]);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0U) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(eax, x.i);
    vmovd(dest, eax);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0ULL) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(rax, x.i);
    vmovq(dest, rax);
  }
}

Address X64Emitter::StashXmm(int index, const Xmm& r) {
  auto addr = ptr[rsp + kStashOffset + (index * 16)];
  vmovups(addr, r);
  return addr;
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
