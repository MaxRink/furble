#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Furble::Sim {

/**
 * Extract the owning process id from a generated preferences store name.
 *
 * Generated stores are `furble-sim-preferences-<scenario>-<pid>-XXXXXX.bin`.
 * Scenario names may contain dashes, so the pid is parsed from the right.
 * A `.bin.tmp.<pid>` sidecar is parsed from its trailing pid. Returns nullopt
 * for an unrelated or malformed name.
 */
std::optional<int> generatedPreferencePid(const std::string &filename);

/** True when a process with this id exists (including a foreign owner). */
bool processAlive(int pid);

/**
 * Remove generated preference stores left by processes that no longer exist.
 *
 * Only regular files owned by the current user and matching the generated
 * naming scheme are removed. A live pid always keeps its store, even when the
 * pid has been reused; an encoded pid cannot prove more than liveness, so the
 * sweep is deliberately conservative.
 *
 * Returns the number of files removed.
 */
std::size_t sweepStalePreferences(const std::filesystem::path &directory, int selfPid);

}  // namespace Furble::Sim
