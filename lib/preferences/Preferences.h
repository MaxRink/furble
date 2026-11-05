// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _FURBLE_PREFERENCES_H_
#define _FURBLE_PREFERENCES_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace Furble {

class Preferences {
 public:
  Preferences();
  ~Preferences();

  bool begin(const char *name, bool readOnly = false, const char *partition_label = NULL);
  void end();

  /**
   * Stage writes without committing each one. Call commit() once before end().
   *
   * The boot settings seed uses this to write one NVS namespace in a single
   * transaction instead of one commit per default. end() still commits pending
   * deferred writes if the caller forgets, so data is not silently dropped.
   */
  void setDeferredCommit(bool deferred);

  /** Commit staged writes. Returns true when there is nothing to write. */
  bool commit();

  bool clear();
  bool remove(const char *key);

  template <typename T>
  size_t put(const char *key, const T value);

  size_t put(const char *key, const char *value);
  /** Report set and commit failures, including when writing an empty string. */
  bool putString(const char *key, const char *value);
  size_t put(const char *key, const void *value, size_t len);

  template <typename T>
  T get(const char *key, T defaultValue = 0);
  enum class string_result_t : uint8_t { OK, NOT_FOUND, ERROR };

  /** Read a string and distinguish an unset key from NVS failures. */
  string_result_t getString(const char *key, std::string &value);
  size_t get(const char *key, void *buf, size_t maxLen);

  bool isKey(const char *key);
  size_t getBytesLength(const char *key);
  size_t freeEntries();

 private:
  /** Commit a set or erase, or stage it when deferred commits are enabled. */
  bool finishWrite(const char *key);

  typedef enum {
    PT_I8,
    PT_U8,
    PT_I16,
    PT_U16,
    PT_I32,
    PT_U32,
    PT_I64,
    PT_U64,
    PT_STR,
    PT_BLOB,
    PT_INVALID
  } PreferenceType;

  template <typename T>
  void nvs_get(uint32_t _handle, const char *key, T *value);

  PreferenceType getType(const char *key);

  uint32_t _handle;
  bool _started;
  bool _readOnly;
  bool _deferCommit;
  bool _dirty;
};

}  // namespace Furble

#endif
