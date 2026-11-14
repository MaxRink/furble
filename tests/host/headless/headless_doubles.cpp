#include "headless_state.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "CameraList.h"
#include "Device.h"
#include "FurbleBootScreen.h"
#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleCrashLog.h"
#include "FurbleEthernet.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurbleMQTT.h"
#include "FurbleMemoryPressure.h"
#include "FurblePlatform.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleTimezoneGPS.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "FurbleWebUI.h"
#include "FurbleWiFi.h"
#include "M5Unified.h"
#include "Scan.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"

const char *LOG_TAG = FURBLE_STR;

namespace {

void record(const char *stage) {
  const std::lock_guard<std::mutex> lock(HeadlessState::bootMutex);
  HeadlessState::boot.emplace_back(stage);
}

}  // namespace

namespace Furble {

Control &Control::getInstance() {
  static Control instance;
  return instance;
}

void Control::addActive(std::shared_ptr<Camera>) {}

void Control::connectAll(bool) {
  HeadlessState::controlConnects++;
}

void Control::disconnect() {
  HeadlessState::controlDisconnects++;
}

Control::state_t Control::getState() const {
  return STATE_IDLE;
}

size_t Control::getTargetCount() const {
  return 0;
}

std::vector<std::shared_ptr<Camera>> Control::getTargetCameras() const {
  return {};
}

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

void Platform::init() {
  record("platform.init");
}

uint32_t Platform::tick() {
  return static_cast<uint32_t>(HeadlessState::nowUs / 1000);
}

void Platform::update() {
  HeadlessState::platformUpdates++;
}

void Platform::setCPUMaxFreq(uint8_t) {
  HeadlessState::cpuFreqSets++;
}

bool Platform::watchdogEnable(bool) {
  HeadlessState::watchdogEnables++;
  return true;
}

void Platform::restart() {}

void Platform::wakeM5PM1() {}

Platform::battery_caps_t Platform::getBatteryCaps() const {
  return {};
}

void BootScreen::begin(uint8_t) {
  record("boot.begin");
}

void BootScreen::step(const char *label) {
  HeadlessState::bootSteps++;
  record(label == nullptr ? "boot.step" : label);
}

void BootScreen::finish() {
  HeadlessState::bootFinish++;
  record("boot.finish");
}

bool BootScreen::wasShown() {
  return true;
}

uint8_t BootScreen::stepsShown() {
  return static_cast<uint8_t>(HeadlessState::bootSteps);
}

namespace CrashLog {

void init() {
  record("crashlog.init");
}

void update() {
  HeadlessState::crashUpdates++;
}

bool recovered() {
  return false;
}

}  // namespace CrashLog

namespace MemoryPressure {

void tick(void) {
  HeadlessState::pressureTicks++;
}

// The headless service loop has no heap to shed from; the connect admission
// policy is covered by memory_pressure_test.
void admit(size_t) {}

level_t current(void) {
  return NORMAL;
}

const char *levelName(level_t) {
  return "normal";
}

}  // namespace MemoryPressure

const std::string &Device::getStringID() {
  static const std::string id = "furble-headless";
  return id;
}

void Device::init(esp_power_level_t) {
  record("device.init");
}

namespace {

std::shared_ptr<Camera> g_NoCamera;

}

void CameraList::load() {}

size_t CameraList::getSaveCount() {
  return 0;
}

size_t CameraList::size() {
  return 0;
}

std::shared_ptr<Camera> CameraList::get(size_t) {
  return {};
}

bool CameraList::isSaved(const Camera *) {
  return false;
}

int32_t CameraList::savedIndex(const Camera *) {
  return -1;
}

std::vector<std::shared_ptr<Camera>> CameraList::savedSnapshot() {
  return {};
}

bool CameraList::remove(Camera *) {
  return false;
}

uint8_t CameraList::getCameraId(const Camera *) {
  return 0;
}

void CameraList::clear() {}

void CameraList::addFauxNY() {}

Scan &Scan::getInstance() {
  static Scan instance;
  return instance;
}

void Scan::setMode(Mode) {}

void Scan::setTimeout(uint32_t) {}

bool Scan::start(std::function<void(void *)>, void *, std::function<void(void *)>) {
  m_Active = true;
  return true;
}

void Scan::stop() {
  m_Active = false;
}

bool Scan::isActive() const {
  return m_Active;
}

void Scan::clear() {}

void Scan::processPendingCallbacks() {
  HeadlessState::scanProcessed++;
}

IR &IR::getInstance() {
  static IR instance;
  return instance;
}

IR::protocol_t IR::clampProtocol(uint8_t value) {
  return value > static_cast<uint8_t>(protocol_t::CANON_DELAYED) ? protocol_t::NIKON
                                                                 : static_cast<protocol_t>(value);
}

void IR::init() {
  HeadlessState::irInit++;
  record("ir.init");
}

bool IR::isSupported() const {
  return false;
}

void IR::fire() {}

void IR::fire(protocol_t) {}

Feedback &Feedback::getInstance() {
  static Feedback instance;
  return instance;
}

void Feedback::init() {
  record("feedback.init");
}

bool Feedback::outputIncludesSound(output_t output) {
  return (output == OUTPUT_SOUND) || (output == OUTPUT_SOUND_LIGHT);
}

bool Feedback::outputIncludesLight(output_t output) {
  return (output == OUTPUT_LIGHT) || (output == OUTPUT_SOUND_LIGHT);
}

bool Feedback::outputIncludesVibration(output_t output) {
  return output == OUTPUT_VIBRATE;
}

bool Feedback::supports(output_t) const {
  return false;
}

size_t Feedback::getOutputOptions(output_t *) const {
  return 0;
}

Feedback::output_t Feedback::outputForOption(uint8_t) const {
  return OUTPUT_OFF;
}

void Feedback::reload() {
  HeadlessState::feedbackReloads++;
}

void Feedback::setVolume(uint8_t) {}

void Feedback::setDisplayOff(bool) {}

void Feedback::signal(event_t, bool) {
  HeadlessState::feedbackSignals++;
}

void Feedback::updateBattery(uint8_t, bool) {}

void SD::init() {
  HeadlessState::sdInit++;
  record("sd.init");
}

bool Ethernet::init() {
  HeadlessState::ethernetInit++;
  record("ethernet.init");
  return true;
}

void TimeKeeper::init() {
  record("timekeeper.init");
}

bool TimezoneGPS::service() {
  return true;
}

GPS &GPS::getInstance() {
  static GPS instance;
  return instance;
}

void GPS::init() {
  HeadlessState::gpsInit++;
  record("gps.init");
}

void GPS::update() {
  HeadlessState::gpsUpdates++;
}

void GPS::reloadSetting() {
  HeadlessState::gpsReloads++;
}

void WiFi::init() {
  HeadlessState::wifiInit++;
  record("wifi.init");
}

CompanionGatt &CompanionGatt::getInstance() {
  static CompanionGatt instance;
  return instance;
}

void CompanionGatt::init() {
  HeadlessState::bleInit++;
  record("companion.init");
}

WebUI &WebUI::getInstance() {
  static WebUI instance;
  return instance;
}

void WebUI::initEarly() {
  HeadlessState::webuiEarly++;
  record("webui.early");
}

void WebUI::init() {
  HeadlessState::webuiInit++;
  record("webui.init");
}

void MQTT::init() {
  HeadlessState::mqttInit++;
  record("mqtt.init");
}

void Console::init() {
  HeadlessState::consoleInit++;
  record("console.init");
  HeadlessState::consoleQueued = Furble::UI::sendRequest(Furble::UI::Request::FEEDBACK_TEST, 0)
                                 && Furble::UI::sendRequest(Furble::UI::Request::FEEDBACK_RELOAD, 0)
                                 && Furble::UI::sendRequest(Furble::UI::Request::GPS_RELOAD, 0);
}

}  // namespace Furble

extern "C" {

void control_task(void *) {
  HeadlessState::controlTasks++;
  record("control.task");
}

esp_err_t esp_netif_init(void) {
  record("netif.init");
  return ESP_OK;
}

esp_err_t esp_event_loop_create_default(void) {
  record("eventloop.init");
  return ESP_OK;
}

int64_t esp_timer_get_time(void) {
  return HeadlessState::nowUs;
}

uint32_t furbleBootTimelineHostResetMs(void) {
  return 0;
}

int64_t furbleBootTimelineHostNowUs(void) {
  return HeadlessState::nowUs;
}

}  // extern "C"
