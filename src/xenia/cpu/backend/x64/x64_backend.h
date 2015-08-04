/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_X64_X64_BACKEND_H_
#define XENIA_BACKEND_X64_X64_BACKEND_H_

#include <gflags/gflags.h>

#include <memory>

#include "xenia/cpu/backend/backend.h"

DECLARE_bool(enable_haswell_instructions);

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64CodeCache;

#define XENIA_HAS_X64_BACKEND 1

typedef void* (*HostToGuestThunk)(void* target, void* arg0, void* arg1);
typedef void* (*GuestToHostThunk)(void* target, void* arg0, void* arg1);
typedef void (*ResolveFunctionThunk)();

class X64Backend : public Backend {
 public:
  const static uint32_t kForceReturnAddress = 0x9FFF0000u;

  X64Backend(Processor* processor);
  ~X64Backend() override;

  X64CodeCache* code_cache() const { return code_cache_.get(); }
  uint32_t emitter_data() const { return emitter_data_; }

  // Call a generated function, saving all stack parameters.
  HostToGuestThunk host_to_guest_thunk() const { return host_to_guest_thunk_; }
  // Function that guest code can call to transition into host code.
  GuestToHostThunk guest_to_host_thunk() const { return guest_to_host_thunk_; }
  // Function that thunks to the ResolveFunction in X64Emitter.
  ResolveFunctionThunk resolve_function_thunk() const {
    return resolve_function_thunk_;
  }

  bool Initialize() override;

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high) override;

  std::unique_ptr<Assembler> CreateAssembler() override;

 private:
  std::unique_ptr<X64CodeCache> code_cache_;

  uint32_t emitter_data_;

  HostToGuestThunk host_to_guest_thunk_;
  GuestToHostThunk guest_to_host_thunk_;
  ResolveFunctionThunk resolve_function_thunk_;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_X64_X64_BACKEND_H_
