#include "FurbleWebUI.h"
#include "FurbleWebUIMemory.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "Device.h"
#include "FurbleCompanion.h"
#include "FurbleCompanionAuth.h"
#include "FurbleCompanionService.h"
#if defined(FURBLE_ETHERNET)
#include "FurbleEthernet.h"
#endif
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleMQTT.h"
#include "FurbleMemoryPressure.h"
#include "FurblePlatform.h"
#include "FurbleProvision.h"
#include "FurbleSettings.h"
#include "FurbleTaskWatchdog.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "FurbleWebUIProtocol.h"
#include "FurbleWiFi.h"
#include "Preferences.h"

namespace Furble {
namespace {

constexpr const char *LOG_TAG = "webui";
constexpr const char *TLS_CERT_KEY = "web_tls_cert";
constexpr const char *TLS_PRIVATE_KEY = "web_tls_key";
constexpr uint64_t AUTH_BLOCK_MS = 30 * 1000;
constexpr size_t HEADER_MAX = 256;

uint64_t nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

bool generateNonce(uint8_t *nonce, size_t length) {
  if (nonce == nullptr) {
    return false;
  }
  esp_fill_random(nonce, length);
  return true;
}

bool readHeader(httpd_req_t *request,
                const char *name,
                std::string &value,
                size_t maximum = HEADER_MAX) {
  const size_t length = httpd_req_get_hdr_value_len(request, name);
  if (length == 0) {
    value.clear();
    return false;
  }
  if (length > maximum) {
    return false;
  }
  std::vector<char> buffer(length + 1, 0);
  if (httpd_req_get_hdr_value_str(request, name, buffer.data(), buffer.size()) != ESP_OK) {
    return false;
  }
  value.assign(buffer.data(), length);
  return true;
}

const char *stateName(Control::state_t state) {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "connect_failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnecting";
  }
  return "unknown";
}

const char *wireTypeName(CompanionService::setting_type_t type) {
  switch (type) {
    case CompanionService::SETTING_BOOL:
      return "bool";
    case CompanionService::SETTING_U8:
      return "u8";
    case CompanionService::SETTING_U32:
      return "u32";
    case CompanionService::SETTING_STRING:
      return "string";
    case CompanionService::SETTING_BLOB:
      return "blob";
  }
  return "blob";
}

bool isSecret(Settings::type_t type) {
  if ((type == Settings::COMPANION_PASSWORD) || (type == Settings::WIFI_PSK)) {
    return true;
  }
#if defined(FURBLE_MQTT) && FURBLE_MQTT
  return (type == Settings::MQTT_PASS) || (type == Settings::MQTT_URI);
#else
  return false;
#endif
}

std::string jsonString(cJSON *object) {
  char *text = cJSON_PrintUnformatted(object);
  if (text == nullptr) {
    return {};
  }
  std::string result(text);
  cJSON_free(text);
  return result;
}

const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>furble</title><style>
:root{color-scheme:dark}body{font:15px system-ui;margin:0;background:#111;color:#eee}header{padding:12px 16px;background:#1c1c1c;font-weight:600}main{padding:16px;max-width:680px;margin:auto}section{background:#1c1c1c;border-radius:8px;padding:12px 16px;margin-bottom:16px}h2{font-size:14px;text-transform:uppercase;color:#aaa;margin:0 0 8px}button{font:inherit;padding:9px 13px;margin:4px;border:0;border-radius:6px;background:#2d6cdf;color:white}.secondary{background:#444}.row{display:flex;justify-content:space-between;gap:12px;padding:3px 0}.key{color:#aaa}.field{display:grid;grid-template-columns:minmax(150px,1fr) minmax(100px,1fr) auto;align-items:center;gap:6px;margin:6px 0}input{min-width:0}small{color:#aaa}.error{color:#ff8a8a}</style></head>
<body><header>furble WebUI</header><main><p id="message" role="status"></p>
<section><h2>Status</h2><div id="status">Loading</div></section>
<section><h2>Shutter</h2><button data-shutter="hold">Hold 200 ms</button><button class="secondary" data-shutter="press">Press</button><button class="secondary" data-shutter="release">Release</button><button class="secondary" data-shutter="focus_press">Focus press</button><button class="secondary" data-shutter="focus_release">Focus release</button><div><small>Press stays held until Release. Hold releases automatically.</small></div></section>
<section><h2>Cameras</h2><div id="cameras"></div></section>
<section><h2>Settings</h2><div id="settings"></div></section></main>
<script>
const q=s=>document.querySelector(s), el=(n,t)=>{const x=document.createElement(n);if(t!==undefined)x.textContent=t;return x};
async function api(url,options){const r=await fetch(url,options);let b={};try{b=await r.json()}catch(e){}if(!r.ok)throw Error(b.error||r.statusText);return b}
function message(text,bad=false){const x=q('#message');x.textContent=text;x.className=bad?'error':''}
function row(k,v){const x=el('div');x.className='row';const a=el('span',k),b=el('span',v);a.className='key';x.append(a,b);return x}
async function refresh(){try{const s=await api('/api/status'),w=s.network,b=s.battery,c=s.cameras,x=q('#status'),n=w.connected?(w.rssi===null?w.ip:`${w.ip} ${w.rssi} dBm`):'down';x.replaceChildren(row('Firmware',s.version),row('ID',s.id),row('State',s.control.state),row('Network',n),row('Cameras',`${c.connected} / ${c.total}`),row('Battery',`${b.level}%${b.charging?' charging':''}`),row('Shutter',s.shutter.held?'held':'idle'))}catch(e){q('#status').textContent='Offline'}}
async function cameras(){const x=q('#cameras');try{const list=await api('/api/cameras');x.replaceChildren();for(const c of list){const r=row(c.name,`${c.connected?'connected':'idle'}${c.rssi===null?'':` ${c.rssi} dBm`}`),b=el('button',c.connected?'Disconnect all':'Connect');b.className='secondary';b.onclick=()=>camera(c.connected?'disconnect':'connect',c.id);r.append(b);x.append(r)}if(!list.length)x.textContent='No saved cameras'}catch(e){message(e.message,true)}}
async function camera(action,id){try{await api('/api/cameras',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,id})});message('Camera request queued');const deadline=Date.now()+15000;const tick=async()=>{await cameras();await refresh();let done=false;try{const list=await api('/api/cameras');const c=list.find(x=>x.id===id);done=!!c&&(action==='connect'?c.connected:!c.connected)}catch(e){}if(!done&&Date.now()<deadline)setTimeout(tick,500)};setTimeout(tick,300)}catch(e){message(e.message,true)}}
async function shutter(action){try{await api('/api/shutter',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,ms:200})});message('Shutter request accepted');refresh()}catch(e){message(e.message,true)}}
document.querySelectorAll('[data-shutter]').forEach(b=>b.onclick=()=>shutter(b.dataset.shutter));
async function settings(){const x=q('#settings');try{const list=await api('/api/settings');x.replaceChildren();for(const s of list){const r=el('div');r.className='field';const inputId='setting-'+s.id;const label=el('label',s.name);label.htmlFor=inputId;r.append(label);if(!s.writable){r.append(el('span',s.type==='blob'?'Not editable':(s.set?'set':'unset')),el('span'));}else if(s.type==='bool'){const i=el('input');i.id=inputId;i.type='checkbox';i.checked=!!s.value;i.onchange=()=>save(s.id,i.checked);r.append(i,el('span'));}else if(s.type==='string'||s.type==='u8'||s.type==='u32'){const i=el('input');i.id=inputId;i.type=s.secret?'password':(s.type==='string'?'text':'number');i.value=s.secret?'':(s.value??'');i.placeholder=s.secret&&s.set?'set':'';const b=el('button','Set');b.className='secondary';b.setAttribute('aria-label','Set '+s.name);b.onclick=()=>{if(s.secret&&!i.value&&!confirm('Clear '+s.name+'?'))return;save(s.id,s.type==='string'?i.value:Number(i.value))};r.append(i,b);}else{r.append(el('span','Not editable'),el('span'));}x.append(r)}}catch(e){message(e.message,true)}}
async function save(id,value){try{await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,value})});message('Setting saved');settings();refresh()}catch(e){message(e.message,true)}}
refresh();cameras();settings();setInterval(refresh,3000);setInterval(cameras,5000);
</script></body></html>)HTML";

}  // namespace

