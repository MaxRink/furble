#include "FurbleWebUI.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
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
#include "FurblePlatform.h"
#include "FurbleProvision.h"
#include "FurbleSettings.h"
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
    return true;
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
async function cameras(){const x=q('#cameras');try{const list=await api('/api/cameras');x.replaceChildren();for(const c of list){const r=row(c.name,`${c.connected?'connected':'idle'}${c.rssi===null?'':` ${c.rssi} dBm`}`),b=el('button',c.connected?'Disconnect':'Connect');b.className='secondary';b.onclick=()=>camera(c.connected?'disconnect':'connect',c.id);r.append(b);x.append(r)}if(!list.length)x.textContent='No saved cameras'}catch(e){message(e.message,true)}}
async function camera(action,id){try{await api('/api/cameras',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,id})});message('Camera request queued');setTimeout(()=>{refresh();cameras()},500)}catch(e){message(e.message,true)}}
async function shutter(action){try{await api('/api/shutter',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,ms:200})});message('Shutter request accepted');refresh()}catch(e){message(e.message,true)}}
document.querySelectorAll('[data-shutter]').forEach(b=>b.onclick=()=>shutter(b.dataset.shutter));
async function settings(){const x=q('#settings');try{const list=await api('/api/settings');x.replaceChildren();for(const s of list){const r=el('div');r.className='field';r.append(el('label',s.name));if(!s.writable){r.append(el('span',s.set?'set':'unset'),el('span'));}else if(s.type==='bool'){const i=el('input');i.type='checkbox';i.checked=!!s.value;i.onchange=()=>save(s.id,i.checked);r.append(i,el('span'));}else if(s.type==='string'||s.type==='u8'||s.type==='u32'){const i=el('input');i.type=s.type==='string'?'text':'number';i.value=s.secret?'':(s.value??'');i.placeholder=s.secret&&s.set?'set':'';const b=el('button','Set');b.className='secondary';b.onclick=()=>save(s.id,s.type==='string'?i.value:Number(i.value));r.append(i,b);}else{r.append(el('span','Not editable'),el('span'));}x.append(r)}}catch(e){message(e.message,true)}}
async function save(id,value){try{await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,value})});message('Setting saved');settings();refresh()}catch(e){message(e.message,true)}}
refresh();cameras();settings();setInterval(refresh,3000);
</script></body></html>)HTML";

}  // namespace

WebUI &WebUI::getInstance(void) {
  static WebUI instance;
  return instance;
}

void WebUI::init(void) {
  getInstance().reloadSetting();
}

void WebUI::reloadSetting(void) {
  const bool enabled = Settings::load<Settings::WEB_UI>();
  TaskHandle_t task = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Reload = true;
    if ((m_Task == nullptr) && enabled
        && (xTaskCreate(taskEntry, "webui", 8192, this, 2, &m_Task) != pdPASS)) {
      m_Task = nullptr;
      ESP_LOGE(LOG_TAG, "Could not create WebUI supervisor");
    }
    task = m_Task;
  }
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

void WebUI::taskEntry(void *param) {
  static_cast<WebUI *>(param)->task();
}

void WebUI::task(void) {
  while (true) {
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
    bool reload = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      reload = m_Reload;
      m_Reload = false;
    }
    if (reload && (m_Server != nullptr)) {
      stopServer();
    }
    const bool enabled = Settings::load<Settings::WEB_UI>();
    if (!enabled || !networkReady() || !credentialsReady()) {
      if (m_Server != nullptr) {
        stopServer();
      }
    } else if (m_Server == nullptr) {
      startServer();
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TASK_PERIOD_MS));
  }
}

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

bool WebUI::credentialsReady(void) {
  std::string password;
  const bool ready = Settings::loadPassword(password) && !password.empty();
  std::fill(password.begin(), password.end(), '\0');
  if (!ready) {
    return false;
  }
  return loadOrCreateIdentity();
}

