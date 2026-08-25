#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "FurbleRestartMarker.h"

using Storage = Furble::RestartMarkerStorage;

struct Operation {
  std::string kind;
  std::string key;
  size_t ordinal;
};

class FaultStorage final: public Storage {
 public:
  std::map<std::string, uint32_t> values;
  std::vector<Operation> trace;
  std::set<std::string> failLabels;

  bool shouldFail(const char *kind, const char *key) {
    const std::string label = std::string(kind) + ":" + key;
    trace.push_back({kind, key, trace.size()});
    return failLabels.count(label) != 0;
  }
  result read(const char *key, uint32_t &value) override {
    if (shouldFail("read", key))
      return result::ERROR;
    const auto it = values.find(key);
    if (it == values.end())
      return result::ABSENT;
    value = it->second;
    return result::PRESENT;
  }
  result write(const char *key, uint32_t value) override {
    const bool error = shouldFail("write", key);
    values[key] = value;
    return error ? result::ERROR : result::SUCCESS;
  }
  result remove(const char *key) override {
    const bool error = shouldFail("remove", key);
    values.erase(key);
    return error ? result::ERROR : result::SUCCESS;
  }
  result exists(const char *key) override {
    if (shouldFail("exists", key))
      return result::ERROR;
    return values.count(key) != 0 ? result::PRESENT : result::ABSENT;
  }
};

static FaultStorage armedStorage(void) {
  FaultStorage storage;
  assert(Furble::RestartMarker::mark(storage));
  storage.trace.clear();
  return storage;
}

static std::string label(const Operation &operation) {
  return operation.kind + ":" + operation.key;
}

static void assertTrace(const std::vector<Operation> &trace,
                        const std::vector<std::string> &expected) {
  assert(trace.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    assert(trace[i].ordinal == i);
    assert(label(trace[i]) == expected[i]);
  }
}

int main() {
  // Derive the complete successful mark schedule and fault every observed
  // boundary, including the final commit readback (ordinal 4).
  FaultStorage markProbe;
  assert(Furble::RestartMarker::mark(markProbe));
  assertTrace(markProbe.trace, {"exists:cr_boot_gen", "write:cr_pending", "read:cr_pending",
                                "write:cr_commit", "read:cr_commit"});
  for (const auto &operation : markProbe.trace) {
    FaultStorage fault;
    fault.failLabels.insert(label(operation));
    assert(!Furble::RestartMarker::mark(fault));
    fault.failLabels.clear();
    fault.trace.clear();
    assert(!Furble::RestartMarker::consume(fault));
  }

  // Derive the complete successful consume schedule and fault each labeled
  // operation, including the final pending existence check (ordinal 9).
  FaultStorage consumeProbe = armedStorage();
  assert(Furble::RestartMarker::consume(consumeProbe));
  assertTrace(consumeProbe.trace,
              {"exists:cr_boot_gen", "write:cr_boot_gen", "read:cr_boot_gen", "exists:cr_poison",
               "read:cr_pending", "read:cr_commit", "remove:cr_commit", "exists:cr_commit",
               "remove:cr_pending", "exists:cr_pending"});
  for (const auto &operation : consumeProbe.trace) {
    FaultStorage fault = armedStorage();
    fault.failLabels.insert(label(operation));
    assert(!Furble::RestartMarker::consume(fault));
    fault.failLabels.clear();
    fault.trace.clear();
    if (Furble::RestartMarker::consume(fault)) {
      std::fprintf(stderr, "generation consume resurrection: %s\\n", label(operation).c_str());
      return 1;
    }
  }

  // Repeat both schedules with an existing, valid boot generation. This
  // exercises the read:cr_boot_gen branches rather than only the empty-store
  // ABSENT branches above.
  FaultStorage presentMark;
  presentMark.values["cr_boot_gen"] = 41;
  assert(Furble::RestartMarker::mark(presentMark));
  assertTrace(presentMark.trace, {"exists:cr_boot_gen", "read:cr_boot_gen", "write:cr_pending",
                                  "read:cr_pending", "write:cr_commit", "read:cr_commit"});
  bool markGenerationReadInjected = false;
  for (const auto &operation : presentMark.trace) {
    markGenerationReadInjected |= label(operation) == "read:cr_boot_gen";
    FaultStorage fault;
    fault.values["cr_boot_gen"] = 41;
    fault.failLabels.insert(label(operation));
    assert(!Furble::RestartMarker::mark(fault));
    fault.failLabels.clear();
    fault.trace.clear();
    assert(!Furble::RestartMarker::consume(fault));
    fault.trace.clear();
    assert(!Furble::RestartMarker::consume(fault));
    fault.trace.clear();
    assert(!Furble::RestartMarker::consume(fault));
  }
  assert(markGenerationReadInjected);

  FaultStorage presentConsume = presentMark;
  presentConsume.trace.clear();
  assert(Furble::RestartMarker::consume(presentConsume));
  assertTrace(presentConsume.trace,
              {"exists:cr_boot_gen", "read:cr_boot_gen", "write:cr_boot_gen", "read:cr_boot_gen",
               "exists:cr_poison", "read:cr_pending", "read:cr_commit", "remove:cr_commit",
               "exists:cr_commit", "remove:cr_pending", "exists:cr_pending"});
  bool consumeGenerationReadInjected = false;
  for (const auto &operation : presentConsume.trace) {
    consumeGenerationReadInjected |= label(operation) == "read:cr_boot_gen";
    FaultStorage fault = presentMark;
    fault.trace.clear();
    fault.failLabels.insert(label(operation));
    assert(!Furble::RestartMarker::consume(fault));
    fault.failLabels.clear();
    fault.trace.clear();
    if (Furble::RestartMarker::consume(fault)) {
      std::fprintf(stderr, "generation consume resurrection: %s\\n", label(operation).c_str());
      return 1;
    }
    fault.trace.clear();
    assert(!Furble::RestartMarker::consume(fault));
  }
  assert(consumeGenerationReadInjected);

  // Target the poison write by its label, not by a positional magic bound.
  FaultStorage poison = armedStorage();
  poison.failLabels = {"exists:cr_poison", "write:cr_poison"};
  assert(!Furble::RestartMarker::consume(poison));
  bool poisonWriteSeen = false;
  for (const auto &operation : poison.trace) {
    poisonWriteSeen |= label(operation) == "write:cr_poison";
  }
  assert(poisonWriteSeen);
  poison.failLabels.clear();
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));

  return 0;
}