WebUI &WebUI::getInstance(void) {
  static WebUI instance;
  return instance;
}

void WebUI::initEarly(void) {
  auto &self = getInstance();
  const bool enabled = Settings::load<Settings::WEB_UI>();
  std::string password;
  const bool havePassword = Settings::loadPassword(password) && !password.empty();
  std::fill(password.begin(), password.end(), '\0');

  self.m_Enabled.store(enabled);
  self.m_PasswordReady.store(havePassword);
  if (!enabled || !havePassword) {
    return;
  }

  // Run the identity read or generation on the dedicated task. It may write
  // NVS, so its stack stays in internal RAM, but it no longer needs the 16 KiB
  // main stack. The main task blocks until the task finishes, which preserves
  // the old ordering: the identity is ready before WiFi, the display and the
  // companion consume the internal heap.
  {
    const std::lock_guard<std::mutex> lock(self.m_Mutex);
    self.m_EarlyIdentityWaiter = xTaskGetCurrentTaskHandle();
  }
  self.m_IdentityTaskActive.store(true);
  // Not gated: this runs before WiFi, the display and the companion take
  // their share of the internal heap, so the identity has the full boot heap.
  if (xTaskCreate(identityTaskEntry, "webui_tls", WebUIMemory::IDENTITY_STACK_BYTES, &self, 2,
                  nullptr)
      != pdPASS) {
    self.m_IdentityTaskActive.store(false);
    {
      const std::lock_guard<std::mutex> lock(self.m_Mutex);
      self.m_EarlyIdentityWaiter = nullptr;
    }
    self.m_CredentialsReady.store(false);
    ESP_LOGE(LOG_TAG, "Could not create the %u-byte TLS identity task; HTTPS stays closed",
             static_cast<unsigned>(WebUIMemory::IDENTITY_STACK_BYTES));
    return;
  }

  // Share the boot notification bit. The early path only runs before other
  // tasks exist, so no other producer can satisfy this wait.
  xTaskNotifyWait(0, UINT32_MAX, nullptr, portMAX_DELAY);
  if (self.m_CredentialsReady.load()) {
    ESP_LOGI(LOG_TAG, "TLS identity ready before radio and display init");
  } else {
    ESP_LOGW(LOG_TAG, "TLS identity unavailable, HTTPS stays closed");
  }
}

void WebUI::init(void) {
  getInstance().reloadSetting();
}

void WebUI::reloadSetting(void) {
  const bool enabled = Settings::load<Settings::WEB_UI>();
  std::string password;
  const bool havePassword = Settings::loadPassword(password) && !password.empty();
  std::fill(password.begin(), password.end(), '\0');

  bool needIdentity = false;
  {
    const std::lock_guard<std::mutex> lock(m_IdentityMutex);
    m_Enabled.store(enabled);
    m_PasswordReady.store(havePassword);
    if (!havePassword) {
      m_CredentialsReady.store(false);
    } else if (!m_Certificate.empty() && !m_PrivateKey.empty()) {
      m_CredentialsReady.store(true);
    } else if (!enabled) {
      // Do not spend a 12 KiB internal task and an NVS write while the WebUI
      // is off. When it is enabled at runtime, this path runs again and uses
      // the one-shot task; a first-time enable is best done before boot so
      // initEarly() can generate the identity with the full heap.
      m_CredentialsReady.store(false);
    } else {
      // Generate off the caller's task. The console and companion tasks do not
      // have the stack for ECDSA/X.509 generation, and the supervisor must stay
      // small enough for the StickS3 after WiFi is connected.
      m_CredentialsReady.store(false);
      needIdentity = true;
    }
  }

  TaskHandle_t task = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Reload = true;
    if ((m_Task == nullptr) && enabled) {
      const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      const size_t largest =
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      const auto gate = WebUIMemory::supervisorGate(freeBytes, largest);
      if (!gate.open) {
        ESP_LOGE(LOG_TAG, FURBLE_WEBUI_SUPERVISOR_GATE_LOG, static_cast<unsigned>(gate.freeBytes),
                 static_cast<unsigned>(gate.largestBlock), static_cast<unsigned>(gate.freeFloor),
                 static_cast<unsigned>(gate.largestFloor));
      } else if (xTaskCreate(taskEntry, "webui", WebUIMemory::SUPERVISOR_STACK_BYTES, this, 2,
                             &m_Task)
                 != pdPASS) {
        m_Task = nullptr;
        ESP_LOGE(LOG_TAG, "Could not create the %u-byte WebUI supervisor",
                 static_cast<unsigned>(WebUIMemory::SUPERVISOR_STACK_BYTES));
      }
    }
    task = m_Task;
  }

  if (needIdentity && !m_IdentityTaskActive.exchange(true)) {
    startIdentityTask();
  }

  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

void WebUI::taskEntry(void *param) {
  static_cast<WebUI *>(param)->task();
}

void WebUI::identityTaskEntry(void *param) {
  static_cast<WebUI *>(param)->identityTask();
}

void WebUI::startIdentityTask(void) {
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const auto gate = WebUIMemory::identityGate(freeBytes, largest);
  if (!gate.open) {
    m_IdentityTaskActive.store(false);
    ESP_LOGE(LOG_TAG, FURBLE_WEBUI_IDENTITY_GATE_LOG, static_cast<unsigned>(gate.freeBytes),
             static_cast<unsigned>(gate.largestBlock), static_cast<unsigned>(gate.freeFloor),
             static_cast<unsigned>(gate.largestFloor));
    return;
  }
  if (xTaskCreate(identityTaskEntry, "webui_tls", WebUIMemory::IDENTITY_STACK_BYTES, this, 2,
                  nullptr)
      != pdPASS) {
    m_IdentityTaskActive.store(false);
    ESP_LOGE(LOG_TAG, "Could not create the %u-byte TLS identity task; reboot to retry",
             static_cast<unsigned>(WebUIMemory::IDENTITY_STACK_BYTES));
  }
}

void WebUI::identityTask(void) {
  bool ready = false;
  {
    const std::lock_guard<std::mutex> lock(m_IdentityMutex);
    ready = prepareIdentityLocked();
    m_CredentialsReady.store(ready && m_PasswordReady.load());
  }
  m_IdentityTaskActive.store(false);

  TaskHandle_t task = nullptr;
  TaskHandle_t earlyWaiter = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    task = m_Task;
    earlyWaiter = m_EarlyIdentityWaiter;
    m_EarlyIdentityWaiter = nullptr;
  }
  if (earlyWaiter != nullptr) {
    xTaskNotifyGive(earlyWaiter);
  }
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
  vTaskDelete(nullptr);
}

void WebUI::task(void) {
  bool reportedStack = false;
  TaskWatchdog::subscribe();
  while (true) {
    TaskWatchdog::feed();
    serviceTimerEvents();
    bool reload = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      reload = m_Reload;
      m_Reload = false;
    }
    if (reload && (m_Server != nullptr)) {
      stopServer();
    }
    // Memory pressure pauses the server without touching the saved setting;
    // it restarts through startServer()'s own gate once the governor clears.
    const bool enabled = m_Enabled.load() && credentialsReady() && networkReady()
                         && !MemoryPressure::pausesWebUI(MemoryPressure::current());
    if (!enabled) {
      if (m_Server != nullptr) {
        stopServer();
      }
      m_BudgetRetryAfterMs.store(0);
      reportedStack = false;
    } else if (m_Server == nullptr) {
      const uint64_t now = nowMs();
      if (now >= m_BudgetRetryAfterMs.load()) {
        if (startServer()) {
          m_BudgetRetryAfterMs.store(0);
          if (!reportedStack) {
            reportedStack = true;
#if !defined(FURBLE_WEBUI_HOST_TEST)
            // ESP-IDF returns this high-water mark in bytes, unlike vanilla
            // FreeRTOS which returns words.
            ESP_LOGI(LOG_TAG, "WebUI supervisor stack headroom %u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
#endif
          }
        } else {
          // A failed start is retried slowly. An immediate retry would spam
          // the log and, on a memory failure, would keep fragmenting the heap.
          m_BudgetRetryAfterMs.store(now + BUDGET_RETRY_MS);
        }
      }
    }
    // Network, credential and memory readiness are polled. A disabled WebUI
    // has nothing to poll: the server is stopped and its held commands are
    // released. reloadSetting() and the hold and release timers notify this
    // task, so block until one of them does.
    if (m_Enabled.load()) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TASK_PERIOD_MS));
    } else {
      TaskWatchdog::unsubscribe();
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      TaskWatchdog::subscribe();
    }
  }
}

