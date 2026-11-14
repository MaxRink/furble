// Host conformance for the production W5500 ESP-IDF transport in
// src/FurbleEthernet.cpp. The ESP-IDF SPI, GPIO, W5500 MAC/PHY, netif and event
// loop are doubles. Link/DHCP/reconnect and teardown ordering are asserted from
// the production transport callbacks.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "FurbleEthernet.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"

namespace {

int g_Failures = 0;
int g_CallbackCount = 0;
std::string g_CallbackIP;
std::string g_State = "idle";

int g_SpiInit = 0;
int g_SpiFree = 0;
int g_MacNew = 0;
int g_MacDel = 0;
int g_PhyNew = 0;
int g_PhyDel = 0;
int g_DriverInstall = 0;
int g_DriverUninstall = 0;
int g_Start = 0;
int g_Stop = 0;
int g_NetifNew = 0;
int g_NetifDestroy = 0;
int g_GlueNew = 0;
int g_GlueDel = 0;
int g_Attach = 0;
int g_EventRegistrations = 0;
int g_EventUnregistrations = 0;
int g_MacIoctl = 0;
uint8_t g_Mac[6] = {};

struct Handler {
  esp_event_base_t base;
  int32_t id;
  esp_event_handler_t callback;
  void *arg;
  esp_event_handler_instance_t instance = nullptr;
  bool active = true;
};

std::vector<Handler> g_Handlers;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "ETH FAIL: " << message << '\n';
    g_Failures++;
  }
}

void emit(esp_event_base_t base, int32_t id, void *data) {
  for (auto &handler : g_Handlers) {
    if (!handler.active || handler.base != base) {
      continue;
    }
    if ((handler.id != ESP_EVENT_ANY_ID) && (handler.id != id)) {
      continue;
    }
    handler.callback(handler.arg, base, id, data);
  }
}

void emitLink(bool up) {
  g_State = up ? "link-up" : "link-down";
  emit(ETH_EVENT, up ? ETHERNET_EVENT_CONNECTED : ETHERNET_EVENT_DISCONNECTED, nullptr);
}

void emitGotIP(const char *text) {
  ip_event_got_ip_t event = {};
  unsigned a = 0;
  unsigned b = 0;
  unsigned c = 0;
  unsigned d = 0;
  std::sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d);
  event.ip_info.ip.addr = (a & 0xff) | ((b & 0xff) << 8) | ((c & 0xff) << 16) | ((d & 0xff) << 24);
  g_State = "got-ip";
  emit(IP_EVENT, IP_EVENT_ETH_GOT_IP, &event);
}

void emitLostIP(void) {
  g_State = "ip-lost";
  emit(IP_EVENT, IP_EVENT_ETH_LOST_IP, nullptr);
}

}  // namespace

const char *ETH_EVENT = "eth";
const char *IP_EVENT = "ip";

