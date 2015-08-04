/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_TEST_MODULE_H_
#define XENIA_CPU_TEST_MODULE_H_

#include <functional>
#include <memory>
#include <string>

#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/module.h"

namespace xe {
namespace cpu {

class TestModule : public Module {
 public:
  TestModule(Processor* processor, const std::string& name,
             std::function<bool(uint32_t)> contains_address,
             std::function<bool(hir::HIRBuilder&)> generate);
  ~TestModule() override;

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint32_t address) override;

  SymbolStatus DeclareFunction(uint32_t address,
                               FunctionInfo** out_symbol_info) override;

 private:
  std::string name_;
  std::function<bool(uint32_t)> contains_address_;
  std::function<bool(hir::HIRBuilder&)> generate_;

  std::unique_ptr<hir::HIRBuilder> builder_;
  std::unique_ptr<compiler::Compiler> compiler_;
  std::unique_ptr<backend::Assembler> assembler_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_TEST_MODULE_H_
