// Host conformance for the production SD writer (src/FurbleSD.cpp) and GPX
// writer (src/FurbleGPX.cpp).
//
// The production writer task, mount decision, settings export/import surface
// and GPX encoder run unchanged. The SD/SPI host layer is a double that maps
// the firmware mount point to a build-directory folder, so real POSIX file I/O
// and the GPIO configuration are exercised.

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "FurbleGPX.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "M5Unified.h"
#include "boundary_host.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "nvs.h"

#ifndef FURBLE_BOUNDARY_SD_MOUNT
#error "FURBLE_BOUNDARY_SD_MOUNT must name the host mount directory"
#endif

namespace {

constexpr const char *MOUNT = FURBLE_BOUNDARY_SD_MOUNT;
constexpr const char *GPX_DIRECTORY = FURBLE_BOUNDARY_SD_MOUNT "/furble";

int g_Failures = 0;
bool g_MountFail = false;
bool g_CardRemoved = false;
bool g_Mounted = false;
uint32_t g_MountCalls = 0;
uint32_t g_UnmountCalls = 0;
sdmmc_card_t g_Card;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "SD FAIL: " << message << '\n';
    g_Failures++;
  }
}

void sleepMs(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool waitFor(Furble::SD::card_state_t state, uint32_t timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (Furble::SD::getInstance().cardState() == state) {
      return true;
    }
    sleepMs(10);
  }
  return Furble::SD::getInstance().cardState() == state;
}

bool waitForLog(const char *needle, uint32_t timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (BoundaryHost::logContains(needle)) {
      return true;
    }
    sleepMs(10);
  }
  return BoundaryHost::logContains(needle);
}

void clearMount(void) {
  std::error_code error;
  std::filesystem::remove_all(MOUNT, error);
  std::filesystem::create_directories(MOUNT, error);
}

void setupBoard(void) {
  M5.board = m5::board_t::board_M5Stack;
  M5.sdCsPin = 4;
  static lgfx::Bus_SPI bus;
  bus.setHost(1);
  M5.Display.panel.setBus(&bus);
}

void initializeSettings(bool gpx) {
  nvs_test_reset();
  Furble::Settings::init();
  Furble::Settings::save<bool>(Furble::Settings::SD_GPX, gpx);
  Furble::Settings::save<uint16_t>(Furble::Settings::GPX_PERIOD, 5);
}

Furble::GPX::point_t point(uint8_t minute, double latitude, double longitude) {
  Furble::GPX::point_t value = {};
  value.latitude = latitude;
  value.longitude = longitude;
  value.altitude = 34.5;
  value.altitude_valid = true;
  value.satellites = 9;
  value.year = 2026;
  value.month = 11;
  value.day = 5;
  value.hour = 12;
  value.minute = minute;
  value.second = 0;
  return value;
}

std::string gpxPath(uint8_t minute) {
  char path[128] = {};
  std::snprintf(path, sizeof(path), "%s/20261105-12%02u00.gpx", GPX_DIRECTORY, minute);
  return path;
}

// The SD task writes the header, the point and the closers in separate calls,
// so a non-empty file is not yet a complete one. Wait for a point and the
// closing tag; a slow host otherwise reads or measures a partial file.
bool fileIsComplete(const std::string &path) {
  std::ifstream file(path);
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  return contents.find("<trkpt") != std::string::npos
         && contents.find("</gpx>") != std::string::npos;
}

bool waitForFile(const std::string &path, uint32_t timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (fileIsComplete(path)) {
      return true;
    }
    sleepMs(10);
  }
  return fileIsComplete(path);
}

void runMountWrite(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED), "SD card did not mount");
  check(g_MountCalls == 1, "the mount double was not called exactly once");
  check(Furble::SD::getInstance().isSupported(), "the SD slot was not detected on the Core model");
  check(BoundaryHost::logContains("SD card mounted."), "the production mount log is missing");
  std::cout << "sd.mount state=mounted calls=" << g_MountCalls << '\n';

  const auto first = point(0, 52.52, 13.405);
  check(Furble::SD::getInstance().logPoint(first), "the first GPX point was not queued");
  const std::string path = gpxPath(0);
  check(waitForFile(path), "the GPX file was not created at the dated path");

  std::ifstream file(path);
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  check(contents.find("<gpx version=\"1.1\"") != std::string::npos, "the GPX header is missing");
  check(contents.find("<trkpt lat=\"52.520000\" lon=\"13.405000\">") != std::string::npos,
        "the GPX track point is missing or has the wrong coordinates");
  check(contents.find("<ele>34.5</ele>") != std::string::npos, "the GPX altitude is missing");
  check(contents.find("<sat>9</sat>") != std::string::npos, "the GPX satellite count is missing");
  check(contents.find("</gpx>") != std::string::npos, "the GPX closer is missing");
  check(Furble::SD::getInstance().capacityMB() > 0, "the SD capacity was not published");
  check(Furble::SD::getInstance().generation() > 0, "the SD generation did not advance");
  std::cout << "sd.write path=" << path << " bytes=" << contents.size() << '\n';
  std::cout << "sd.card capacity_mb=" << Furble::SD::getInstance().capacityMB()
            << " free_mb=" << Furble::SD::getInstance().freeMB()
            << " generation=" << Furble::SD::getInstance().generation() << '\n';

  Furble::SD::getInstance().powerOff();
  check(waitFor(Furble::SD::card_state_t::UNMOUNTED), "power off did not unmount the card");
  check(g_UnmountCalls == 1, "the unmount double was not called exactly once");
  std::cout << "sd.power_off unmounted=1 calls=" << g_UnmountCalls << '\n';
}