extern "C" {

esp_err_t esp_netif_init(void) {
  return ESP_OK;
}

esp_err_t esp_event_loop_create_default(void) {
  return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t base,
                                              int32_t id,
                                              esp_event_handler_t handler,
                                              void *arg,
                                              esp_event_handler_instance_t *instance) {
  static int next = 1;
  check(handler != nullptr, "registered a null event handler");
  g_EventRegistrations++;
  *instance = reinterpret_cast<esp_event_handler_instance_t>(static_cast<intptr_t>(next++));
  g_Handlers.push_back({base, id, handler, arg, *instance, true});
  return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,
                                                int32_t id,
                                                esp_event_handler_instance_t instance) {
  for (auto &handler : g_Handlers) {
    if (handler.instance == instance && handler.active) {
      check(handler.base == base, "event handler base changed on unregister");
      check(handler.id == id, "event handler id changed on unregister");
      handler.active = false;
      g_EventUnregistrations++;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *config, int dma) {
  (void)dma;
  g_SpiInit++;
  check(host == SPI2_HOST, "the production W5500 host is not SPI2");
  check(config != nullptr, "the SPI bus config is null");
  check(config->sclk_io_num == 13, "the W5500 SCLK pin changed");
  check(config->mosi_io_num == 11, "the W5500 MOSI pin changed");
  check(config->miso_io_num == 12, "the W5500 MISO pin changed");
  check(config->quadwp_io_num == -1 && config->quadhd_io_num == -1,
        "the W5500 bus is not single-lane");
  return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host) {
  g_SpiFree++;
  check(host == SPI2_HOST, "the wrong SPI host was freed");
  return ESP_OK;
}

esp_eth_mac_t *esp_eth_mac_new_w5500(const eth_w5500_config_t *config, const eth_mac_config_t *) {
  g_MacNew++;
  check(config != nullptr && config->spi_devcfg != nullptr,
        "the W5500 SPI device config is missing");
  check(config->spi_host == SPI2_HOST, "the W5500 MAC is on the wrong SPI host");
  check(config->int_gpio_num == 10, "the W5500 interrupt pin changed");
  check(config->spi_devcfg->command_bits == 16, "the W5500 command phase changed");
  check(config->spi_devcfg->address_bits == 8, "the W5500 address phase changed");
  check(config->spi_devcfg->clock_speed_hz == 20000000, "the W5500 SPI clock changed");
  check(config->spi_devcfg->spics_io_num == 14, "the W5500 chip select changed");
  check(config->spi_devcfg->queue_size == 20, "the W5500 SPI queue depth changed");
  auto *mac = new esp_eth_mac_t;
  mac->del = [](esp_eth_mac_t *value) {
    g_MacDel++;
    delete value;
    return ESP_OK;
  };
  return mac;
}

esp_eth_phy_t *esp_eth_phy_new_w5500(const eth_phy_config_t *config) {
  g_PhyNew++;
  check(config != nullptr && config->reset_gpio_num == 9, "the W5500 reset pin changed");
  auto *phy = new esp_eth_phy_t;
  phy->del = [](esp_eth_phy_t *value) {
    g_PhyDel++;
    delete value;
    return ESP_OK;
  };
  return phy;
}

esp_err_t esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *handle) {
  g_DriverInstall++;
  check(config != nullptr && config->mac != nullptr && config->phy != nullptr,
        "the Ethernet driver was installed without MAC/PHY");
  auto *instance = new int(0);
  *handle = reinterpret_cast<esp_eth_handle_t>(instance);
  return ESP_OK;
}

esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t handle) {
  g_DriverUninstall++;
  delete reinterpret_cast<int *>(handle);
  return ESP_OK;
}

esp_err_t esp_eth_start(esp_eth_handle_t handle) {
  g_Start++;
  check(handle != nullptr, "Ethernet start received a null handle");
  return ESP_OK;
}

esp_err_t esp_eth_stop(esp_eth_handle_t handle) {
  g_Stop++;
  check(handle != nullptr, "Ethernet stop received a null handle");
  return ESP_OK;
}

esp_err_t esp_eth_ioctl(esp_eth_handle_t handle, int command, void *data) {
  g_MacIoctl++;
  check(handle != nullptr, "Ethernet ioctl received a null handle");
  check(command == ETH_CMD_S_MAC_ADDR, "the transport set the wrong Ethernet ioctl");
  std::memcpy(g_Mac, data, sizeof(g_Mac));
  return ESP_OK;
}

esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t handle) {
  g_GlueNew++;
  check(handle != nullptr, "the netif glue received a null Ethernet handle");
  return new esp_eth_netif_glue_t;
}

esp_err_t esp_eth_del_netif_glue(esp_eth_netif_glue_handle_t glue) {
  g_GlueDel++;
  delete glue;
  return ESP_OK;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *) {
  g_NetifNew++;
  return new esp_netif_t;
}

void esp_netif_destroy(esp_netif_t *netif) {
  g_NetifDestroy++;
  delete netif;
}

esp_err_t esp_netif_attach(esp_netif_t *netif, esp_eth_netif_glue_handle_t glue) {
  g_Attach++;
  check(netif != nullptr && glue != nullptr, "the netif attach received a null object");
  netif->attached = true;
  return ESP_OK;
}

const char *esp_ip4addr_ntoa(const esp_ip4_addr_t *address, char *buffer, int length) {
  check(address != nullptr && buffer != nullptr, "the IP formatter received a null object");
  const uint32_t value = address->addr;
  std::snprintf(buffer, static_cast<size_t>(length), "%u.%u.%u.%u", value & 0xff,
                (value >> 8) & 0xff, (value >> 16) & 0xff, (value >> 24) & 0xff);
  return buffer;
}

}  // extern "C"