void WebUI::serviceTimerEvents(void) {
  {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    syncCommandSession(Control::getInstance().getSessionGeneration());
  }
  {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    const int64_t focusDeadline = m_FocusLeaseDeadlineUs;
    if ((focusDeadline != 0) && (esp_timer_get_time() >= focusDeadline)) {
      m_FocusLeaseDeadlineUs = 0;
      queueRelease(Control::CMD_FOCUS_RELEASE, HELD_FOCUS);
    }
  }
  const uint8_t timerEvents = m_TimerEvents.exchange(0);
  if ((timerEvents & TIMER_HOLD) != 0) {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    const int64_t deadline = m_HoldDeadlineUs.load();
    const bool timerActive = (m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer);
    // Manual release or a newer command invalidates the old deadline. A
    // replacement timed hold has a future deadline and is not released early.
    if (WebUIProtocol::holdExpired(deadline, esp_timer_get_time(), timerActive)) {
      m_HoldDeadlineUs.store(0);
      queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
    }
  }
  if ((timerEvents & TIMER_RELEASE_RETRY) != 0) {
    retryReleases();
  }
}

#if defined(FURBLE_WEBUI_HOST_TEST)
bool WebUI::hostStartServer(const std::string &certificate, const std::string &privateKey) {
  {
    const std::lock_guard<std::mutex> lock(m_IdentityMutex);
    m_Certificate = certificate;
    m_PrivateKey = privateKey;
  }
  return startServer();
}

void WebUI::hostStopServer(void) {
  stopServer();
}

bool WebUI::hostCredentialsReady(void) {
  std::string password;
  const bool havePassword = Settings::loadPassword(password) && !password.empty();
  std::fill(password.begin(), password.end(), '\0');
  m_PasswordReady.store(havePassword);
  if (!havePassword) {
    m_CredentialsReady.store(false);
    return false;
  }
  const std::lock_guard<std::mutex> lock(m_IdentityMutex);
  const bool ready = prepareIdentityLocked();
  m_CredentialsReady.store(ready);
  return ready;
}

void WebUI::hostResetIdentity(void) {
  const std::lock_guard<std::mutex> lock(m_IdentityMutex);
  m_Certificate.clear();
  m_PrivateKey.clear();
  m_Fingerprint.clear();
}

void WebUI::hostServiceTimerEvents(void) {
  serviceTimerEvents();
}
#endif

bool WebUI::networkReady(void) const {
  const WiFi::status_t status = WiFi::getStatus();
  if (status.connected && !status.ip.empty()) {
    return true;
  }
#if defined(FURBLE_ETHERNET)
  return Ethernet::isConnected() && !Ethernet::getIP().empty();
#else
  return false;
#endif
}

namespace {
bool pemLooksValid(const std::string &certificate, const std::string &key) {
  return (certificate.find("-----BEGIN CERTIFICATE-----") != std::string::npos)
         && (certificate.find("-----END CERTIFICATE-----") != std::string::npos)
         && (key.find("-----BEGIN") != std::string::npos)
         && (key.find("PRIVATE KEY-----") != std::string::npos)
         && (key.find("-----END") != std::string::npos);
}
}  // namespace

bool WebUI::credentialsReady(void) const {
  return m_PasswordReady.load() && m_CredentialsReady.load();
}

bool WebUI::prepareIdentityLocked(void) {
  if (!m_Certificate.empty() && !m_PrivateKey.empty()) {
    if (m_Fingerprint.empty()) {
      logCertificateFingerprint();
    }
    return true;
  }

  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, true)) {
    return false;
  }
  const auto certResult = preferences.getString(TLS_CERT_KEY, m_Certificate);
  const auto keyResult = preferences.getString(TLS_PRIVATE_KEY, m_PrivateKey);
  preferences.end();
  // A transient read failure is not the same as an unset key. Regenerating the
  // identity after a failed read would silently change the certificate the
  // browser was asked to trust.
  if ((certResult == Preferences::string_result_t::ERROR)
      || (keyResult == Preferences::string_result_t::ERROR)) {
    m_Certificate.clear();
    m_PrivateKey.clear();
    m_Fingerprint.clear();
    ESP_LOGE(LOG_TAG, "Failed to read the TLS identity from NVS, keeping HTTPS closed");
    return false;
  }
  if ((certResult == Preferences::string_result_t::OK)
      && (keyResult == Preferences::string_result_t::OK) && !m_Certificate.empty()
      && !m_PrivateKey.empty()) {
    if (pemLooksValid(m_Certificate, m_PrivateKey)) {
      logCertificateFingerprint();
      return true;
    }
    ESP_LOGW(LOG_TAG, "Stored TLS identity is malformed, creating a replacement");
  }

  m_Certificate.clear();
  m_PrivateKey.clear();
  m_Fingerprint.clear();
  if (!createIdentity()) {
    m_Certificate.clear();
    m_PrivateKey.clear();
    return false;
  }
  if (!preferences.begin(FURBLE_STR, false)) {
    // The generated pair was never persisted. Do not let the cache path start
    // HTTPS with an identity that changes on the next boot.
    m_Certificate.clear();
    m_PrivateKey.clear();
    return false;
  }
  const bool saved = preferences.putString(TLS_CERT_KEY, m_Certificate.c_str())
                     && preferences.putString(TLS_PRIVATE_KEY, m_PrivateKey.c_str());
  preferences.end();
  if (!saved) {
    m_Certificate.clear();
    m_PrivateKey.clear();
    return false;
  }
  ESP_LOGI(LOG_TAG, "Created a persistent per-device TLS identity");
  logCertificateFingerprint();
  return true;
}

bool WebUI::createIdentity(void) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context key;
  mbedtls_x509write_cert certificate;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&certificate);

  bool ok = false;
  std::array<unsigned char, 1024> keyPem = {};
  std::array<unsigned char, 2048> certPem = {};
  std::array<unsigned char, 16> serial = {};
  const char personalization[] = "furble webui tls";
  const std::string name = "CN=" + Device::getStringID() + ",O=furble";

  int result = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                     reinterpret_cast<const unsigned char *>(personalization),
                                     sizeof(personalization) - 1);
  if (result == 0) {
    result = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  }
  if (result == 0) {
    result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                 mbedtls_ctr_drbg_random, &random);
  }
  if (result == 0) {
    result = mbedtls_ctr_drbg_random(&random, serial.data(), serial.size());
    serial[0] &= 0x7f;
    serial[0] |= 1;
  }
  if (result == 0) {
    mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&certificate, &key);
    mbedtls_x509write_crt_set_issuer_key(&certificate, &key);
    result = mbedtls_x509write_crt_set_subject_name(&certificate, name.c_str());
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_set_issuer_name(&certificate, name.c_str());
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_set_serial_raw(&certificate, serial.data(), serial.size());
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_set_validity(&certificate, "20240101000000", "20501231235959");
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_set_basic_constraints(&certificate, false, -1);
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_set_key_usage(&certificate, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
  }
  if (result == 0) {
    result = mbedtls_pk_write_key_pem(&key, keyPem.data(), keyPem.size());
  }
  if (result == 0) {
    result = mbedtls_x509write_crt_pem(&certificate, certPem.data(), certPem.size(),
                                       mbedtls_ctr_drbg_random, &random);
  }
  if (result == 0) {
    m_PrivateKey.assign(reinterpret_cast<const char *>(keyPem.data()));
    m_Certificate.assign(reinterpret_cast<const char *>(certPem.data()));
    ok = !m_PrivateKey.empty() && !m_Certificate.empty();
  }

  std::fill(keyPem.begin(), keyPem.end(), 0);
  mbedtls_x509write_crt_free(&certificate);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  if (!ok) {
    ESP_LOGE(LOG_TAG, "Could not create TLS identity: -0x%04x", static_cast<unsigned>(-result));
  }
  return ok;
}

