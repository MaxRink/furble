// Host conformance for the production SD writer (src/FurbleSD.cpp) and GPX
// writer (src/FurbleGPX.cpp).
//
// The production writer task, mount decision, settings export/import surface
// and GPX encoder run unchanged. The SD/SPI host layer is a double that maps
// the firmware mount point to a build-directory folder, so real POSIX file I/O
// and the GPIO configuration are exercised. The double can also hold the
// unmount, which is how a hung writer that never answers the power-off
// handshake is represented.

#include <fcntl.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
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

// FatFs refuses an existing destination: ff.c f_rename() answers FR_EXIST when
// the name is already in use, and vfs_fat_rename() maps that to EEXIST, while a
// POSIX rename() replaces. The production export has to cope with the card, so
// this double reports what the card reports. It has to sit at global scope with
// external linkage, or the call in src/FurbleSD.cpp would bind to libc instead.
extern "C" int rename(const char *from, const char *to) {
  if (std::filesystem::exists(to)) {
    errno = EEXIST;
    return -1;
  }
  return ::renameat(AT_FDCWD, from, AT_FDCWD, to);
}

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

// A hung writer: the unmount double blocks until the test releases it, the way
// a slow mount or FAT flush outlasts the production power-off handshake.
std::mutex g_UnmountMutex;
std::condition_variable g_UnmountCondition;
bool g_UnmountHang = false;
bool g_UnmountReleased = false;
std::atomic<uint32_t> g_UnmountEntered {0};

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "SD FAIL: " << message << '\n';
    g_Failures++;
  }
}

void sleepMs(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void holdUnmount(void) {
  if (!g_UnmountHang) {
    return;
  }
  g_UnmountEntered.fetch_add(1);
  std::unique_lock<std::mutex> lock(g_UnmountMutex);
  check(
      g_UnmountCondition.wait_for(lock, std::chrono::seconds(60), [] { return g_UnmountReleased; }),
      "the test never released the hung unmount");
}

void releaseUnmount(void) {
  {
    const std::lock_guard<std::mutex> lock(g_UnmountMutex);
    g_UnmountReleased = true;
  }
  g_UnmountCondition.notify_all();
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

bool waitForUnmountEntered(uint32_t timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (g_UnmountEntered.load() > 0) {
      return true;
    }
    sleepMs(10);
  }
  return g_UnmountEntered.load() > 0;
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
  // Lower only the soft limit. An unprivileged process cannot raise a hard
  // limit again, and a coverage build writes its profile at exit.
  struct rlimit original = {};
  check(getrlimit(RLIMIT_FSIZE, &original) == 0, "the host file-size limit could not be read");
  struct rlimit limit = original;
  limit.rlim_cur = static_cast<rlim_t>(info.st_size + 16);
  check(setrlimit(RLIMIT_FSIZE, &limit) == 0, "the host file-size limit could not be installed");

  const auto second = point(2, 52.53, 13.406);
  check(Furble::SD::getInstance().logPoint(second), "the second GPX point was not queued");
  check(waitForLog("GPX locate closers failed: File too large"),
        "a full card did not reach the production GPX failure");
  std::cout << "sd.full write_failed=1 disabled=0\n";

  check(setrlimit(RLIMIT_FSIZE, &original) == 0, "the host file-size limit could not be restored");
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

// A writer that never answers the shutdown handshake must not look like a
// settled card. The production code waits, extends once, names the writer and
// refuses, so the platform keeps the rails up instead of losing the track tail.
void runShutdownRefused(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED), "SD card did not mount for the refusal case");
  check(Furble::SD::getInstance().logPoint(point(5, 52.52, 13.405)),
        "the refusal-case point was not queued");
  const std::string path = gpxPath(5);
  check(waitForFile(path), "the refusal-case GPX file was not created");

  // The writer reaches the unmount double and stays there, which is the shape of
  // a slow mount or FAT flush at the moment the user asks for power off.
  g_UnmountHang = true;
  const auto start = std::chrono::steady_clock::now();
  const bool acknowledged = Furble::SD::getInstance().powerOff();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  check(!acknowledged, "a hung writer still reported an acknowledged power off");
  check(elapsed.count() >= 8000,
        "the refusal came before the 3000 ms wait and its 5000 ms extension elapsed");
  check(g_UnmountEntered.load() > 0, "the writer never reached the unmount double");
  check(BoundaryHost::logContains("did not acknowledge power off in 3000 ms"),
        "the extended-wait log is missing");
  check(BoundaryHost::logContains("refusing to power off"), "the refusal log is missing");
  check(Furble::SD::getInstance().cardState() == Furble::SD::card_state_t::MOUNTED,
        "the refusal did not leave the card mounted");
  std::cout << "sd.shutdown_refused acknowledged=" << (acknowledged ? 1 : 0)
            << " wait_ms=" << elapsed.count() << " unmount_entered=" << g_UnmountEntered.load()
            << '\n';

  // Release the stuck write. The writer then completes the close, so the track
  // the refusal protected is the same one a later power off would leave.
  releaseUnmount();
  check(waitFor(Furble::SD::card_state_t::UNMOUNTED),
        "the released writer did not finish the unmount");
  check(fileIsComplete(path), "the protected GPX track lost its tail");
  std::cout << "sd.shutdown_refused.released unmounted=1 track_complete=1\n";
}