namespace {

void run(void) {
  Furble::Ethernet::setNetworkUpCallback([](const std::string &ip) {
    g_CallbackCount++;
    g_CallbackIP = ip;
  });

  check(Furble::Ethernet::init(), "the production W5500 transport did not initialize");
  check(g_SpiInit == 1, "the SPI bus was not initialized once");
  check(g_MacNew == 1 && g_PhyNew == 1, "the W5500 MAC/PHY were not created");
  check(g_DriverInstall == 1, "the Ethernet driver was not installed");
  check(g_Start == 1, "the Ethernet driver was not started");
  check(g_NetifNew == 1 && g_GlueNew == 1 && g_Attach == 1,
        "the Ethernet netif was not created and attached");
  check(g_MacIoctl == 1, "the MAC address ioctl was not issued");
  check(g_Mac[0] == 0x10 && g_Mac[5] == 0x15, "the Ethernet MAC address is wrong");
  check(g_EventRegistrations == 3 && g_EventUnregistrations == 0,
        "the transport did not register all three event handlers");
  check(!Furble::Ethernet::isConnected(), "Ethernet started connected");
  std::cout << "eth.init spi=1 mac=1 phy=1 event_handlers=3 clock_hz=20000000\n";

  emitGotIP("192.0.2.1");
  check(!Furble::Ethernet::isConnected(), "an IP before link-up connected Ethernet");
  check(g_CallbackCount == 0, "an IP before link-up fired the callback");

  emitLink(true);
  check(!Furble::Ethernet::isConnected(), "link-up alone connected Ethernet");
  emitGotIP("192.0.2.42");
  check(Furble::Ethernet::isConnected(), "Ethernet did not connect after DHCP");
  check(Furble::Ethernet::getIP() == "192.0.2.42", "the DHCP address is wrong");
  check(g_CallbackCount == 1 && g_CallbackIP == "192.0.2.42",
        "the network-up callback did not fire once with the DHCP address");
  std::cout << "eth.dhcp link=up ip=192.0.2.42 callback=1\n";

  emitGotIP("192.0.2.42");
  check(g_CallbackCount == 1, "a duplicate DHCP event refired the callback");

  emitLink(false);
  check(!Furble::Ethernet::isConnected(), "link-down left Ethernet connected");
  check(Furble::Ethernet::getIP().empty(), "link-down left the address in place");
  emitGotIP("192.0.2.99");
  check(!Furble::Ethernet::isConnected(), "a stale IP reconnected a down link");
  check(g_CallbackCount == 1, "a stale IP refired the callback");
  std::cout << "eth.link_down connected=0 stale_ip=ignored\n";

  emitLink(true);
  emitGotIP("198.51.100.7");
  check(Furble::Ethernet::isConnected(), "Ethernet did not reconnect");
  check(g_CallbackCount == 2 && g_CallbackIP == "198.51.100.7",
        "the reconnect did not fire a fresh callback");
  std::cout << "eth.reconnect ip=198.51.100.7 callback=2\n";

  emitLostIP();
  check(!Furble::Ethernet::isConnected(), "IP loss left Ethernet connected");
  check(Furble::Ethernet::getIP().empty(), "IP loss left the address in place");
  emitGotIP("198.51.100.7");
  check(Furble::Ethernet::isConnected(), "the same IP was not reacquired");
  check(g_CallbackCount == 3, "same-IP reacquisition did not fire the callback");
  std::cout << "eth.dhcp_lost readdr=198.51.100.7 callback=3\n";

  Furble::Ethernet::stop();
  check(!Furble::Ethernet::isConnected(), "stop left Ethernet connected");
  check(g_Stop == 1 && g_DriverUninstall == 1, "the Ethernet driver was not stopped once");
  check(g_SpiFree == 1, "the SPI bus was not freed");
  check(g_NetifDestroy == 1 && g_GlueDel == 1, "the netif/glue were not released");
  check(g_MacDel == 1 && g_PhyDel == 1, "the W5500 MAC/PHY were not released");
  check(g_EventUnregistrations == 3, "the event handlers were not unregistered");
  std::cout << "eth.stop spi_free=1 netif_destroy=1 mac_del=1 handlers=3\n";

  const int before = g_CallbackCount;
  emitLink(true);
  emitGotIP("203.0.113.5");
  check(g_CallbackCount == before, "a queued event after stop fired the callback");
  check(!Furble::Ethernet::isConnected(), "a queued event after stop revived Ethernet");

  check(Furble::Ethernet::init(), "the W5500 transport did not restart");
  check(g_SpiInit == 2 && g_NetifNew == 2 && g_EventRegistrations == 6,
        "restart did not rebuild the transport");
  emitLink(true);
  emitGotIP("203.0.113.9");
  check(Furble::Ethernet::isConnected(), "the restarted transport did not connect");
  check(g_CallbackCount == before + 1, "the restarted transport did not notify once");
  std::cout << "eth.restart handlers=6 ip=203.0.113.9\n";
  Furble::Ethernet::stop();
}

}  // namespace

int main() {
  run();
  if (g_Failures != 0) {
    std::cerr << "ethernet_w5500_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ethernet_w5500_test: PASS\n";
  return 0;
}
