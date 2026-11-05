#ifndef FURBLE_SETTINGS_BOOTSTRAP_H
#define FURBLE_SETTINGS_BOOTSTRAP_H

#include <cstddef>
#include <cstdint>

namespace Furble {
namespace SettingsBootstrap {

/**
 * Version of the first-boot default set.
 *
 * Bump this whenever a new setting is added, a default changes, or a default
 * becomes board conditional. An existing device then runs one full missing-key
 * scan on the next boot and writes only the keys it does not have. A device
 * with the current version skips the scan entirely.
 */
constexpr uint32_t DEFAULTS_VERSION = 1;

/** The number of NVS namespaces the batched seeder can hold open at once. */
constexpr size_t MAX_BATCH_NAMESPACES = 8;

/**
 * True when the stored marker does not match this firmware's default set.
 *
 * A missing marker reads as zero, so a device upgraded from a build without
 * the marker always scans once. The marker is written only after every default
 * commit succeeds, so an interrupted seed is retried on the next boot.
 */
constexpr bool needsDefaultScan(uint32_t stored_version, uint32_t current_version) {
  return stored_version != current_version;
}

}  // namespace SettingsBootstrap
}  // namespace Furble

#endif  // FURBLE_SETTINGS_BOOTSTRAP_H