void runMountFail(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();
  g_MountFail = true;

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::FAILED), "a failed mount did not publish FAILED");
  check(BoundaryHost::logContains("SD card mount failed: ESP_FAIL"),
        "the mount failure log is missing");
  check(g_MountCalls > 0, "the mount double was not called");
  std::cout << "sd.mount_fail state=failed calls=" << g_MountCalls << '\n';
}

void runFull(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED), "SD card did not mount for the full case");
  const auto first = point(1, 52.52, 13.405);
  check(Furble::SD::getInstance().logPoint(first), "the first GPX point was not queued");
  const std::string path = gpxPath(1);
  check(waitForFile(path), "the full-case GPX file was not created");

  struct stat info = {};
  check(stat(path.c_str(), &info) == 0, "the full-case GPX file could not be measured");
  signal(SIGXFSZ, SIG_IGN);
  struct rlimit limit = {};
  limit.rlim_cur = static_cast<rlim_t>(info.st_size + 16);
  limit.rlim_max = limit.rlim_cur;
  check(setrlimit(RLIMIT_FSIZE, &limit) == 0, "the host file-size limit could not be installed");

  const auto second = point(2, 52.53, 13.406);
  check(Furble::SD::getInstance().logPoint(second), "the second GPX point was not queued");
  check(waitForLog("GPX locate closers failed: File too large"),
        "a full card did not reach the production GPX failure");
  std::cout << "sd.full write_failed=1 disabled=0\n";

  limit.rlim_cur = RLIM_INFINITY;
  limit.rlim_max = RLIM_INFINITY;
  (void)setrlimit(RLIMIT_FSIZE, &limit);
}

void runRemoved(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED), "SD card did not mount before removal");
  check(Furble::SD::getInstance().logPoint(point(3, 52.52, 13.405)),
        "the pre-removal point was not queued");
  const std::string path = gpxPath(3);
  check(waitForFile(path), "the pre-removal GPX file was not created");

  Furble::SD::getInstance().powerOff();
  check(waitFor(Furble::SD::card_state_t::UNMOUNTED), "power off did not unmount before removal");
  g_CardRemoved = true;
  std::error_code error;
  std::filesystem::remove_all(MOUNT, error);

  for (int attempt = 0; attempt < 3; attempt++) {
    check(Furble::SD::getInstance().logPoint(point(4 + attempt, 52.52, 13.405)),
          "a removed-card point was not queued");
    sleepMs(50);
  }
  check(waitForLog("Disabling GPX logging after repeated SD failures."),
        "three failures on a removed card did not disable GPX logging");
  check(!Furble::Settings::load<bool>(Furble::Settings::SD_GPX),
        "the production failure path did not persist SD_GPX false");
  std::cout << "sd.removed mount_failures=3 gpx_disabled=1\n";
}

}  // namespace

extern "C" {

esp_err_t esp_vfs_fat_sdspi_mount(const char *base_path,
                                  const sdmmc_host_t *host_config,
                                  const sdspi_device_config_t *slot_config,
                                  const esp_vfs_fat_mount_config_t *,
                                  sdmmc_card_t **out_card) {
  g_MountCalls++;
  check(base_path != nullptr && std::strcmp(base_path, MOUNT) == 0,
        "the production mount point did not match the host mount");
  check(host_config != nullptr && host_config->max_freq_khz == 10000,
        "the production SD SPI frequency changed");
  check(slot_config != nullptr && slot_config->gpio_cs == 4,
        "the production SD chip select changed");
  if (g_MountFail || g_CardRemoved) {
    return ESP_FAIL;
  }
  std::error_code error;
  std::filesystem::create_directories(base_path, error);
  if (error) {
    return ESP_FAIL;
  }
  g_Card.csd.capacity = 67108864U;  // 32 GiB at 512-byte sectors
  g_Card.csd.sector_size = 512;
  *out_card = &g_Card;
  g_Mounted = true;
  return ESP_OK;
}

esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path, sdmmc_card_t *card) {
  g_UnmountCalls++;
  check(base_path != nullptr && std::strcmp(base_path, MOUNT) == 0,
        "the production unmount point did not match the host mount");
  check(card == &g_Card, "the production unmount did not pass the mounted card");
  g_Mounted = false;
  return ESP_OK;
}

esp_err_t esp_vfs_fat_info(const char *base_path,
                           uint64_t *out_total_bytes,
                           uint64_t *out_free_bytes) {
  if (!g_Mounted || base_path == nullptr || std::strcmp(base_path, MOUNT) != 0) {
    return ESP_FAIL;
  }
  *out_total_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
  *out_free_bytes = 30ULL * 1024ULL * 1024ULL * 1024ULL;
  return ESP_OK;
}

}  // extern "C"

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "mount-write";

  if (mode == "mount-write") {
    runMountWrite();
  } else if (mode == "mount-fail") {
    runMountFail();
  } else if (mode == "full") {
    runFull();
  } else if (mode == "removed") {
    runRemoved();
  } else {
    std::cerr << "unknown SD boundary mode: " << mode << '\n';
    return 2;
  }

  furbleHostStopTasks();
  std::error_code error;
  std::filesystem::remove_all(MOUNT, error);
  if (g_Failures != 0) {
    std::cerr << "sd_driver_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "sd_driver_test: PASS (" << mode << ")\n";
  return 0;
}