void WebUI::logCertificateFingerprint(void) {
  mbedtls_x509_crt certificate;
  mbedtls_x509_crt_init(&certificate);
  if (mbedtls_x509_crt_parse(&certificate,
                             reinterpret_cast<const unsigned char *>(m_Certificate.c_str()),
                             m_Certificate.size() + 1)
      != 0) {
    mbedtls_x509_crt_free(&certificate);
    return;
  }
  std::array<unsigned char, 32> digest = {};
  if (mbedtls_sha256(certificate.raw.p, certificate.raw.len, digest.data(), 0) != 0) {
    mbedtls_x509_crt_free(&certificate);
    return;
  }
  mbedtls_x509_crt_free(&certificate);
  char text[digest.size() * 3] = {};
  size_t offset = 0;
  for (size_t index = 0; index < digest.size(); index++) {
    offset +=
        static_cast<size_t>(std::snprintf(text + offset, sizeof(text) - offset, "%02X%s",
                                          digest[index], index + 1 == digest.size() ? "" : ":"));
  }
  // Cache the text: the supervisor logs it at start without parsing the
  // certificate again on its small stack.
  m_Fingerprint.assign(text);
  ESP_LOGI(LOG_TAG, "TLS certificate SHA-256 %s", text);
}

bool WebUI::serverMemoryAvailable(void) const {
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const auto gate = WebUIMemory::serverGate(freeBytes, largest);
  if (gate.open) {
    return true;
  }
  ESP_LOGE(LOG_TAG, FURBLE_WEBUI_SERVER_GATE_LOG, static_cast<unsigned>(gate.freeBytes),
           static_cast<unsigned>(gate.largestBlock), static_cast<unsigned>(gate.freeFloor),
           static_cast<unsigned>(gate.largestFloor));
  return false;
}

bool WebUI::startServer(void) {
  if (m_Server != nullptr) {
    return true;
  }
  {
    const std::lock_guard<std::mutex> lock(m_IdentityMutex);
    if (m_Certificate.empty() || m_PrivateKey.empty()) {
      return false;
    }
  }
  if (!serverMemoryAvailable()) {
    return false;
  }
  if (m_HoldTimer == nullptr) {
    const esp_timer_create_args_t timerArgs = {
        .callback = holdTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "webui_hold",
        .skip_unhandled_events = false,
    };
    if (esp_timer_create(&timerArgs, &m_HoldTimer) != ESP_OK) {
      return false;
    }
  }
  if (m_ReleaseTimer == nullptr) {
    const esp_timer_create_args_t timerArgs = {
        .callback = releaseTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "webui_release",
        .skip_unhandled_events = false,
    };
    if (esp_timer_create(&timerArgs, &m_ReleaseTimer) != ESP_OK) {
      return false;
    }
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.stack_size = WebUIMemory::HTTPD_STACK_BYTES;
  config.httpd.max_uri_handlers = HTTPD_MAX_URI_HANDLERS;
  config.httpd.max_open_sockets = HTTPD_MAX_SOCKETS;
  // max_open_sockets counts client sockets. ESP-IDF reserves three additional
  // internal sockets, so two clients plus the reserved set total five. A
  // browser can still leave a stale connection; purge it instead of refusing
  // the next one when both client slots are occupied.
  config.httpd.lru_purge_enable = true;
  // The page is small and its headers are short. Capping the scratch buffer
  // removes 1 KiB of internal heap from every request on the StickS3.
  config.httpd.max_req_hdr_len = HTTPD_MAX_REQ_HDR_LEN;
  config.httpd.max_resp_headers = HTTPD_MAX_RESP_HEADERS;
  config.httpd.recv_wait_timeout = 2;
  config.httpd.send_wait_timeout = 2;
  config.tls_handshake_timeout_ms = 5000;

  esp_err_t result = ESP_FAIL;
  std::string fingerprint;
  {
    // httpd_ssl_start copies the PEM buffers before returning, so the strings
    // only have to stay alive for the call. Holding the mutex also keeps the
    // one-shot identity task from replacing them mid-start.
    const std::lock_guard<std::mutex> lock(m_IdentityMutex);
    config.servercert = reinterpret_cast<const uint8_t *>(m_Certificate.c_str());
    config.servercert_len = m_Certificate.size() + 1;
    config.prvtkey_pem = reinterpret_cast<const uint8_t *>(m_PrivateKey.c_str());
    config.prvtkey_len = m_PrivateKey.size() + 1;
    result = httpd_ssl_start(&m_Server, &config);
    fingerprint = m_Fingerprint;
  }
  if (result != ESP_OK) {
    m_Server = nullptr;
    ESP_LOGW(LOG_TAG, "HTTPS server start failed: %s", esp_err_to_name(result));
    const bool memoryFailure = (result == ESP_ERR_NO_MEM) || (result == ESP_ERR_HTTPD_ALLOC_MEM)
                               || (result == ESP_ERR_HTTPD_TASK);
    if (!memoryFailure && !m_IdentityStartFailed) {
      // Out of internal heap is not a bad identity: keep the certificate and
      // retry later instead of adding NVS writes to an already low heap. Any
      // other start failure may be a structurally plausible but corrupt or
      // mismatched pair, so replace it once on the one-shot task.
      {
        const std::lock_guard<std::mutex> lock(m_IdentityMutex);
        m_IdentityStartFailed = true;
        m_Certificate.clear();
        m_PrivateKey.clear();
        m_Fingerprint.clear();
      }
      if (!m_IdentityTaskActive.exchange(true)) {
        startIdentityTask();
      }
    }
    return false;
  }
  m_IdentityStartFailed = false;
  m_HttpdStackLowWater.store(0);

  const httpd_uri_t routes[] = {
      {"/",             HTTP_GET,  handleRoot,         this},
      {"/api/status",   HTTP_GET,  handleStatus,       this},
      {"/api/cameras",  HTTP_GET,  handleCamerasGet,   this},
      {"/api/cameras",  HTTP_POST, handleCamerasPost,  this},
      {"/api/shutter",  HTTP_POST, handleShutter,      this},
      {"/api/settings", HTTP_GET,  handleSettingsGet,  this},
      {"/api/settings", HTTP_POST, handleSettingsPost, this},
  };
  for (const auto &route : routes) {
    if (httpd_register_uri_handler(m_Server, &route) != ESP_OK) {
      stopServer();
      return false;
    }
  }
  {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    m_Stopping = false;
  }
  m_Running.store(true);
  if (!fingerprint.empty()) {
    ESP_LOGI(LOG_TAG, "TLS certificate SHA-256 %s", fingerprint.c_str());
  }
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  ESP_LOGI(LOG_TAG,
           "Authenticated WebUI listening on HTTPS port 443 (internal free %u, largest %u)",
           static_cast<unsigned>(freeBytes), static_cast<unsigned>(largest));
  return true;
}

void WebUI::stopServer(void) {
  {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    m_Stopping = true;
  }
  // Release before joining the server task. A slowly delivered authenticated
  // request can keep the task busy past its hold deadline, and waiting for the
  // join would leave a timed hold pressed for minutes. m_Stopping is already
  // set, so a handler admitted before it can only finish a press that
  // releaseAll() then releases; every later press is refused under the same
  // command mutex.
  releaseAll();
  httpd_handle_t server = m_Server;
  m_Server = nullptr;
  m_Running.store(false);
  if (server != nullptr) {
    httpd_ssl_stop(server);
  }
  const uint32_t stackLowWater = m_HttpdStackLowWater.exchange(0);
  if (stackLowWater != 0) {
    ESP_LOGI(LOG_TAG, "HTTPS task stack minimum headroom was %u bytes",
             static_cast<unsigned>(stackLowWater));
  }
  // A handler that was already past the m_Stopping check finished before the
  // join. Run one final pass for any release retry it left queued.
  releaseAll();
}

void WebUI::reportHttpdStack(void) {
#if !defined(FURBLE_WEBUI_HOST_TEST)
  // ESP-IDF returns the high-water mark in bytes.
  const uint32_t bytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  uint32_t previous = m_HttpdStackLowWater.load();
  while (((previous == 0) || (bytes < previous))
         && !m_HttpdStackLowWater.compare_exchange_weak(previous, bytes)) {
  }
  if (previous == 0) {
    // One line per server run gives hardware validation a real number without
    // logging on every request.
    ESP_LOGI(LOG_TAG, "HTTPS task stack headroom %u bytes", static_cast<unsigned>(bytes));
  }
#endif
}

bool WebUI::verifyBasic(const std::string &header) {
  if (header.compare(0, 6, "Basic ") != 0) {
    return false;
  }
  std::string encoded = header.substr(6);
  std::vector<unsigned char> decoded(encoded.size() + 1, 0);
  size_t decodedLength = 0;
  if (mbedtls_base64_decode(decoded.data(), decoded.size(), &decodedLength,
                            reinterpret_cast<const unsigned char *>(encoded.data()), encoded.size())
      != 0) {
    std::fill(encoded.begin(), encoded.end(), '\0');
    return false;
  }
  std::fill(encoded.begin(), encoded.end(), '\0');
  const auto colon = std::find(decoded.begin(), decoded.begin() + decodedLength, ':');
  if (colon == decoded.begin() + decodedLength) {
    std::fill(decoded.begin(), decoded.end(), 0);
    return false;
  }
  const std::string user(decoded.begin(), colon);
  std::string supplied(colon + 1, decoded.begin() + decodedLength);
  std::fill(decoded.begin(), decoded.end(), 0);
  if ((user != "furble") || (supplied.size() > CompanionAuth::PASSWORD_MAX)) {
    std::fill(supplied.begin(), supplied.end(), '\0');
    return false;
  }

  std::string stored;
  const bool loaded = Settings::loadPassword(stored);
  if (!loaded || stored.empty()) {
    std::fill(stored.begin(), stored.end(), '\0');
    std::fill(supplied.begin(), supplied.end(), '\0');
    return false;
  }
  CompanionAuth auth {companionHmacSha256, generateNonce};
  std::array<uint8_t, CompanionAuth::NONCE_SIZE> nonce = {};
  std::array<uint8_t, CompanionAuth::HMAC_SIZE> digest = {};
  bool started = auth.setPassword(stored, true);
  if (started) {
    auth.onConnected();
    started = auth.begin(nonce);
  }
  const bool computed =
      started
      && companionHmacSha256(reinterpret_cast<const uint8_t *>(supplied.data()), supplied.size(),
                             nonce.data(), nonce.size(), digest.data(), digest.size());
  const bool valid = computed
                     && (auth.respond(digest.data(), CompanionAuth::RESPONSE_SIZE)
                         == CompanionAuth::response_t::AUTHENTICATED);
  std::fill(digest.begin(), digest.end(), 0);
  std::fill(stored.begin(), stored.end(), '\0');
  std::fill(supplied.begin(), supplied.end(), '\0');
  return valid;
}

bool WebUI::authorize(httpd_req_t *request) {
  std::string header;
  const uint64_t now = nowMs();
  {
    const std::lock_guard<std::mutex> lock(m_AuthMutex);
    if (now < m_AuthBlockedUntilMs) {
      return false;
    }
  }
  const bool hasHeader = readHeader(request, "Authorization", header);
  const bool valid = hasHeader && verifyBasic(header);
  std::fill(header.begin(), header.end(), '\0');
  {
    const std::lock_guard<std::mutex> lock(m_AuthMutex);
    if (valid) {
      m_AuthFailures = 0;
      m_AuthBlockedUntilMs = 0;
    } else if (hasHeader && (++m_AuthFailures >= CompanionAuth::MAX_FAILURES)) {
      m_AuthFailures = 0;
      m_AuthBlockedUntilMs = now + AUTH_BLOCK_MS;
    }
  }
  return valid;
}

bool WebUI::requestAllowed(httpd_req_t *request, bool mutation) {
  if (!authorize(request)) {
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"furble\", charset=\"UTF-8\"");
    sendError(request, "401 Unauthorized", "authentication required");
    return false;
  }
  std::string host;
  std::string origin;
  const bool hasOrigin = httpd_req_get_hdr_value_len(request, "Origin") > 0;
  if (!readHeader(request, "Host", host, 128)
      || (hasOrigin
          && (!readHeader(request, "Origin", origin)
              || !WebUIProtocol::sameOrigin(host, origin)))) {
    sendError(request, "403 Forbidden", "cross-origin request rejected");
    return false;
  }
  if (mutation) {
    std::string contentType;
    if (!readHeader(request, "Content-Type", contentType, 64)
        || !WebUIProtocol::isJSONContentType(contentType)) {
      sendError(request, "415 Unsupported Media Type", "application/json required");
      return false;
    }
  }
  return true;
}