bool WebUI::loadOrCreateIdentity(void) {
  if (!m_Certificate.empty() && !m_PrivateKey.empty()) {
    return true;
  }

  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, true)) {
    return false;
  }
  const auto certResult = preferences.getString(TLS_CERT_KEY, m_Certificate);
  const auto keyResult = preferences.getString(TLS_PRIVATE_KEY, m_PrivateKey);
  preferences.end();
  if ((certResult == Preferences::string_result_t::OK)
      && (keyResult == Preferences::string_result_t::OK) && !m_Certificate.empty()
      && !m_PrivateKey.empty()) {
    return true;
  }

  m_Certificate.clear();
  m_PrivateKey.clear();
  if (!createIdentity()) {
    return false;
  }
  if (!preferences.begin(FURBLE_STR, false)) {
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
    result = mbedtls_x509write_crt_set_validity(&certificate, "20240101000000",
                                                "20501231235959");
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

void WebUI::logCertificateFingerprint(void) const {
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
    offset += static_cast<size_t>(std::snprintf(text + offset, sizeof(text) - offset, "%02X%s",
                                                digest[index], index + 1 == digest.size() ? "" : ":"));
  }
  ESP_LOGI(LOG_TAG, "TLS certificate SHA-256 %s", text);
}

bool WebUI::startServer(void) {
  if ((m_Server != nullptr) || m_Certificate.empty() || m_PrivateKey.empty()) {
    return m_Server != nullptr;
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
  config.httpd.stack_size = 12288;
  config.httpd.max_uri_handlers = 8;
  config.httpd.recv_wait_timeout = 2;
  config.httpd.send_wait_timeout = 2;
  config.tls_handshake_timeout_ms = 5000;
  config.servercert = reinterpret_cast<const uint8_t *>(m_Certificate.c_str());
  config.servercert_len = m_Certificate.size() + 1;
  config.prvtkey_pem = reinterpret_cast<const uint8_t *>(m_PrivateKey.c_str());
  config.prvtkey_len = m_PrivateKey.size() + 1;

  const esp_err_t result = httpd_ssl_start(&m_Server, &config);
  if (result != ESP_OK) {
    m_Server = nullptr;
    ESP_LOGW(LOG_TAG, "HTTPS server start failed: %s", esp_err_to_name(result));
    return false;
  }

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
  logCertificateFingerprint();
  ESP_LOGI(LOG_TAG, "Authenticated WebUI listening on HTTPS port 443");
  return true;
}

void WebUI::stopServer(void) {
  {
    const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
    m_Stopping = true;
  }
  httpd_handle_t server = m_Server;
  m_Server = nullptr;
  m_Running.store(false);
  if (server != nullptr) {
    httpd_ssl_stop(server);
  }
  // httpd_ssl_stop joins the server task, so no admitted handler can press
  // after this final release pass.
  releaseAll();
}

bool WebUI::verifyBasic(const std::string &header) {
  if (header.compare(0, 6, "Basic ") != 0) {
    return false;
  }
  std::string encoded = header.substr(6);
  std::vector<unsigned char> decoded(encoded.size() + 1, 0);
  size_t decodedLength = 0;
  if (mbedtls_base64_decode(decoded.data(), decoded.size(), &decodedLength,
                            reinterpret_cast<const unsigned char *>(encoded.data()),
                            encoded.size())
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
  const bool computed = started
                        && companionHmacSha256(
                            reinterpret_cast<const uint8_t *>(supplied.data()), supplied.size(),
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
  const bool valid = readHeader(request, "Authorization", header) && verifyBasic(header);
  std::fill(header.begin(), header.end(), '\0');
  {
    const std::lock_guard<std::mutex> lock(m_AuthMutex);
    if (valid) {
      m_AuthFailures = 0;
      m_AuthBlockedUntilMs = 0;
    } else if (++m_AuthFailures >= CompanionAuth::MAX_FAILURES) {
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
  if (!readHeader(request, "Host", host, 128) || !readHeader(request, "Origin", origin)
      || !WebUIProtocol::sameOrigin(host, origin)) {
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
                     "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
}

esp_err_t WebUI::sendJSON(httpd_req_t *request, const std::string &json) {
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
  while (received < request->content_len) {
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
  return self->requestAllowed(request, false) ? sendJSON(request, self->buildCamerasJSON()) : ESP_OK;
}

esp_err_t WebUI::handleCamerasPost(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, true)) {
    return ESP_OK;
  }
  std::string body;
  if (recvBody(request, body) != ESP_OK) {
    return sendError(request, "400 Bad Request", "invalid request body");
  }
  std::string error;
  if (!self->applyCamera(body, error)) {
    return sendError(request, "409 Conflict", error.c_str());
  }
  return sendJSON(request, "{\"accepted\":true}");
}

esp_err_t WebUI::handleShutter(httpd_req_t *request) {
  auto *self = static_cast<WebUI *>(request->user_ctx);
  if (!self->requestAllowed(request, true)) {
    return ESP_OK;
  }
  std::string body;
  if (recvBody(request, body) != ESP_OK) {
    return sendError(request, "400 Bad Request", "invalid request body");
  }
  std::string error;
  if (!self->applyShutter(body, error)) {
    return sendError(request, "409 Conflict", error.c_str());
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
  const Platform::battery_t battery = Platform::getInstance().readBattery();
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
  cJSON *root = cJSON_CreateArray();
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
    cJSON_AddBoolToObject(item, "writable", setting->type != Settings::COMPANION_PASSWORD);
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
    cJSON_AddItemToArray(root, item);
  }
  const std::string result = jsonString(root);
  cJSON_Delete(root);
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

bool WebUI::queuePress(Control::cmd_t command, uint8_t heldBit) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (m_Stopping) {
    return false;
  }
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    return false;
  }
  bool held = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if ((m_ReleasePending & heldBit) != 0) {
      return false;
    }
    held = (m_Held & heldBit) != 0;
  }
  if (heldBit == HELD_SHUTTER) {
    m_HoldDeadlineUs.store(0);
    if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
      esp_timer_stop(m_HoldTimer);
    }
  }
  if (held) {
    return true;
  }
  const Control::command_delivery_t delivery = control.sendCameraCommand(command);
  if (!delivery.any) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Held |= heldBit;
  m_ReleasePending &= ~heldBit;
  return true;
}

bool WebUI::queueRelease(Control::cmd_t command, uint8_t heldBit) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (heldBit == HELD_SHUTTER) {
    m_HoldDeadlineUs.store(0);
    if ((m_HoldTimer != nullptr) && esp_timer_is_active(m_HoldTimer)) {
      esp_timer_stop(m_HoldTimer);
    }
  }
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_ReleasePending |= heldBit;
  }
  const Control::command_delivery_t delivery =
      Control::getInstance().sendCameraCommand(command);
  if (delivery.all) {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Held &= ~heldBit;
    m_ReleasePending &= ~heldBit;
    return true;
  }
  if ((m_ReleaseTimer != nullptr) && !esp_timer_is_active(m_ReleaseTimer)) {
    return esp_timer_start_once(m_ReleaseTimer, RELEASE_RETRY_MS * 1000ULL) == ESP_OK;
  }
  return m_ReleaseTimer != nullptr;
}

