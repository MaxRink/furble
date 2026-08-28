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
  std::set<size_t> failOrdinals;
  std::vector<size_t> failedOrdinals;

  bool shouldFail(const char *kind, const char *key) {
    const std::string label = std::string(kind) + ":" + key;
    trace.push_back({kind, key, trace.size()});
    const size_t ordinal = trace.back().ordinal;
    if (failOrdinals.count(ordinal) != 0) {
      failedOrdinals.push_back(ordinal);
      return true;
    }
    return false;
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
  // Fail-safe contract for the boot origin: an empty store, a true first boot
  // or a marker lost to power failure, must never report a clean restart, so
  // the first connect keeps the patient peer backoff. The Control-side half
  // of this contract lives in tests/host/reconnect_initiator_test.cpp, whose
  // boot test fails if the default origin ever turns fast again.
  FaultStorage neverMarked;
  assert(!Furble::RestartMarker::consume(neverMarked));

  // Derive the complete successful mark schedule and fault every observed
  // boundary, including the final commit readback (ordinal 4).
  FaultStorage markProbe;
  assert(Furble::RestartMarker::mark(markProbe));
  assertTrace(markProbe.trace, {"exists:cr_boot_gen", "write:cr_pending", "read:cr_pending",
                                "write:cr_commit", "read:cr_commit"});
  for (const auto &operation : markProbe.trace) {
    FaultStorage fault;
    fault.failOrdinals.insert(operation.ordinal);
    assert(!Furble::RestartMarker::mark(fault));
    assert(fault.failedOrdinals == std::vector<size_t> {operation.ordinal});
    fault.failOrdinals.clear();
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
    fault.failOrdinals.insert(operation.ordinal);
    assert(!Furble::RestartMarker::consume(fault));
    assert(fault.failedOrdinals == std::vector<size_t> {operation.ordinal});
    fault.failOrdinals.clear();
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
    fault.failOrdinals.insert(operation.ordinal);
    assert(!Furble::RestartMarker::mark(fault));
    assert(fault.failedOrdinals == std::vector<size_t> {operation.ordinal});
    fault.failOrdinals.clear();
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
    fault.failOrdinals.insert(operation.ordinal);
    assert(!Furble::RestartMarker::consume(fault));
    assert(fault.failedOrdinals == std::vector<size_t> {operation.ordinal});
    fault.failOrdinals.clear();
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
  FaultStorage poisonProbe = armedStorage();
  poisonProbe.failOrdinals.insert(3);  // exists:cr_poison
  assert(!Furble::RestartMarker::consume(poisonProbe));
  size_t poisonWriteOrdinal = SIZE_MAX;
  for (const auto &operation : poisonProbe.trace) {
    if (label(operation) == "write:cr_poison")
      poisonWriteOrdinal = operation.ordinal;
  }
  assert(poisonWriteOrdinal != SIZE_MAX);
  FaultStorage poison = armedStorage();
  poison.failOrdinals = {3, poisonWriteOrdinal};
  assert(!Furble::RestartMarker::consume(poison));
  const std::vector<size_t> expectedPoisonFailures = {3, poisonWriteOrdinal};
  assert(poison.failedOrdinals == expectedPoisonFailures);
  poison.failOrdinals.clear();
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));
  poison.trace.clear();
  assert(!Furble::RestartMarker::consume(poison));

  return 0;
}
