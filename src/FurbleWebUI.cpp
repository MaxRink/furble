#include "FurbleWebUI.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>

// M5PM1.h (pulled in transitively via FurblePlatform.h) leaks the Arduino GPIO
// macros LOW and HIGH. furble is ESP-IDF and does not use them, so drop them
// before the NimBLE headers below, matching FurbleMQTT.cpp.
#undef LOW
#undef HIGH

#include <cJSON.h>

#include "CameraList.h"
#include "Device.h"

#include "FurbleCompanionService.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleMQTT.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {

constexpr const char *WEB_LOG_TAG = "webui";

uint64_t nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

/** The single embedded page. Vanilla HTML and JS, no framework, no CDN. */
const char INDEX_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>furble</title>
<style>
:root { color-scheme: dark; }
body { font-family: system-ui, sans-serif; margin: 0; background: #111; color: #eee; }
header { padding: 12px 16px; background: #1c1c1c; font-weight: 600; }
main { padding: 16px; max-width: 640px; margin: 0 auto; }
section { background: #1c1c1c; border-radius: 8px; padding: 12px 16px; margin-bottom: 16px; }
h2 { font-size: 14px; text-transform: uppercase; letter-spacing: .05em; color: #999; margin: 0 0 8px; }
button { font-size: 15px; padding: 10px 14px; margin: 4px 6px 4px 0; border: 0; border-radius: 6px;
         background: #2d6cdf; color: #fff; cursor: pointer; }
button.secondary { background: #444; }
button:active { opacity: .7; }
.row { display: flex; justify-content: space-between; padding: 3px 0; }
.k { color: #999; }
table { width: 100%; border-collapse: collapse; font-size: 14px; }
td { padding: 4px 0; border-bottom: 1px solid #2a2a2a; }
input[type=number] { width: 90px; }
.field { margin: 6px 0; }
.field label { display: inline-block; min-width: 180px; color: #ccc; }
small { color: #888; }
</style>
</head>
<body>
<header>furble WebUI</header>
<main>
<section>
<h2>Status</h2>
<div id="status">loading...</div>
</section>
<section>
<h2>Shutter</h2>
<button onclick="shutter('hold')">Hold 200 ms</button>
<button onclick="shutter('press')" class="secondary">Press</button>
<button onclick="shutter('release')" class="secondary">Release</button>
<div><small>Prefer Hold. Press without Release can leave the shutter open.</small></div>
</section>
<section>
<h2>Cameras</h2>
<table id="cameras"><tbody></tbody></table>
</section>
<section>
<h2>Settings</h2>
<div id="settings"></div>
</section>
</main>
<script>
function j(u,o){return fetch(u,o).then(function(r){return r.json();});}
function state(s){return s.control?s.control.state:"?";}
function refresh(){
  j("/api/status").then(function(s){
    var w=s.wifi||{}, b=s.battery||{}, c=s.cameras||{};
    document.getElementById("status").innerHTML=
      row("Firmware",s.version)+row("ID",s.id)+row("State",state(s))+
      row("WiFi",(w.connected?("up "+(w.ip||"")):"down")+(w.rssi!=null?(" "+w.rssi+" dBm"):""))+
      row("Cameras",(c.connected||0)+" / "+(c.total||0))+
      row("Battery",(b.level!=null?b.level+"%":"?")+" "+(b.charging?"(charging)":""))+
      row("Shutter",s.shutter&&s.shutter.held?"held":"idle");
  }).catch(function(){document.getElementById("status").textContent="offline";});
  j("/api/cameras").then(function(l){
    var t="";l.forEach(function(c){t+="<tr><td>"+esc(c.name)+"</td><td>"+
      (c.connected?"connected":"idle")+"</td><td>"+(c.rssi!=null?c.rssi+" dBm":"")+"</td></tr>";});
    document.querySelector("#cameras tbody").innerHTML=t||"<tr><td>no active cameras</td></tr>";
  });
}
function row(k,v){return '<div class="row"><span class="k">'+k+'</span><span>'+esc(v)+'</span></div>';}
function esc(s){return String(s==null?"":s).replace(/[&<>]/g,function(m){return{"&":"&amp;","<":"&lt;",">":"&gt;"}[m];});}
function shutter(a){j("/api/shutter",{method:"POST",headers:{"Content-Type":"application/json"},
  body:JSON.stringify({action:a,ms:200})}).then(refresh);}
function loadSettings(){
  j("/api/settings").then(function(list){
    var h="";
    list.forEach(function(s){
      var v=s.value;
      var id="set_"+s.id;
      if(s.type=="bool"){
        h+='<div class="field"><label>'+esc(s.name)+'</label>'+
           '<input type="checkbox" id="'+id+'" '+(v?"checked":"")+
           ' onchange="save('+s.id+',this.checked)"></div>';
      } else if(s.type=="string"){
        var ph=s.secret?(s.set?"(set)":""):"";
        h+='<div class="field"><label>'+esc(s.name)+'</label>'+
           '<input type="text" id="'+id+'" value="'+(s.secret?"":esc(v))+'" placeholder="'+ph+'">'+
           ' <button class="secondary" onclick="save('+s.id+',document.getElementById(\''+id+'\').value)">Set</button></div>';
      } else if(s.type=="u8"||s.type=="u32"){
        h+='<div class="field"><label>'+esc(s.name)+'</label>'+
           '<input type="number" id="'+id+'" value="'+(v==null?0:v)+'">'+
           ' <button class="secondary" onclick="save('+s.id+',Number(document.getElementById(\''+id+'\').value))">Set</button></div>';
      }
    });
    document.getElementById("settings").innerHTML=h;
  });
}
function save(id,value){
  j("/api/settings",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({id:id,value:value})}).then(function(){loadSettings();refresh();});
}
refresh();loadSettings();setInterval(refresh,3000);
</script>
</body>
</html>)HTML";

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

const char *typeName(Camera::Type type) {
  switch (type) {
    case Camera::Type::FUJIFILM_BASIC:
      return "fujifilm_basic";
    case Camera::Type::CANON_EOS_SMART:
      return "canon_eos_smart";
    case Camera::Type::CANON_EOS_REMOTE:
      return "canon_eos_remote";
    case Camera::Type::MOBILE_DEVICE:
      return "mobile_device";
    case Camera::Type::FAUXNY:
      return "fauxny";
    case Camera::Type::NIKON:
      return "nikon";
    case Camera::Type::SONY:
      return "sony";
    case Camera::Type::FUJIFILM_SECURE:
      return "fujifilm_secure";
    case Camera::Type::RICOH:
      return "ricoh";
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

/** Redact secrets: never expose a stored passphrase in a response. */
bool isSecret(Settings::type_t type) {
  return type == Settings::MQTT_PASS;
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

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Reload = true;
  }

  if (!enabled && (m_Task == nullptr)) {
    return;
  }

  if (m_Task == nullptr) {
    const BaseType_t result = xTaskCreate(taskEntry, "webui", 4096, this, 2, &m_Task);
    if (result != pdPASS) {
      m_Task = nullptr;
      ESP_LOGE(WEB_LOG_TAG, "Failed to create WebUI task.");
    }
  }
}

void WebUI::taskEntry(void *param) {
  static_cast<WebUI *>(param)->task();
}

void WebUI::task(void) {
  while (true) {
    bool reload = false;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      reload = m_Reload;
      m_Reload = false;
    }

    const bool enabled = Settings::load<Settings::WEB_UI>();

    if (reload && (m_Server != nullptr)) {
      stopServer();
    }

    if (!enabled) {
      if (m_Server != nullptr) {
        stopServer();
      }
      vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      continue;
    }

    if ((m_Server == nullptr) && networkReady()) {
      startServer();
    }

    vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
  }
}

bool WebUI::networkReady(void) const {
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == nullptr) {
    const uint32_t now = static_cast<uint32_t>(nowMs());
    if ((now - m_LastNetworkLogMs) > 10000) {
      m_LastNetworkLogMs = now;
      ESP_LOGI(WEB_LOG_TAG, "Waiting for the WiFi station interface.");
    }
    return false;
  }

  esp_netif_ip_info_t info = {};
  return esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0;
}

bool WebUI::startServer(void) {
  if (m_Server != nullptr) {
    return true;
  }

  if (m_HoldTimer == nullptr) {
    const esp_timer_create_args_t args = {
        .callback = holdTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "webui_hold",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&args, &m_HoldTimer);
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HTTP_PORT;
  config.stack_size = 6144;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&m_Server, &config);
  if (err != ESP_OK) {
    m_Server = nullptr;
    ESP_LOGW(WEB_LOG_TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return false;
  }

  const httpd_uri_t routes[] = {
      {"/",             HTTP_GET,  handleRoot,         this},
      {"/api/status",   HTTP_GET,  handleStatus,       this},
      {"/api/cameras",  HTTP_GET,  handleCameras,      this},
      {"/api/shutter",  HTTP_POST, handleShutter,      this},
      {"/api/settings", HTTP_GET,  handleSettingsGet,  this},
      {"/api/settings", HTTP_POST, handleSettingsPost, this},
  };
  for (const auto &route : routes) {
    httpd_register_uri_handler(m_Server, &route);
  }

  m_Running.store(true);
  ESP_LOGI(WEB_LOG_TAG, "WebUI serving on port %u.", static_cast<unsigned>(HTTP_PORT));
  return true;
}

void WebUI::stopServer(void) {
  releaseHold();

  httpd_handle_t server = m_Server;
  m_Server = nullptr;
  m_Running.store(false);
  if (server != nullptr) {
    httpd_stop(server);
    ESP_LOGI(WEB_LOG_TAG, "WebUI stopped.");
  }
}

esp_err_t WebUI::recvBody(httpd_req_t *req, std::string &out) {
  const size_t total = req->content_len;
  if (total > MAX_BODY_BYTES) {
    return ESP_FAIL;
  }

  out.clear();
  out.reserve(total);
  char buffer[256];
  size_t received = 0;
  while (received < total) {
    const int chunk = httpd_req_recv(req, buffer, std::min(sizeof(buffer), total - received));
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (chunk <= 0) {
      return ESP_FAIL;
    }
    out.append(buffer, static_cast<size_t>(chunk));
    received += static_cast<size_t>(chunk);
  }
  return ESP_OK;
}

esp_err_t WebUI::sendJSON(httpd_req_t *req, const std::string &json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t WebUI::sendJSONError(httpd_req_t *req, const char *status, const std::string &message) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "error", message.c_str());
  const std::string payload = jsonString(root);
  cJSON_Delete(root);

  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, payload.c_str(), static_cast<ssize_t>(payload.size()));
}

esp_err_t WebUI::handleRoot(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebUI::handleStatus(httpd_req_t *req) {
  auto *self = static_cast<WebUI *>(req->user_ctx);
  return sendJSON(req, self->buildStatusJSON());
}

esp_err_t WebUI::handleCameras(httpd_req_t *req) {
  auto *self = static_cast<WebUI *>(req->user_ctx);
  return sendJSON(req, self->buildCamerasJSON());
}

esp_err_t WebUI::handleShutter(httpd_req_t *req) {
  auto *self = static_cast<WebUI *>(req->user_ctx);
  std::string body;
  if (recvBody(req, body) != ESP_OK) {
    return sendJSONError(req, "400 Bad Request", "invalid request body");
  }

  std::string error;
  if (!self->applyShutter(body, error)) {
    return sendJSONError(req, "409 Conflict", error);
  }
  return sendJSON(req, "{\"ok\":true}");
}

esp_err_t WebUI::handleSettingsGet(httpd_req_t *req) {
  auto *self = static_cast<WebUI *>(req->user_ctx);
  return sendJSON(req, self->buildSettingsJSON());
}

esp_err_t WebUI::handleSettingsPost(httpd_req_t *req) {
  auto *self = static_cast<WebUI *>(req->user_ctx);
  std::string body;
  if (recvBody(req, body) != ESP_OK) {
    return sendJSONError(req, "400 Bad Request", "invalid request body");
  }

  std::string error;
  if (!self->applySettings(body, error)) {
    return sendJSONError(req, "400 Bad Request", error);
  }
  return sendJSON(req, self->buildSettingsJSON());
}

std::string WebUI::buildStatusJSON(void) const {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "version", FURBLE_VERSION);
  cJSON_AddStringToObject(root, "id", Device::getStringID().c_str());

  auto &control = Control::getInstance();
  cJSON *ctrl = cJSON_AddObjectToObject(root, "control");
  cJSON_AddStringToObject(ctrl, "state", stateName(control.getState()));

  cJSON *cameras = cJSON_AddObjectToObject(root, "cameras");
  cJSON_AddNumberToObject(cameras, "total", control.getTargetCount());
  cJSON_AddNumberToObject(cameras, "connected", control.getConnectedTargetCount());

  const Platform::battery_t battery = Platform::getInstance().readBattery();
  cJSON *batt = cJSON_AddObjectToObject(root, "battery");
  cJSON_AddNumberToObject(batt, "level", battery.level);
  cJSON_AddNumberToObject(batt, "voltage", battery.voltage);
  cJSON_AddNumberToObject(batt, "current", battery.current);
  cJSON_AddBoolToObject(batt, "charging", battery.charging);

  cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t info = {};
  const bool connected =
      (netif != nullptr) && (esp_netif_get_ip_info(netif, &info) == ESP_OK) && (info.ip.addr != 0);
  cJSON_AddBoolToObject(wifi, "connected", connected);
  if (connected) {
    char ip[16];
    std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    cJSON_AddStringToObject(wifi, "ip", ip);
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
    }
  }

  cJSON *shutter = cJSON_AddObjectToObject(root, "shutter");
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    cJSON_AddBoolToObject(shutter, "held", m_HoldActive);
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string WebUI::buildCamerasJSON(void) const {
  auto &control = Control::getInstance();
  const auto targets = control.getTargetStatus();

  cJSON *root = cJSON_CreateArray();

  // Only touch the shared saved-camera list when idle, matching FurbleMQTT so
  // the control task is never racing a load here.
  if (control.getState() == Control::STATE_IDLE) {
    CameraList::load();
    for (size_t n = 0; n < CameraList::size(); n++) {
      Camera *camera = CameraList::get(n);
      const std::string id = Control::getCameraID(*camera);
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "id", id.c_str());
      cJSON_AddStringToObject(item, "name", camera->getName().c_str());
      cJSON_AddStringToObject(item, "type", typeName(camera->getType()));
      cJSON_AddBoolToObject(item, "connected", false);
      cJSON_AddItemToArray(root, item);
    }
  } else {
    for (const auto &target : targets) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "id", target.id.c_str());
      cJSON_AddStringToObject(item, "name", target.name.c_str());
      cJSON_AddStringToObject(item, "type", typeName(target.type));
      cJSON_AddBoolToObject(item, "connected", target.connected);
      cJSON_AddNumberToObject(item, "progress", target.progress);
      cJSON_AddNumberToObject(item, "rssi", target.rssi);
      cJSON_AddItemToArray(root, item);
    }
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string WebUI::buildSettingsJSON(void) const {
  std::vector<const Settings::setting_t *> settings;
  for (const auto &it : Settings::all()) {
    if (it.second.wire_id != 0) {
      settings.push_back(&it.second);
    }
  }
  std::sort(settings.begin(), settings.end(),
            [](const auto *left, const auto *right) { return left->wire_id < right->wire_id; });

  cJSON *root = cJSON_CreateArray();
  for (const auto *setting : settings) {
    const CompanionService::setting_type_t type = CompanionService::settingType(setting->type);
    std::vector<uint8_t> value;
    if (!CompanionService::settingValue(setting->type, value)) {
      continue;
    }

    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", setting->wire_id);
    cJSON_AddStringToObject(item, "key", setting->key);
    cJSON_AddStringToObject(item, "name", setting->name);
    cJSON_AddStringToObject(item, "type", wireTypeName(type));

    switch (type) {
      case CompanionService::SETTING_BOOL:
        cJSON_AddBoolToObject(item, "value", !value.empty() && value[0] != 0);
        break;
      case CompanionService::SETTING_U8:
        cJSON_AddNumberToObject(item, "value", value.empty() ? 0 : value[0]);
        break;
      case CompanionService::SETTING_U32:
      {
        uint32_t number = 0;
        if (value.size() == sizeof(number)) {
          std::memcpy(&number, value.data(), sizeof(number));
        }
        cJSON_AddNumberToObject(item, "value", number);
        break;
      }
      case CompanionService::SETTING_STRING:
        if (isSecret(setting->type)) {
          cJSON_AddBoolToObject(item, "secret", true);
          cJSON_AddBoolToObject(item, "set", !value.empty());
          cJSON_AddNullToObject(item, "value");
        } else {
          cJSON_AddStringToObject(item, "value", std::string(value.begin(), value.end()).c_str());
        }
        break;
      case CompanionService::SETTING_BLOB:
        cJSON_AddNullToObject(item, "value");
        break;
    }
    cJSON_AddItemToArray(root, item);
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

bool WebUI::applyShutter(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }

  const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
  if (!cJSON_IsString(action) || (action->valuestring == nullptr)) {
    cJSON_Delete(root);
    error = "action is required";
    return false;
  }
  const std::string name = action->valuestring;

  bool ok = true;
  if (name == "press") {
    ok = sendCommand(Control::CMD_SHUTTER_PRESS);
  } else if (name == "release") {
    releaseHold();
    ok = sendCommand(Control::CMD_SHUTTER_RELEASE);
  } else if (name == "hold") {
    const cJSON *ms = cJSON_GetObjectItemCaseSensitive(root, "ms");
    uint32_t duration = 200;
    if (cJSON_IsNumber(ms)) {
      const double value = ms->valuedouble;
      duration =
          (value < 0) ? 0 : (value > MAX_HOLD_MS ? MAX_HOLD_MS : static_cast<uint32_t>(value));
    }
    ok = sendHold(duration);
  } else if (name == "focus_press") {
    ok = sendCommand(Control::CMD_FOCUS_PRESS);
  } else if (name == "focus_release") {
    ok = sendCommand(Control::CMD_FOCUS_RELEASE);
  } else {
    cJSON_Delete(root);
    error = "unknown action";
    return false;
  }

  cJSON_Delete(root);
  if (!ok) {
    error = "no camera connected";
    return false;
  }
  return true;
}

bool WebUI::applySettings(const std::string &body, std::string &error) {
  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    error = "body is not JSON";
    return false;
  }

  const Settings::setting_t *setting = nullptr;
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
  const cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
  if (cJSON_IsNumber(id)) {
    setting = Settings::getByWireId(static_cast<uint8_t>(id->valueint));
  } else if (cJSON_IsString(key) && (key->valuestring != nullptr)) {
    for (const auto &it : Settings::all()) {
      if ((it.second.wire_id != 0) && (std::strcmp(it.second.key, key->valuestring) == 0)) {
        setting = &it.second;
        break;
      }
    }
  }

  if (setting == nullptr) {
    cJSON_Delete(root);
    error = "unknown setting";
    return false;
  }

  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
  const CompanionService::setting_type_t type = CompanionService::settingType(setting->type);
  std::vector<uint8_t> encoded;

  switch (type) {
    case CompanionService::SETTING_BOOL:
    {
      bool on = false;
      if (cJSON_IsBool(value)) {
        on = cJSON_IsTrue(value);
      } else if (cJSON_IsNumber(value)) {
        on = value->valueint != 0;
      } else {
        cJSON_Delete(root);
        error = "value must be a boolean";
        return false;
      }
      encoded.push_back(on ? 1 : 0);
      break;
    }
    case CompanionService::SETTING_U8:
    {
      if (!cJSON_IsNumber(value) || (value->valueint < 0) || (value->valueint > 255)) {
        cJSON_Delete(root);
        error = "value must be 0-255";
        return false;
      }
      encoded.push_back(static_cast<uint8_t>(value->valueint));
      break;
    }
    case CompanionService::SETTING_U32:
    {
      if (!cJSON_IsNumber(value) || (value->valuedouble < 0)) {
        cJSON_Delete(root);
        error = "value must be a non-negative number";
        return false;
      }
      const uint32_t number = static_cast<uint32_t>(value->valuedouble);
      encoded.resize(sizeof(number));
      std::memcpy(encoded.data(), &number, sizeof(number));
      break;
    }
    case CompanionService::SETTING_STRING:
    {
      if (!cJSON_IsString(value) || (value->valuestring == nullptr)) {
        cJSON_Delete(root);
        error = "value must be a string";
        return false;
      }
      const std::string text = value->valuestring;
      if (text.size() > 255) {
        cJSON_Delete(root);
        error = "string is too long";
        return false;
      }
      encoded.assign(text.begin(), text.end());
      break;
    }
    case CompanionService::SETTING_BLOB:
      cJSON_Delete(root);
      error = "blob settings are not writable over REST";
      return false;
  }

  const Settings::type_t settingType = setting->type;
  cJSON_Delete(root);

  if (!CompanionService::saveSetting(settingType, encoded.data(),
                                     static_cast<uint8_t>(encoded.size()))) {
    error = "rejected";
    return false;
  }

  // Apply the same live reloads the console and companion trigger. WEB_UI is
  // deferred to the supervisor task so a handler never tears down its own
  // server.
  switch (settingType) {
    case Settings::GPS:
      GPS::getInstance().reloadSetting();
      break;
    case Settings::TX_POWER:
      Control::getInstance().setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
      break;
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      Feedback::getInstance().reload();
      break;
    case Settings::MQTT:
    case Settings::MQTT_URI:
    case Settings::MQTT_USER:
    case Settings::MQTT_PASS:
    case Settings::MQTT_BASE:
    case Settings::MQTT_HA:
      MQTT::getInstance().reloadSetting();
      break;
    case Settings::WEB_UI:
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_Reload = true;
      break;
    }
    default:
      break;
  }
  return true;
}

bool WebUI::sendCommand(Control::cmd_t command) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    return false;
  }
  return control.sendCommand(command) == pdTRUE;
}

bool WebUI::sendHold(uint32_t duration_ms) {
  releaseHold();
  if (!sendCommand(Control::CMD_SHUTTER_PRESS)) {
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_HoldActive = true;
  }

  if (m_HoldTimer == nullptr) {
    releaseHold();
    return false;
  }

  esp_timer_stop(m_HoldTimer);
  const esp_err_t err =
      esp_timer_start_once(m_HoldTimer, std::max<uint32_t>(duration_ms, 1) * 1000ULL);
  if (err != ESP_OK) {
    releaseHold();
    return false;
  }
  return true;
}

void WebUI::holdTimerCallback(void *arg) {
  static_cast<WebUI *>(arg)->releaseHold();
}

void WebUI::releaseHold(void) {
  bool active = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    active = m_HoldActive;
    m_HoldActive = false;
  }

  if (m_HoldTimer != nullptr) {
    esp_timer_stop(m_HoldTimer);
  }
  if (active) {
    sendCommand(Control::CMD_SHUTTER_RELEASE);
  }
}

}  // namespace Furble
