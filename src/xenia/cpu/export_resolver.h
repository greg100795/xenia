/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_EXPORT_RESOLVER_H_
#define XENIA_CPU_EXPORT_RESOLVER_H_

#include <string>
#include <vector>

#include "xenia/base/math.h"
#include "xenia/cpu/frontend/ppc_context.h"

namespace xe {
namespace cpu {

struct ExportTag {
  typedef uint32_t type;

  // Export is implemented in some form and can be used.
  static const type kImplemented = 1u << 0;
  // Export is a stub and is probably bad.
  static const type kStub = 1u << 1;
  // Export is known to cause problems, or may not be complete.
  static const type kSketchy = 1u << 2;
  // Export is called *a lot*.
  static const type kHighFrequency = 1u << 3;
  // Export is important and should always be logged.
  static const type kImportant = 1u << 4;

  static const type kThreading = 1u << 10;
  static const type kInput = 1u << 11;
  static const type kAudio = 1u << 12;
  static const type kVideo = 1u << 13;
  static const type kFileSystem = 1u << 14;
  static const type kModules = 1u << 15;
  static const type kUserProfiles = 1u << 16;
  static const type kNetworking = 1u << 17;
  static const type kMemory = 1u << 18;

  // Export will be logged on each call.
  static const type kLog = 1u << 30;
  // Export's result will be logged on each call.
  static const type kLogResult = 1u << 31;
};

// DEPRECATED
typedef void (*xe_kernel_export_shim_fn)(void*, void*);

typedef void (*ExportTrampoline)(xe::cpu::frontend::PPCContext* ppc_context);

class Export {
 public:
  enum class Type {
    kFunction = 0,
    kVariable = 1,
  };

  Export(uint16_t ordinal, Type type, const char* name,
         ExportTag::type tags = 0)
      : ordinal(ordinal),
        type(type),
        tags(tags),
        function_data({nullptr, nullptr, 0}) {
    std::strncpy(this->name, name, xe::countof(this->name));
  }

  uint16_t ordinal;
  Type type;
  char name[96];
  ExportTag::type tags;

  bool is_implemented() const {
    return (tags & ExportTag::kImplemented) == ExportTag::kImplemented;
  }

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    // This is an address in the client memory space that the variable can
    // be found at.
    uint32_t variable_ptr;

    struct {
      // DEPRECATED
      xe_kernel_export_shim_fn shim;

      // Trampoline that is called from the guest-to-host thunk.
      // Expects only PPC context as first arg.
      ExportTrampoline trampoline;
      uint64_t call_count;
    } function_data;
  };
};

class ExportResolver {
 public:
  ExportResolver();
  ~ExportResolver();

  void RegisterTable(const std::string& library_name,
                     const std::vector<Export*>* exports);

  Export* GetExportByOrdinal(const std::string& library_name, uint16_t ordinal);

  void SetVariableMapping(const std::string& library_name, uint16_t ordinal,
                          uint32_t value);
  void SetFunctionMapping(const std::string& library_name, uint16_t ordinal,
                          xe_kernel_export_shim_fn shim);
  void SetFunctionMapping(const std::string& library_name, uint16_t ordinal,
                          ExportTrampoline trampoline);

 private:
  struct ExportTable {
    std::string name;
    std::string simple_name;  // without extension
    const std::vector<Export*>* exports;
    ExportTable(const std::string& name, const std::vector<Export*>* exports)
        : name(name), exports(exports) {
      auto dot_pos = name.find_last_of('.');
      if (dot_pos != std::string::npos) {
        simple_name = name.substr(0, dot_pos);
      } else {
        simple_name = name;
      }
    }
  };
  std::vector<ExportTable> tables_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_EXPORT_RESOLVER_H_
