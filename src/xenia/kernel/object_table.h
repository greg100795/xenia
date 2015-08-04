/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_
#define XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "xenia/base/mutex.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class ObjectTable {
 public:
  ObjectTable();
  ~ObjectTable();

  X_STATUS AddHandle(XObject* object, X_HANDLE* out_handle);
  X_STATUS DuplicateHandle(X_HANDLE orig, X_HANDLE* out_handle);
  X_STATUS RetainHandle(X_HANDLE handle);
  X_STATUS ReleaseHandle(X_HANDLE handle);
  X_STATUS RemoveHandle(X_HANDLE handle);

  template <typename T>
  object_ref<T> LookupObject(X_HANDLE handle) {
    auto object = LookupObject(handle, false);
    auto result = object_ref<T>(reinterpret_cast<T*>(object));
    return result;
  }

  X_STATUS AddNameMapping(const std::string& name, X_HANDLE handle);
  void RemoveNameMapping(const std::string& name);
  X_STATUS GetObjectByName(const std::string& name, X_HANDLE* out_handle);
  template <typename T>
  std::vector<object_ref<T>> GetObjectsByType(XObject::Type type) {
    std::vector<object_ref<T>> results;
    GetObjectsByType(
        type, *reinterpret_cast<std::vector<object_ref<XObject>>*>(&results));
    return results;
  }

 private:
  typedef struct {
    int handle_ref_count = 0;
    XObject* object = nullptr;
  } ObjectTableEntry;

  ObjectTableEntry* LookupTable(X_HANDLE handle);
  XObject* LookupObject(X_HANDLE handle, bool already_locked);
  void GetObjectsByType(XObject::Type type,
                        std::vector<object_ref<XObject>>& results);

  X_HANDLE TranslateHandle(X_HANDLE handle);
  X_STATUS FindFreeSlot(uint32_t* out_slot);

  xe::recursive_mutex table_mutex_;
  uint32_t table_capacity_;
  ObjectTableEntry* table_;
  uint32_t last_free_entry_;
  std::unordered_map<std::string, X_HANDLE> name_table_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_
