/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/processor.h"

#include <gflags/gflags.h>

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/debug/debugger.h"
#include "xenia/profiling.h"

// TODO(benvanik): based on compiler support
#include "xenia/cpu/backend/x64/x64_backend.h"

namespace xe {
namespace cpu {

using PPCContext = xe::cpu::frontend::PPCContext;

class BuiltinModule : public Module {
 public:
  BuiltinModule(Processor* processor) : Module(processor), name_("builtin") {}
  const std::string& name() const override { return name_; }
  bool ContainsAddress(uint32_t address) override {
    return (address & 0xFFFFFFF0) == 0xFFFFFFF0;
  }

 private:
  std::string name_;
};

Processor::Processor(xe::Memory* memory, ExportResolver* export_resolver,
                     debug::Debugger* debugger)
    : memory_(memory), debugger_(debugger), export_resolver_(export_resolver) {}

Processor::~Processor() {
  {
    std::lock_guard<xe::mutex> guard(modules_lock_);
    modules_.clear();
  }

  frontend_.reset();
  backend_.reset();
}

bool Processor::Setup() {
  // TODO(benvanik): query mode from debugger?
  debug_info_flags_ = 0;

  auto frontend = std::make_unique<xe::cpu::frontend::PPCFrontend>(this);
  // TODO(benvanik): set options/etc.

  // Must be initialized by subclass before calling into this.
  assert_not_null(memory_);

  std::unique_ptr<Module> builtin_module(new BuiltinModule(this));
  builtin_module_ = builtin_module.get();
  modules_.push_back(std::move(builtin_module));

  if (frontend_ || backend_) {
    return false;
  }

  std::unique_ptr<xe::cpu::backend::Backend> backend;
  if (!backend) {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
    if (FLAGS_cpu == "x64") {
      backend.reset(new xe::cpu::backend::x64::X64Backend(this));
    }
#endif  // XENIA_HAS_X64_BACKEND
    if (FLAGS_cpu == "any") {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
      if (!backend) {
        backend.reset(new xe::cpu::backend::x64::X64Backend(this));
      }
#endif  // XENIA_HAS_X64_BACKEND
    }
  }

  if (!backend) {
    return false;
  }
  if (!backend->Initialize()) {
    return false;
  }
  if (!frontend->Initialize()) {
    return false;
  }

  backend_ = std::move(backend);
  frontend_ = std::move(frontend);

  // Stack walker is used when profiling, debugging, and dumping.
  stack_walker_ = StackWalker::Create(backend_->code_cache());
  if (!stack_walker_) {
    XELOGE("Unable to create stack walker");
    return false;
  }

  return true;
}

bool Processor::AddModule(std::unique_ptr<Module> module) {
  std::lock_guard<xe::mutex> guard(modules_lock_);
  modules_.push_back(std::move(module));
  return true;
}

Module* Processor::GetModule(const char* name) {
  std::lock_guard<xe::mutex> guard(modules_lock_);
  for (const auto& module : modules_) {
    if (module->name() == name) {
      return module.get();
    }
  }
  return nullptr;
}

std::vector<Module*> Processor::GetModules() {
  std::lock_guard<xe::mutex> guard(modules_lock_);
  std::vector<Module*> clone(modules_.size());
  for (const auto& module : modules_) {
    clone.push_back(module.get());
  }
  return clone;
}

FunctionInfo* Processor::DefineBuiltin(const std::string& name,
                                       FunctionInfo::BuiltinHandler handler,
                                       void* arg0, void* arg1) {
  uint32_t address = next_builtin_address_;
  next_builtin_address_ += 4;

  FunctionInfo* fn_info;
  builtin_module_->DeclareFunction(address, &fn_info);
  fn_info->set_end_address(address + 4);
  fn_info->set_name(name);
  fn_info->SetupBuiltin(handler, arg0, arg1);
  fn_info->set_status(SymbolStatus::kDeclared);

  return fn_info;
}

Function* Processor::QueryFunction(uint32_t address) {
  auto entry = entry_table_.Get(address);
  if (!entry) {
    return nullptr;
  }
  return entry->function;
}

std::vector<Function*> Processor::FindFunctionsWithAddress(uint32_t address) {
  return entry_table_.FindWithAddress(address);
}

bool Processor::ResolveFunction(uint32_t address, Function** out_function) {
  *out_function = nullptr;
  Entry* entry;
  Entry::Status status = entry_table_.GetOrCreate(address, &entry);
  if (status == Entry::STATUS_NEW) {
    // Needs to be generated. We have the 'lock' on it and must do so now.

    // Grab symbol declaration.
    FunctionInfo* symbol_info;
    if (!LookupFunctionInfo(address, &symbol_info)) {
      return false;
    }

    if (!DemandFunction(symbol_info, &entry->function)) {
      entry->status = Entry::STATUS_FAILED;
      return false;
    }
    entry->end_address = symbol_info->end_address();
    status = entry->status = Entry::STATUS_READY;
  }
  if (status == Entry::STATUS_READY) {
    // Ready to use.
    *out_function = entry->function;
    return true;
  } else {
    // Failed or bad state.
    return false;
  }
}

bool Processor::LookupFunctionInfo(uint32_t address,
                                   FunctionInfo** out_symbol_info) {
  *out_symbol_info = nullptr;

  // TODO(benvanik): fast reject invalid addresses/log errors.

  // Find the module that contains the address.
  Module* code_module = nullptr;
  {
    std::lock_guard<xe::mutex> guard(modules_lock_);
    // TODO(benvanik): sort by code address (if contiguous) so can bsearch.
    // TODO(benvanik): cache last module low/high, as likely to be in there.
    for (const auto& module : modules_) {
      if (module->ContainsAddress(address)) {
        code_module = module.get();
        break;
      }
    }
  }
  if (!code_module) {
    // No module found that could contain the address.
    return false;
  }

  return LookupFunctionInfo(code_module, address, out_symbol_info);
}

bool Processor::LookupFunctionInfo(Module* module, uint32_t address,
                                   FunctionInfo** out_symbol_info) {
  // Atomic create/lookup symbol in module.
  // If we get back the NEW flag we must declare it now.
  FunctionInfo* symbol_info = nullptr;
  SymbolStatus symbol_status = module->DeclareFunction(address, &symbol_info);
  if (symbol_status == SymbolStatus::kNew) {
    // Symbol is undeclared, so declare now.
    if (!frontend_->DeclareFunction(symbol_info)) {
      symbol_info->set_status(SymbolStatus::kFailed);
      return false;
    }
    symbol_info->set_status(SymbolStatus::kDeclared);
  }

  *out_symbol_info = symbol_info;
  return true;
}

bool Processor::DemandFunction(FunctionInfo* symbol_info,
                               Function** out_function) {
  *out_function = nullptr;

  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  Module* module = symbol_info->module();
  SymbolStatus symbol_status = module->DefineFunction(symbol_info);
  if (symbol_status == SymbolStatus::kNew) {
    // Symbol is undefined, so define now.
    Function* function = nullptr;
    if (!frontend_->DefineFunction(symbol_info, debug_info_flags_, &function)) {
      symbol_info->set_status(SymbolStatus::kFailed);
      return false;
    }
    symbol_info->set_function(function);

    // Before we give the symbol back to the rest, let the debugger know.
    if (debugger_) {
      debugger_->OnFunctionDefined(symbol_info, function);
    }

    symbol_info->set_status(SymbolStatus::kDefined);
    symbol_status = symbol_info->status();
  }

  if (symbol_status == SymbolStatus::kFailed) {
    // Symbol likely failed.
    return false;
  }

  *out_function = symbol_info->function();

  return true;
}

bool Processor::Execute(ThreadState* thread_state, uint32_t address) {
  SCOPE_profile_cpu_f("cpu");

  // Attempt to get the function.
  Function* fn;
  if (!ResolveFunction(address, &fn)) {
    // Symbol not found in any module.
    XELOGCPU("Execute(%.8X): failed to find function", address);
    return false;
  }

  PPCContext* context = thread_state->context();

  // Pad out stack a bit, as some games seem to overwrite the caller by about
  // 16 to 32b.
  context->r[1] -= 64 + 112;

  // This could be set to anything to give us a unique identifier to track
  // re-entrancy/etc.
  uint64_t previous_lr = context->lr;
  context->lr = 0xBCBCBCBC;

  // Execute the function.
  auto result = fn->Call(thread_state, uint32_t(context->lr));

  context->lr = previous_lr;
  context->r[1] += 64 + 112;

  return result;
}

uint64_t Processor::Execute(ThreadState* thread_state, uint32_t address,
                            uint64_t args[], size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  PPCContext* context = thread_state->context();
  assert_true(arg_count <= 5);
  for (size_t i = 0; i < arg_count; ++i) {
    context->r[3 + i] = args[i];
  }
  if (!Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

Irql Processor::RaiseIrql(Irql new_value) {
  return static_cast<Irql>(
      xe::atomic_exchange(static_cast<uint32_t>(new_value),
                          reinterpret_cast<volatile uint32_t*>(&irql_)));
}

void Processor::LowerIrql(Irql old_value) {
  xe::atomic_exchange(static_cast<uint32_t>(old_value),
                      reinterpret_cast<volatile uint32_t*>(&irql_));
}

}  // namespace cpu
}  // namespace xe
