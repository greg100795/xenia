/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_
#define XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

struct X_KSEMAPHORE {
  X_DISPATCH_HEADER header;
  xe::be<uint32_t> limit;
};

class XSemaphore : public XObject {
 public:
  XSemaphore(KernelState* kernel_state);
  ~XSemaphore() override;

  void Initialize(int32_t initial_count, int32_t maximum_count);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER& header);

  int32_t ReleaseSemaphore(int32_t release_count);

  xe::threading::WaitHandle* GetWaitHandle() override {
    return semaphore_.get();
  }

 private:
  std::unique_ptr<xe::threading::Semaphore> semaphore_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XSEMAPHORE_H_