// A writer that has stopped draining cannot even take the power-off request, so
// there is no acknowledgement to wait for. That is the same unsettled card as a
// missed acknowledgement: its queue holds track points, so the handshake must
// refuse rather than let the rail drop on a writer it never reached.
void runShutdownUndelivered(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED),
        "SD card did not mount for the undelivered case");
  check(Furble::SD::getInstance().logPoint(point(6, 52.52, 13.405)),
        "the undelivered-case point was not queued");
  const std::string path = gpxPath(6);
  check(waitForFile(path), "the undelivered-case GPX file was not created");

  // Turn logging off and hold the unmount the reload asks for, which is how a
  // wedged FAT flush stops the writer draining its queue.
  g_UnmountHang = true;
  Furble::Settings::save<bool>(Furble::Settings::SD_GPX, false);
  check(Furble::SD::getInstance().request(Furble::SD::request_t::RELOAD),
        "the reload request was not queued");
  check(waitForUnmountEntered(), "the writer never reached the held unmount");

  // The writer took the reload and is inside the held unmount, so the queue is
  // empty and these fill it to its length. Points are dropped by logPoint() once
  // it is full, which is the production behaviour.
  uint32_t queued = 0;
  for (uint32_t index = 0; index < 12; index++) {
    if (Furble::SD::getInstance().logPoint(point(7, 52.52 + index * 0.001, 13.405))) {
      queued++;
    }
  }
  check(queued == 8, "the writer queue did not fill to its declared length");

  const auto start = std::chrono::steady_clock::now();
  const bool acknowledged = Furble::SD::getInstance().powerOff();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  check(!acknowledged, "a writer that never took the request still reported a settled power off");
  check(elapsed.count() < 1000, "the undelivered request still blocked for a full wait");
  check(BoundaryHost::logContains("did not take the power off request"),
        "the undelivered-request log is missing");
  std::cout << "sd.shutdown_undelivered acknowledged=" << (acknowledged ? 1 : 0)
            << " queued=" << queued << " wait_ms=" << elapsed.count() << '\n';

  // The refusal must leave a live writer, not one the rail cut hid. Releasing the
  // unmount lets it finish the reload and drain the queue it never reached.
  releaseUnmount();
  check(waitFor(Furble::SD::card_state_t::UNMOUNTED),
        "the released writer did not finish the held unmount");
  check(fileIsComplete(path), "the closed GPX track lost its tail");
  std::cout << "sd.shutdown_undelivered.released drained=1 track_complete=1\n";
}