void WebUI::setSecurityHeaders(httpd_req_t *request) {
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Content-Security-Policy",
                     "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                     "frame-ancestors 'none'");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
}

esp_err_t WebUI::sendJSON(httpd_req_t *request, const std::string &json) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (self != nullptr) {
    self->reportHttpdStack();
  }
  setSecurityHeaders(request);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t WebUI::sendError(httpd_req_t *request, const char *status, const char *message) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "error", message);
  const std::string body = jsonString(root);
  cJSON_Delete(root);
  httpd_resp_set_status(request, status);
  return sendJSON(request, body);
}

esp_err_t WebUI::recvBody(httpd_req_t *request, std::string &out) {
  if ((request->content_len == 0) || (request->content_len > MAX_BODY_BYTES)) {
    return ESP_FAIL;
  }
  out.clear();
  out.reserve(request->content_len);
  std::array<char, 256> buffer = {};
  size_t received = 0;
  unsigned timeouts = 0;
  // The socket timeout bounds each read, not the whole body. Without a total
  // deadline a client can keep a handler (and the shutdown supervisor behind
  // it) busy by trickling bytes just inside the per-read timeout.
  constexpr int64_t BODY_READ_DEADLINE_US = 5LL * 1000LL * 1000LL;
  const int64_t deadline = esp_timer_get_time() + BODY_READ_DEADLINE_US;
  while (received < request->content_len) {
    if (esp_timer_get_time() >= deadline) {
      return ESP_FAIL;
    }
    const int chunk = httpd_req_recv(request, buffer.data(),
                                     std::min(buffer.size(), request->content_len - received));
    if ((chunk == HTTPD_SOCK_ERR_TIMEOUT) && (++timeouts <= 2)) {
      continue;
    }
    if (chunk <= 0) {
      return ESP_FAIL;
    }
    out.append(buffer.data(), static_cast<size_t>(chunk));
    received += static_cast<size_t>(chunk);
  }
  return ESP_OK;
}

esp_err_t WebUI::handleRoot(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, false)) {
    return ESP_OK;
  }
  self->reportHttpdStack();
  setSecurityHeaders(request);
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebUI::handleStatus(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  return self->requestAllowed(request, false) ? sendJSON(request, self->buildStatusJSON()) : ESP_OK;
}

esp_err_t WebUI::handleCamerasGet(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  return self->requestAllowed(request, false) ? sendJSON(request, self->buildCamerasJSON())
                                              : ESP_OK;
}

