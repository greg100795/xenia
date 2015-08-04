/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/value.h"

#include <cmath>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/math.h"

namespace xe {
namespace cpu {
namespace hir {

Value::Use* Value::AddUse(Arena* arena, Instr* instr) {
  Use* use = arena->Alloc<Use>();
  use->instr = instr;
  use->prev = NULL;
  use->next = use_head;
  if (use_head) {
    use_head->prev = use;
  }
  use_head = use;
  return use;
}

void Value::RemoveUse(Use* use) {
  if (use == use_head) {
    use_head = use->next;
  } else {
    use->prev->next = use->next;
  }
  if (use->next) {
    use->next->prev = use->prev;
  }
}

uint32_t Value::AsUint32() {
  assert_true(IsConstant());
  switch (type) {
    case INT8_TYPE:
      return constant.i8;
    case INT16_TYPE:
      return constant.i16;
    case INT32_TYPE:
      return constant.i32;
    case INT64_TYPE:
      return (uint32_t)constant.i64;
    default:
      assert_unhandled_case(type);
      return 0;
  }
}

uint64_t Value::AsUint64() {
  assert_true(IsConstant());
  switch (type) {
    case INT8_TYPE:
      return constant.i8;
    case INT16_TYPE:
      return constant.i16;
    case INT32_TYPE:
      return constant.i32;
    case INT64_TYPE:
      return constant.i64;
    default:
      assert_unhandled_case(type);
      return 0;
  }
}

void Value::Cast(TypeName target_type) {
  // TODO(benvanik): big matrix.
  assert_always();
}

void Value::ZeroExtend(TypeName target_type) {
  switch (type) {
    case INT8_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFF;
      return;
    case INT16_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFFFF;
      return;
    case INT32_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFFFFFFFF;
      return;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::SignExtend(TypeName target_type) {
  switch (type) {
    case INT8_TYPE:
      type = target_type;
      switch (target_type) {
        case INT16_TYPE:
          constant.i16 = constant.i8;
          return;
        case INT32_TYPE:
          constant.i32 = constant.i8;
          return;
        case INT64_TYPE:
          constant.i64 = constant.i8;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT16_TYPE:
      type = target_type;
      switch (target_type) {
        case INT32_TYPE:
          constant.i32 = constant.i16;
          return;
        case INT64_TYPE:
          constant.i64 = constant.i16;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT32_TYPE:
      type = target_type;
      switch (target_type) {
        case INT64_TYPE:
          constant.i64 = constant.i32;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    default:
      assert_unhandled_case(type);
      return;
  }
}

void Value::Truncate(TypeName target_type) {
  switch (type) {
    case INT16_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT32_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        case INT16_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT64_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        case INT16_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFF;
          return;
        case INT32_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFFFFFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    default:
      assert_unhandled_case(type);
      return;
  }
}

void Value::Convert(TypeName target_type, RoundMode round_mode) {
  // TODO(benvanik): big matrix.
  assert_always();
}

void Value::Round(RoundMode round_mode) {
  // TODO(benvanik): big matrix.
  assert_always();
}

bool Value::Add(Value* other) {
#define CHECK_DID_CARRY(v1, v2) (((uint64_t)v2) > ~((uint64_t)v1))
#define ADD_DID_CARRY(a, b) CHECK_DID_CARRY(a, b)
  assert_true(type == other->type);
  bool did_carry = false;
  switch (type) {
    case INT8_TYPE:
      did_carry = ADD_DID_CARRY(constant.i8, other->constant.i8);
      constant.i8 += other->constant.i8;
      break;
    case INT16_TYPE:
      did_carry = ADD_DID_CARRY(constant.i16, other->constant.i16);
      constant.i16 += other->constant.i16;
      break;
    case INT32_TYPE:
      did_carry = ADD_DID_CARRY(constant.i32, other->constant.i32);
      constant.i32 += other->constant.i32;
      break;
    case INT64_TYPE:
      did_carry = ADD_DID_CARRY(constant.i64, other->constant.i64);
      constant.i64 += other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 += other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 += other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
  return did_carry;
}

bool Value::Sub(Value* other) {
#define SUB_DID_CARRY(a, b) (b == 0 || a > (~(0 - b)))
  assert_true(type == other->type);
  bool did_carry = false;
  switch (type) {
    case INT8_TYPE:
      did_carry =
          SUB_DID_CARRY(uint16_t(constant.i8), uint16_t(other->constant.i8));
      constant.i8 -= other->constant.i8;
      break;
    case INT16_TYPE:
      did_carry =
          SUB_DID_CARRY(uint16_t(constant.i16), uint16_t(other->constant.i16));
      constant.i16 -= other->constant.i16;
      break;
    case INT32_TYPE:
      did_carry =
          SUB_DID_CARRY(uint32_t(constant.i32), uint32_t(other->constant.i32));
      constant.i32 -= other->constant.i32;
      break;
    case INT64_TYPE:
      did_carry =
          SUB_DID_CARRY(uint64_t(constant.i64), uint64_t(other->constant.i64));
      constant.i64 -= other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 -= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 -= other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
  return did_carry;
}

void Value::Mul(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 *= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 *= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 *= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 *= other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 *= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 *= other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::MulHi(Value* other, bool is_unsigned) {
  assert_true(type == other->type);
  switch (type) {
    case INT32_TYPE:
      if (is_unsigned) {
        constant.i32 = (int32_t)(((uint64_t)((uint32_t)constant.i32) *
                                  (uint32_t)other->constant.i32) >>
                                 32);
      } else {
        constant.i32 = (int32_t)(
            ((int64_t)constant.i32 * (int64_t)other->constant.i32) >> 32);
      }
      break;
    case INT64_TYPE:
      if (is_unsigned) {
        constant.i64 = __umulh(constant.i64, other->constant.i64);
      } else {
        constant.i64 = __mulh(constant.i64, other->constant.i64);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Div(Value* other, bool is_unsigned) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      if (is_unsigned) {
        constant.i8 /= uint8_t(other->constant.i8);
      } else {
        constant.i8 /= other->constant.i8;
      }
      break;
    case INT16_TYPE:
      if (is_unsigned) {
        constant.i16 /= uint16_t(other->constant.i16);
      } else {
        constant.i16 /= other->constant.i16;
      }
      break;
    case INT32_TYPE:
      if (is_unsigned) {
        constant.i32 /= uint32_t(other->constant.i32);
      } else {
        constant.i32 /= other->constant.i32;
      }
      break;
    case INT64_TYPE:
      if (is_unsigned) {
        constant.i64 /= uint64_t(other->constant.i64);
      } else {
        constant.i64 /= other->constant.i64;
      }
      break;
    case FLOAT32_TYPE:
      constant.f32 /= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 /= other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::MulAdd(Value* dest, Value* value1, Value* value2, Value* value3) {
  // TODO(benvanik): big matrix.
  assert_always();
}

void Value::MulSub(Value* dest, Value* value1, Value* value2, Value* value3) {
  // TODO(benvanik): big matrix.
  assert_always();
}

void Value::Neg() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = -constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = -constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 = -constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 = -constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 = -constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 = -constant.f64;
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; ++i) {
        constant.v128.f32[i] = -constant.v128.f32[i];
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Abs() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = int8_t(std::abs(constant.i8));
      break;
    case INT16_TYPE:
      constant.i16 = int16_t(std::abs(constant.i16));
      break;
    case INT32_TYPE:
      constant.i32 = std::abs(constant.i32);
      break;
    case INT64_TYPE:
      constant.i64 = std::abs(constant.i64);
      break;
    case FLOAT32_TYPE:
      constant.f32 = std::abs(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = std::abs(constant.f64);
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; ++i) {
        constant.v128.f32[i] = std::fabsf(constant.v128.f32[i]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Sqrt() {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = 1.0f / std::sqrtf(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = 1.0 / std::sqrt(constant.f64);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::RSqrt() {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = std::sqrt(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = std::sqrt(constant.f64);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::And(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 &= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 &= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 &= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 &= other->constant.i64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Or(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 |= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 |= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 |= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 |= other->constant.i64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Xor(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 ^= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 ^= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 ^= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 ^= other->constant.i64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Not() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = ~constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = ~constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 = ~constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 = ~constant.i64;
      break;
    case VEC128_TYPE:
      constant.v128.low = ~constant.v128.low;
      constant.v128.high = ~constant.v128.high;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Shl(Value* other) {
  assert_true(other->type == INT8_TYPE);
  switch (type) {
    case INT8_TYPE:
      constant.i8 <<= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 <<= other->constant.i8;
      break;
    case INT32_TYPE:
      constant.i32 <<= other->constant.i8;
      break;
    case INT64_TYPE:
      constant.i64 <<= other->constant.i8;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Shr(Value* other) {
  assert_true(other->type == INT8_TYPE);
  switch (type) {
    case INT8_TYPE:
      constant.i8 = (uint8_t)constant.i8 >> other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = (uint16_t)constant.i16 >> other->constant.i8;
      break;
    case INT32_TYPE:
      constant.i32 = (uint32_t)constant.i32 >> other->constant.i8;
      break;
    case INT64_TYPE:
      constant.i64 = (uint64_t)constant.i64 >> other->constant.i8;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Sha(Value* other) {
  assert_true(other->type == INT8_TYPE);
  switch (type) {
    case INT8_TYPE:
      constant.i8 = constant.i8 >> other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = constant.i16 >> other->constant.i8;
      break;
    case INT32_TYPE:
      constant.i32 = constant.i32 >> other->constant.i8;
      break;
    case INT64_TYPE:
      constant.i64 = constant.i64 >> other->constant.i8;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::ByteSwap() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = xe::byte_swap(constant.i16);
      break;
    case INT32_TYPE:
      constant.i32 = xe::byte_swap(constant.i32);
      break;
    case INT64_TYPE:
      constant.i64 = xe::byte_swap(constant.i64);
      break;
    case VEC128_TYPE:
      for (int n = 0; n < 4; n++) {
        constant.v128.u32[n] = xe::byte_swap(constant.v128.u32[n]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::CountLeadingZeros(const Value* other) {
  switch (other->type) {
    case INT8_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i8);
      break;
    case INT16_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i16);
      break;
    case INT32_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i32);
      break;
    case INT64_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i64);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

bool Value::Compare(Opcode opcode, Value* other) {
  assert_true(type == other->type);
  switch (other->type) {
    case INT8_TYPE:
      return CompareInt8(opcode, this, other);
    case INT16_TYPE:
      return CompareInt16(opcode, this, other);
    case INT32_TYPE:
      return CompareInt32(opcode, this, other);
    case INT64_TYPE:
      return CompareInt64(opcode, this, other);
    default:
      assert_unhandled_case(type);
      return false;
  }
}

bool Value::CompareInt8(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i8 == b->constant.i8;
    case OPCODE_COMPARE_NE:
      return a->constant.i8 != b->constant.i8;
    case OPCODE_COMPARE_SLT:
      return a->constant.i8 < b->constant.i8;
    case OPCODE_COMPARE_SLE:
      return a->constant.i8 <= b->constant.i8;
    case OPCODE_COMPARE_SGT:
      return a->constant.i8 > b->constant.i8;
    case OPCODE_COMPARE_SGE:
      return a->constant.i8 >= b->constant.i8;
    case OPCODE_COMPARE_ULT:
      return uint8_t(a->constant.i8) < uint8_t(b->constant.i8);
    case OPCODE_COMPARE_ULE:
      return uint8_t(a->constant.i8) <= uint8_t(b->constant.i8);
    case OPCODE_COMPARE_UGT:
      return uint8_t(a->constant.i8) > uint8_t(b->constant.i8);
    case OPCODE_COMPARE_UGE:
      return uint8_t(a->constant.i8) >= uint8_t(b->constant.i8);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt16(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i16 == b->constant.i16;
    case OPCODE_COMPARE_NE:
      return a->constant.i16 != b->constant.i16;
    case OPCODE_COMPARE_SLT:
      return a->constant.i16 < b->constant.i16;
    case OPCODE_COMPARE_SLE:
      return a->constant.i16 <= b->constant.i16;
    case OPCODE_COMPARE_SGT:
      return a->constant.i16 > b->constant.i16;
    case OPCODE_COMPARE_SGE:
      return a->constant.i16 >= b->constant.i16;
    case OPCODE_COMPARE_ULT:
      return uint16_t(a->constant.i16) < uint16_t(b->constant.i16);
    case OPCODE_COMPARE_ULE:
      return uint16_t(a->constant.i16) <= uint16_t(b->constant.i16);
    case OPCODE_COMPARE_UGT:
      return uint16_t(a->constant.i16) > uint16_t(b->constant.i16);
    case OPCODE_COMPARE_UGE:
      return uint16_t(a->constant.i16) >= uint16_t(b->constant.i16);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt32(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i32 == b->constant.i32;
    case OPCODE_COMPARE_NE:
      return a->constant.i32 != b->constant.i32;
    case OPCODE_COMPARE_SLT:
      return a->constant.i32 < b->constant.i32;
    case OPCODE_COMPARE_SLE:
      return a->constant.i32 <= b->constant.i32;
    case OPCODE_COMPARE_SGT:
      return a->constant.i32 > b->constant.i32;
    case OPCODE_COMPARE_SGE:
      return a->constant.i32 >= b->constant.i32;
    case OPCODE_COMPARE_ULT:
      return uint32_t(a->constant.i32) < uint32_t(b->constant.i32);
    case OPCODE_COMPARE_ULE:
      return uint32_t(a->constant.i32) <= uint32_t(b->constant.i32);
    case OPCODE_COMPARE_UGT:
      return uint32_t(a->constant.i32) > uint32_t(b->constant.i32);
    case OPCODE_COMPARE_UGE:
      return uint32_t(a->constant.i32) >= uint32_t(b->constant.i32);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt64(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i64 == b->constant.i64;
    case OPCODE_COMPARE_NE:
      return a->constant.i64 != b->constant.i64;
    case OPCODE_COMPARE_SLT:
      return a->constant.i64 < b->constant.i64;
    case OPCODE_COMPARE_SLE:
      return a->constant.i64 <= b->constant.i64;
    case OPCODE_COMPARE_SGT:
      return a->constant.i64 > b->constant.i64;
    case OPCODE_COMPARE_SGE:
      return a->constant.i64 >= b->constant.i64;
    case OPCODE_COMPARE_ULT:
      return uint64_t(a->constant.i64) < uint64_t(b->constant.i64);
    case OPCODE_COMPARE_ULE:
      return uint64_t(a->constant.i64) <= uint64_t(b->constant.i64);
    case OPCODE_COMPARE_UGT:
      return uint64_t(a->constant.i64) > uint64_t(b->constant.i64);
    case OPCODE_COMPARE_UGE:
      return uint64_t(a->constant.i64) >= uint64_t(b->constant.i64);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

}  // namespace hir
}  // namespace cpu
}  // namespace xe