void runExportFull(void) {
  clearMount();
  initializeSettings(true);
  setupBoard();

  Furble::SD::init();
  check(waitFor(Furble::SD::card_state_t::MOUNTED), "SD card did not mount for the export case");

  const std::string path = std::string(GPX_DIRECTORY) + "/settings.txt";
  const std::string temp = std::string(GPX_DIRECTORY) + "/settings.tmp";

  check(Furble::SD::getInstance().request(Furble::SD::request_t::EXPORT),
        "the first settings export was not queued");
  check(waitForLog("Settings exported to"), "the first settings export did not complete");
  check(!std::filesystem::exists(temp), "a successful export left its temporary file behind");

  std::ifstream first_file(path);
  const std::string first((std::istreambuf_iterator<char>(first_file)),
                          std::istreambuf_iterator<char>());
  first_file.close();
  check(first.rfind("# furble settings v1", 0) == 0, "the first settings export has no header");
  check(first.find("brightness=") != std::string::npos,
        "the first settings export carries no settings");
  std::cout << "sd.export_first path=" << path << " bytes=" << first.size() << '\n';

  // A second export has to replace the first backup. On the card that is a two
  // step replace, because f_rename() refuses the existing name; on a bare POSIX
  // rename it looks identical, which is why the double above emulates FatFs.
  const uint8_t brightness = Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS);
  Furble::Settings::save<uint8_t>(Furble::Settings::BRIGHTNESS, (brightness == 64) ? 96 : 64);
  check(Furble::SD::getInstance().request(Furble::SD::request_t::EXPORT),
        "the replacing settings export was not queued");
  std::string replaced;
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      std::ifstream current_file(path);
      replaced = std::string((std::istreambuf_iterator<char>(current_file)),
                             std::istreambuf_iterator<char>());
      current_file.close();
      if (!replaced.empty() && (replaced != first)) {
        break;
      }
      sleepMs(10);
    }
  }
  check(replaced != first, "a second settings export did not replace the previous backup");
  check(!std::filesystem::exists(temp), "a replacing export left its temporary file behind");
  std::cout << "sd.export_replace replaced=" << ((replaced != first) ? 1 : 0)
            << " bytes=" << replaced.size() << '\n';

  // A card with less free space than the backup needs fails partway through the
  // write. This is the same host file-size seam the full-card GPX case uses.
  signal(SIGXFSZ, SIG_IGN);
  struct rlimit original = {};
  check(getrlimit(RLIMIT_FSIZE, &original) == 0, "the host file-size limit could not be read");
  struct rlimit limit = original;
  limit.rlim_cur = static_cast<rlim_t>(replaced.size() / 2);
  check(setrlimit(RLIMIT_FSIZE, &limit) == 0, "the host file-size limit could not be installed");

  check(Furble::SD::getInstance().request(Furble::SD::request_t::EXPORT),
        "the failing settings export was not queued");
  check(waitForLog("SD settings export failed"),
        "a full card did not reach the production export failure");
  check(setrlimit(RLIMIT_FSIZE, &original) == 0, "the host file-size limit could not be restored");

  std::ifstream kept_file(path);
  const std::string kept((std::istreambuf_iterator<char>(kept_file)),
                         std::istreambuf_iterator<char>());
  kept_file.close();
  check(kept == replaced, "a failed export replaced the previous settings backup");
  check(!std::filesystem::exists(temp), "a failed export left its partial file on the card");
  std::cout << "sd.export_full backup_preserved=" << ((kept == replaced) ? 1 : 0)
            << " bytes=" << kept.size() << '\n';
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
  holdUnmount();
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
  } else if (mode == "shutdown-refused") {
    runShutdownRefused();
  } else if (mode == "shutdown-undelivered") {
    runShutdownUndelivered();
  } else if (mode == "export-full") {
    runExportFull();
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