esp_err_t WebUI::handleCamerasPost(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, true)) {
    return ESP_OK;
  }
  std::string body;
  if (request->content_len > MAX_BODY_BYTES) {
    return sendError(request, "413 Payload Too Large", "request body is too large");
  }
  if (recvBody(request, body) != ESP_OK) {
    return sendError(request, "400 Bad Request", "invalid request body");
  }
  std::string error;
  if (!self->applyCamera(body, error)) {
    const bool busy = error.find("queue is busy") != std::string::npos;
    return sendError(request, busy ? "409 Conflict" : "400 Bad Request", error.c_str());
  }
  return sendJSON(request, "{\"accepted\":true}");
}

esp_err_t WebUI::handleShutter(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, true)) {
    return ESP_OK;
  }
  std::string body;
  if (request->content_len > MAX_BODY_BYTES) {
    return sendError(request, "413 Payload Too Large", "request body is too large");
  }
  if (recvBody(request, body) != ESP_OK) {
    return sendError(request, "400 Bad Request", "invalid request body");
  }
  std::string error;
  if (!self->applyShutter(body, error)) {
    const bool busy = error.find("queue is busy") != std::string::npos;
    return sendError(request, busy ? "409 Conflict" : "400 Bad Request", error.c_str());
  }
  return sendJSON(request, "{\"accepted\":true}");
}

esp_err_t WebUI::handleSettingsGet(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  return self->requestAllowed(request, false) ? sendJSON(request, self->buildSettingsJSON())
                                              : ESP_OK;
}

esp_err_t WebUI::handleSettingsPost(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, true)) {
    return ESP_OK;
  }
  std::string body;
  if (request->content_len > MAX_BODY_BYTES) {
    return sendError(request, "413 Payload Too Large", "request body is too large");
  }
  if (recvBody(request, body) != ESP_OK) {
    return sendError(request, "400 Bad Request", "invalid request body");
  }
  std::string error;
  if (!self->applySettings(body, error)) {
    return sendError(request, "400 Bad Request", error.c_str());
  }
  return sendJSON(request, "{\"saved\":true}");
}

std::string WebUI::buildStatusJSON(void) const {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "version", FURBLE_VERSION);
  cJSON_AddStringToObject(root, "id", Device::getStringID().c_str());
  auto &control = Control::getInstance();
  cJSON *controlJSON = cJSON_AddObjectToObject(root, "control");
  cJSON_AddStringToObject(controlJSON, "state", stateName(control.getState()));
  cJSON *cameras = cJSON_AddObjectToObject(root, "cameras");
  cJSON_AddNumberToObject(cameras, "total", control.getTargetCount());
  cJSON_AddNumberToObject(cameras, "connected", control.getConnectedTargetCount());
#if defined(FURBLE_NO_DISPLAY)
  const Platform::battery_t battery = Platform::getInstance().readBattery();
#else
  const Platform::battery_t battery = Platform::getInstance().getBatterySample().battery;
#endif
  cJSON *batteryJSON = cJSON_AddObjectToObject(root, "battery");
  cJSON_AddNumberToObject(batteryJSON, "level", battery.level);
  cJSON_AddNumberToObject(batteryJSON, "voltage", battery.voltage);
  cJSON_AddNumberToObject(batteryJSON, "current", battery.current);
  cJSON_AddBoolToObject(batteryJSON, "charging", battery.charging);
  const WiFi::status_t wifi = WiFi::getStatus();
  bool connected = wifi.connected;
  bool wireless = connected;
  std::string ip = wifi.ip;
#if defined(FURBLE_ETHERNET)
  if (!connected && Ethernet::isConnected()) {
    connected = true;
    ip = Ethernet::getIP();
  }
#endif
  cJSON *networkJSON = cJSON_AddObjectToObject(root, "network");
  cJSON_AddBoolToObject(networkJSON, "connected", connected);
  cJSON_AddStringToObject(networkJSON, "ip", ip.c_str());
  if (wireless) {
    cJSON_AddNumberToObject(networkJSON, "rssi", wifi.rssi);
  } else {
    cJSON_AddNullToObject(networkJSON, "rssi");
  }
  cJSON *shutter = cJSON_AddObjectToObject(root, "shutter");
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    cJSON_AddBoolToObject(shutter, "held", (m_Held & HELD_SHUTTER) != 0);
  }
  const std::string result = jsonString(root);
  cJSON_Delete(root);
  return result;
}

std::string WebUI::buildCamerasJSON(void) const {
  cJSON *root = cJSON_CreateArray();
  for (const auto &snapshot : CompanionService::getCameraSnapshots()) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", snapshot.record.camera_id);
    cJSON_AddStringToObject(item, "name", snapshot.name.c_str());
    cJSON_AddNumberToObject(item, "type", snapshot.record.cam_type);
    cJSON_AddNumberToObject(item, "state", snapshot.record.state);
    const bool connected = (snapshot.record.flags & CompanionService::CAMERA_FLAG_CONNECTED) != 0;
    cJSON_AddBoolToObject(item, "connected", connected);
    if (snapshot.record.rssi == CompanionService::CAMERA_RSSI_UNKNOWN) {
      cJSON_AddNullToObject(item, "rssi");
    } else {
      cJSON_AddNumberToObject(item, "rssi", snapshot.record.rssi);
    }
    cJSON_AddItemToArray(root, item);
  }
  const std::string result = jsonString(root);
  cJSON_Delete(root);
  return result;
}

std::string WebUI::buildSettingsJSON(void) const {
  std::vector<const Settings::setting_t *> settings;
  for (const auto &entry : Settings::all()) {
    if (entry.second.wire_id != 0) {
      settings.push_back(&entry.second);
    }
  }
  std::sort(settings.begin(), settings.end(),
            [](const auto *left, const auto *right) { return left->wire_id < right->wire_id; });
  // Serialize one setting at a time. Building the whole cJSON array and then
  // printing it held the tree and the text copy at the same time: about 7 KiB
  // of internal heap for the full catalog, which is the largest allocation the
  // HTTPS task makes on the StickS3. The wire format is unchanged.
  std::string result = "[";
  result.reserve(settings.size() * 128);
  bool first = true;
  for (const auto *setting : settings) {
    std::vector<uint8_t> value;
    const auto type = CompanionService::settingType(setting->type);
    const bool secret = isSecret(setting->type);
    bool readable = false;
    if (secret) {
      std::string secretValue;
      if (setting->type == Settings::COMPANION_PASSWORD) {
        readable = Settings::loadPassword(secretValue);
      } else {
        secretValue = Settings::load<std::string>(setting->type);
        readable = true;
      }
      value.assign(secretValue.empty() ? 0 : 1, 1);
      std::fill(secretValue.begin(), secretValue.end(), '\0');
    } else {
      readable = CompanionService::settingValue(setting->type, value);
    }
    if (!readable) {
      continue;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", setting->wire_id);
    cJSON_AddStringToObject(item, "name", setting->name);
    cJSON_AddStringToObject(item, "type", wireTypeName(type));
    cJSON_AddBoolToObject(item, "secret", secret);
    cJSON_AddBoolToObject(item, "writable",
                          (type != CompanionService::SETTING_BLOB)
                              && (setting->type != Settings::COMPANION_PASSWORD));
    if (secret) {
      cJSON_AddBoolToObject(item, "set", !value.empty());
      cJSON_AddNullToObject(item, "value");
    } else if (type == CompanionService::SETTING_BOOL) {
      cJSON_AddBoolToObject(item, "value", !value.empty() && (value[0] != 0));
    } else if (type == CompanionService::SETTING_U8) {
      cJSON_AddNumberToObject(item, "value", value.empty() ? 0 : value[0]);
    } else if (type == CompanionService::SETTING_U32) {
      uint32_t number = 0;
      if (value.size() == sizeof(number)) {
        std::memcpy(&number, value.data(), sizeof(number));
      }
      cJSON_AddNumberToObject(item, "value", number);
    } else if (type == CompanionService::SETTING_STRING) {
      cJSON_AddStringToObject(item, "value", std::string(value.begin(), value.end()).c_str());
    } else {
      cJSON_AddNullToObject(item, "value");
    }
    char *text = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (text == nullptr) {
      return {};
    }
    if (!first) {
      result += ',';
    }
    first = false;
    result += text;
    cJSON_free(text);
  }
  result += ']';
  return result;
}

bool WebUI::applyCamera(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
  bool queued = false;
  if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "disconnect") == 0)) {
    queued = UI::sendRequest(UI::Request::DISCONNECT, 0);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "connect") == 0)) {
    uint32_t cameraId = 0;
    if (!cJSON_IsNumber(id)
        || !WebUIProtocol::unsignedInteger(id->valuedouble, UINT8_MAX, cameraId)) {
      error = "camera id must be an integer from 0 through 255";
    } else {
      queued = UI::sendRequest(UI::Request::CONNECT_SAVED, static_cast<int32_t>(cameraId));
    }
  } else {
    error = "unknown camera action";
  }
  cJSON_Delete(root);
  if (!queued && error.empty()) {
    error = "camera request queue is busy";
  }
  return queued;
}

