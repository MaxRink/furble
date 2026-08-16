#ifndef FURBLE_SD_H
#define FURBLE_SD_H

#include <cstdint>

#include <sdmmc_cmd.h>

namespace Furble {
class SD {
 public:
  static SD &getInstance(void);

  SD(SD const &) = delete;
  SD(SD &&) = delete;
  SD &operator=(SD const &) = delete;
  SD &operator=(SD &&) = delete;

  /** Probe whether this board has the M5Stack SD slot. */
  bool isSupported(void) const;

  /** Probe the card and prepare the SD service. */
  static void init(void);

  /** Mount the card without initializing the display SPI bus. */
  bool mount(void);

  /** Unmount the card if it is mounted. */
  void unmount(void);

  /** Return whether the card filesystem is mounted. */
  bool isMounted(void) const;

  /** Return the card capacity in bytes, or zero when unavailable. */
  uint64_t capacityBytes(void) const;

  /** Return the free filesystem space in bytes, or zero when unavailable. */
  uint64_t freeBytes(void) const;

  /** Export all NVS settings to the text backup file. */
  bool exportSettings(void);

  /** Import recognized settings from the text backup file. */
  bool importSettings(void);

 private:
  SD() = default;

  bool m_Supported = false;
  bool m_Mounted = false;
  sdmmc_card_t *m_Card = nullptr;
};
}  // namespace Furble

#endif