bool WebUI::queueHold(uint32_t durationMs) {
  const std::lock_guard<std::recursive_mutex> commandLock(m_CommandMutex);
  if (!queuePress(Control::CMD_SHUTTER_PRESS, HELD_SHUTTER)) {
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

bool WebUI::applyShutter(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
  bool accepted = false;
  if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "press") == 0)) {
    accepted = queuePress(Control::CMD_SHUTTER_PRESS, HELD_SHUTTER);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "release") == 0)) {
    accepted = queueRelease(Control::CMD_SHUTTER_RELEASE, HELD_SHUTTER);
  } else if (cJSON_IsString(action) && (std::strcmp(action->valuestring, "focus_press") == 0)) {
    accepted = queuePress(Control::CMD_FOCUS_PRESS, HELD_FOCUS);
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
  if (!cJSON_IsNumber(idJSON) || !WebUIProtocol::unsignedInteger(idJSON->valuedouble, UINT8_MAX, id)) {
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
      GPS::getInstance().reloadSetting();
      break;
    case Settings::GPS_MOTION:
      GPS::getInstance().reloadMotionSetting();
      break;
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      Feedback::getInstance().reload();
      break;
    case Settings::TX_POWER:
      Control::getInstance().setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
      break;
    case Settings::COMPANION:
      CompanionGatt::getInstance().reloadSetting();
      break;
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