std::vector<uint64_t> &WebUI::heldTargets(uint8_t heldBit) {
  return (heldBit == HELD_SHUTTER) ? m_HeldShutterTargets : m_HeldFocusTargets;
}

void WebUI::startManualLease(uint8_t heldBit) {
  if (heldBit == HELD_FOCUS) {
    m_FocusLeaseDeadlineUs = esp_timer_get_time() + MANUAL_PRESS_LEASE_MS * 1000LL;
    return;
  }
  if ((m_HoldTimer != nullptr) && !esp_timer_is_active(m_HoldTimer)) {
    m_HoldDeadlineUs.store(esp_timer_get_time() + MANUAL_PRESS_LEASE_MS * 1000LL);
    (void)esp_timer_start_once(m_HoldTimer, MANUAL_PRESS_LEASE_MS * 1000ULL);
  }
}

bool WebUI::queuePress(Control::cmd_t command, uint8_t heldBit, bool startLease) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (m_Stopping) {
    return false;
  }
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    return false;
  }
  syncCommandSession(control.getSessionGeneration());
  bool held = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if ((m_ReleasePending & heldBit) != 0) {
      return false;
    }
    held = (m_Held & heldBit) != 0;
    if (held && (heldBit == HELD_SHUTTER)) {
      const int64_t deadline = m_HoldDeadlineUs.load();
      const bool timerActive = (m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer);
      if ((deadline > 0) && (esp_timer_get_time() >= deadline) && !timerActive) {
        // The hold has expired but the supervisor has not released it yet.
        // Preserve the ownership and reject the press until that release runs.
        return false;
      }
    }
  }
  if (held) {
    // A duplicate press of an already-held control is idempotent. Clearing the
    // deadline here would turn a timed hold into an indefinite one.
    return true;
  }
  if (heldBit == HELD_SHUTTER) {
    m_HoldDeadlineUs.store(0);
    if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
      esp_timer_stop(m_HoldTimer);
    }
  }
  const Control::command_delivery_t delivery = control.sendCameraCommand(command);
  if (!delivery.any) {
    return false;
  }
  syncCommandSession(delivery.session);
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_CommandSession = delivery.session;
  m_Held |= heldBit;
  m_ReleasePending &= ~heldBit;
  heldTargets(heldBit) = delivery.accepted;
  if (startLease) {
    startManualLease(heldBit);
  }
  return true;
}

bool WebUI::queueRelease(Control::cmd_t command, uint8_t heldBit) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (heldBit == HELD_FOCUS) {
    m_FocusLeaseDeadlineUs = 0;
  }
  if (heldBit == HELD_SHUTTER) {
    m_HoldDeadlineUs.store(0);
    if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
      esp_timer_stop(m_HoldTimer);
    }
  }
  const uint32_t currentSession = Control::getInstance().getSessionGeneration();
  if (syncCommandSession(currentSession)) {
    return true;
  }
  uint32_t commandSession = 0;
  std::vector<uint64_t> targets;
  bool owned = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    owned = ((m_Held | m_ReleasePending) & heldBit) != 0;
    if (owned) {
      m_ReleasePending |= heldBit;
      commandSession = m_CommandSession;
      targets = heldTargets(heldBit);
      if (targets.empty()) {
        m_Held &= ~heldBit;
        m_ReleasePending &= ~heldBit;
        return true;
      }
    }
  }
  if (!owned) {
    // Not our hold. Still send a release so a physical trigger or another
    // owner's press can be freed from the WebUI.
    for (const auto &camera : Control::getInstance().getTargetCameras()) {
      if (camera && camera->isConnected()) {
        targets.push_back(static_cast<uint64_t>(camera->getAddress()));
      }
    }
    if (targets.empty()) {
      return true;
    }
    commandSession = Control::getInstance().getSessionGeneration();
  }
  // Address only the targets that still owe a release. A target that never
  // accepted the press was never holding the shutter, and a target that
  // already accepted the release must not see it again.
  const Control::command_delivery_t delivery =
      Control::getInstance().sendCameraCommand(command, commandSession, &targets);
  if (delivery.session != commandSession) {
    syncCommandSession(delivery.session);
    return true;
  }
  bool complete = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    auto &held = heldTargets(heldBit);
    for (uint64_t address : delivery.accepted) {
      held.erase(std::remove(held.begin(), held.end(), address), held.end());
    }
    if (held.empty() || !owned) {
      if (owned) {
        m_Held &= ~heldBit;
        m_ReleasePending &= ~heldBit;
      }
      complete = true;
    }
  }
  if (complete) {
    return true;
  }
  if ((m_ReleaseTimer != nullptr) && !esp_timer_is_active(m_ReleaseTimer)) {
    return esp_timer_start_once(m_ReleaseTimer, RELEASE_RETRY_MS * 1000ULL) == ESP_OK;
  }
  return m_ReleaseTimer != nullptr;
}

bool WebUI::queueHold(uint32_t durationMs) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (!queuePress(Control::CMD_SHUTTER_PRESS, HELD_SHUTTER, false)) {
    return false;
  }
  if (m_HoldTimer == nullptr) {
    queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
    return false;
  }
  if (esp_timer_is_active(m_HoldTimer)) {
    esp_timer_stop(m_HoldTimer);
  }
  const uint32_t boundedDuration = std::max<uint32_t>(durationMs, 1);
  m_HoldDeadlineUs.store(esp_timer_get_time() + (static_cast<int64_t>(boundedDuration) * 1000));
  if (esp_timer_start_once(m_HoldTimer, boundedDuration * 1000ULL) != ESP_OK) {
    m_HoldDeadlineUs.store(0);
    queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
    return false;
  }
  return true;
}

bool WebUI::syncCommandSession(uint32_t session) {
  bool stale = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    stale = ((m_Held | m_ReleasePending) != 0) && (m_CommandSession != session);
    if (stale) {
      m_Held = 0;
      m_ReleasePending = 0;
      m_CommandSession = session;
      m_FocusLeaseDeadlineUs = 0;
      m_HeldShutterTargets.clear();
      m_HeldFocusTargets.clear();
    }
  }
  if (!stale) {
    return false;
  }
  m_HoldDeadlineUs.store(0);
  if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
    esp_timer_stop(m_HoldTimer);
  }
  if ((m_ReleaseTimer != nullptr) && esp_timer_is_active(m_ReleaseTimer)) {
    esp_timer_stop(m_ReleaseTimer);
  }
  return true;
}

bool WebUI::applyShutter(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
  bool accepted = false;
  if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "press") == 0)) {
    accepted = queuePress(Control::CMD_SHUTTER_PRESS, HELD_SHUTTER, true);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "release") == 0)) {
    accepted = queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "focus_press") == 0)) {
    accepted = queuePress(Control::CMD_FOCUS_PRESS, HELD_FOCUS, true);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "focus_release") == 0)) {
    accepted = queueRelease(Control::CMD_FOCUS_RELEASE, HELD_FOCUS);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "hold") == 0)) {
    const cJSON *milliseconds = cJSON_GetObjectItemCaseSensitive(root, "ms");
    uint32_t duration = 200;
    if ((milliseconds != nullptr)
        && (!cJSON_IsNumber(milliseconds)
            || !WebUIProtocol::unsignedInteger(milliseconds->valuedouble, MAX_HOLD_MS, duration))) {
      error = "hold must be an integer from 0 through 60000 ms";
    } else {
      accepted = queueHold(duration);
    }
  } else {
    error = "unknown shutter action";
  }
  cJSON_Delete(root);
  if (!accepted && error.empty()) {
    error = "camera is not active or the command queue is busy";
  }
  return accepted;
}

void WebUI::releaseAll(void) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  m_FocusLeaseDeadlineUs = 0;
  m_HoldDeadlineUs.store(0);
  if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
    esp_timer_stop(m_HoldTimer);
  }
  uint8_t held = 0;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    held = m_Held;
  }
  if ((held & HELD_SHUTTER) != 0) {
    queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
  }
  if ((held & HELD_FOCUS) != 0) {
    queueRelease(Control::CMD_FOCUS_RELEASE, HELD_FOCUS);
  }
}

void WebUI::retryReleases(void) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  uint8_t pending = 0;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    pending = m_ReleasePending;
  }
  if ((pending & HELD_SHUTTER) != 0) {
    queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
  }
  if ((pending & HELD_FOCUS) != 0) {
    queueRelease(Control::CMD_FOCUS_RELEASE, HELD_FOCUS);
  }
  if (Control::getInstance().getTargetCount() == 0) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Held = 0;
    m_ReleasePending = 0;
  }
}

void WebUI::holdTimerCallback(void *arg) {
  auto *self = static_cast<WebUI *>(arg);
  self->m_TimerEvents.fetch_or(TIMER_HOLD);
  xTaskNotifyGive(self->m_Task);
}

void WebUI::releaseTimerCallback(void *arg) {
  auto *self = static_cast<WebUI *>(arg);
  self->m_TimerEvents.fetch_or(TIMER_RELEASE_RETRY);
  xTaskNotifyGive(self->m_Task);
}

bool WebUI::applySettings(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }
  const cJSON *idJSON = cJSON_GetObjectItemCaseSensitive(root, "id");
  uint32_t id = 0;
  if (!cJSON_IsNumber(idJSON)
      || !WebUIProtocol::unsignedInteger(idJSON->valuedouble, UINT8_MAX, id)) {
    cJSON_Delete(root);
    error = "setting id must be an integer from 0 through 255";
    return false;
  }
  const Settings::setting_t *setting = Settings::getByWireId(static_cast<uint8_t>(id));
  if ((setting == nullptr) || (setting->type == Settings::COMPANION_PASSWORD)) {
    cJSON_Delete(root);
    error = "setting is not writable here";
    return false;
  }
  const cJSON *valueJSON = cJSON_GetObjectItemCaseSensitive(root, "value");
  const auto wireType = CompanionService::settingType(setting->type);
  ProvisionTLV::SettingValue field = {setting->wire_id, ProvisionTLV::ValueType::BLOB, {}};
  switch (wireType) {
    case CompanionService::SETTING_BOOL:
      if (!cJSON_IsBool(valueJSON)) {
        error = "value must be a boolean";
      } else {
        field.type = ProvisionTLV::ValueType::BOOL;
        field.value = {static_cast<uint8_t>(cJSON_IsTrue(valueJSON) ? 1 : 0)};
      }
      break;
    case CompanionService::SETTING_U8:
    {
      uint32_t number = 0;
      if (!cJSON_IsNumber(valueJSON)
          || !WebUIProtocol::unsignedInteger(valueJSON->valuedouble, UINT8_MAX, number)) {
        error = "value must be an integer from 0 through 255";
      } else {
        field.type = ProvisionTLV::ValueType::U8;
        field.value = {static_cast<uint8_t>(number)};
      }
      break;
    }
    case CompanionService::SETTING_U32:
    {
      uint32_t number = 0;
      if (!cJSON_IsNumber(valueJSON)
          || !WebUIProtocol::unsignedInteger(valueJSON->valuedouble, UINT32_MAX, number)) {
        error = "value must be an unsigned integer";
      } else {
        field.type = ProvisionTLV::ValueType::U32;
        field.value.resize(sizeof(number));
        std::memcpy(field.value.data(), &number, sizeof(number));
      }
      break;
    }
    case CompanionService::SETTING_STRING:
      if (!cJSON_IsString(valueJSON) || (valueJSON->valuestring == nullptr)) {
        error = "value must be a string";
      } else {
        const size_t length = std::strlen(valueJSON->valuestring);
        if (length > UINT8_MAX) {
          error = "string is too long";
        } else {
          field.type = ProvisionTLV::ValueType::STRING;
          field.value.assign(valueJSON->valuestring, valueJSON->valuestring + length);
        }
      }
      break;
    case CompanionService::SETTING_BLOB:
      error = "blob settings are not writable here";
      break;
  }
  cJSON_Delete(root);
  if (!error.empty()) {
    return false;
  }
  ProvisionTLV::ProvisionBundle bundle;
  bundle.settings.push_back(std::move(field));
  Provision::ApplyReport report;
  Provision::ApplyOptions options;
  options.onSettingApplied = settingApplied;
  if (!Provision::apply(bundle, report, options)) {
    error = report.message.empty() ? Provision::applyErrorString(report.error) : report.message;
    return false;
  }
  return true;
}

void WebUI::settingApplied(uint8_t wireId) {
  const auto *setting = Settings::getByWireId(wireId);
  if (setting == nullptr) {
    return;
  }
  switch (setting->type) {
    case Settings::GPS:
    case Settings::GPS_BAUD:
    case Settings::GPS_RATE:
    case Settings::GPS_NMEA:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::GPS_HOLD:
    case Settings::GPS_EXTRAP:
    case Settings::GPS_PLATFORM:
      // The UI task owns the GPS page caches, including the Extrapolate
      // control, so the reload has to run there rather than on the HTTPS task.
      UI::sendRequest(UI::Request::GPS_RELOAD, 0);
      break;
    case Settings::GPS_MOTION:
      GPS::getInstance().reloadMotionSetting();
      break;
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      // Feedback caches are read on the UI task; reload through its queue.
      UI::sendRequest(UI::Request::FEEDBACK_RELOAD, 0);
      break;
    case Settings::TX_POWER:
      Control::getInstance().setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
      break;
    case Settings::COMPANION:
      CompanionGatt::getInstance().reloadSetting();
      break;
    case Settings::SD_GPX:
    case Settings::GPX_PERIOD:
#if defined(FURBLE_NO_DISPLAY)
      GPS::getInstance().reloadLogSettings();
#else
      UI::sendRequest(UI::Request::SD_RELOAD, 0);
#endif
      break;
    case Settings::IR:
      UI::sendRequest(UI::Request::IR_RELOAD, 0);
      break;
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
    case Settings::AUTO_OFF_CHARGING:
#if !defined(FURBLE_NO_DISPLAY)
      UI::sendRequest(UI::Request::POWER_RELOAD, 0);
#endif
      break;
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
      UI::sendRequest(UI::Request::DISPLAY_MODE, Settings::load<uint8_t>(Settings::DISPLAY_MODE));
      break;
#endif
    case Settings::IMU:
    case Settings::IMU_WAKE:
    case Settings::IMU_TRIG:
      UI::notifyGestureSettingsChanged();
      break;
    case Settings::WIFI:
      WiFi::setEnabled(Settings::load<bool>(Settings::WIFI));
      break;
    case Settings::WIFI_SSID:
      WiFi::clearRememberedAccessPoint();
      break;
    case Settings::NTP:
      WiFi::setNtpEnabled(Settings::load<bool>(Settings::NTP));
      break;
    case Settings::NTP_SERVER:
      WiFi::reloadNtp();
      break;
#if defined(FURBLE_MQTT) && FURBLE_MQTT
    case Settings::MQTT:
    case Settings::MQTT_URI:
    case Settings::MQTT_USER:
    case Settings::MQTT_PASS:
    case Settings::MQTT_BASE:
    case Settings::MQTT_HA:
      MQTT::getInstance().reloadSetting();
      break;
#endif
    case Settings::WEB_UI:
      getInstance().reloadSetting();
      break;
    default:
      break;
  }
}

}  // namespace Furble
