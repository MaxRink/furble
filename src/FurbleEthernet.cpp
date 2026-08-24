#include "FurbleEthernet.h"

#include <memory>
#include <mutex>
#include <utility>

#if defined(ESP_PLATFORM) && defined(FURBLE_ETHERNET)
#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_eth_mac_spi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#endif

namespace Furble {

namespace {

std::mutex g_StateMutex;
std::unique_ptr<Ethernet::Transport> g_OwnedTransport;
Ethernet::Transport *g_Transport = nullptr;
Ethernet::NetworkUpCallback g_NetworkUpCallback;
bool g_Initialized = false;
bool g_LinkUp = false;
bool g_Connected = false;
std::string g_IP;

void handleTransportEvent(Ethernet::Transport::Event event, const std::string &ip) {
  Ethernet::NetworkUpCallback callback;
  std::string callbackIP;

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    if (!g_Initialized) {
      return;
    }

    switch (event) {
      case Ethernet::Transport::Event::LINK_UP:
        g_LinkUp = true;
        g_Connected = false;
        g_IP.clear();
        break;

      case Ethernet::Transport::Event::LINK_DOWN:
        g_LinkUp = false;
        g_Connected = false;
        g_IP.clear();
        break;

      case Ethernet::Transport::Event::GOT_IP:
        if (g_LinkUp && !ip.empty()) {
          const bool wasConnected = g_Connected;
          const bool changedIP = g_IP != ip;
          g_Connected = true;
          g_IP = ip;
          if (!wasConnected || changedIP) {
            callback = g_NetworkUpCallback;
            callbackIP = g_IP;
          }
        }
        break;
    }
  }

  if (callback) {
    callback(callbackIP);
  }
}

bool initializeTransport(Ethernet::Transport &transport,
                         std::unique_ptr<Ethernet::Transport> ownedTransport) {
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    if (g_Initialized) {
      return true;
    }

    g_OwnedTransport = std::move(ownedTransport);
    g_Transport = &transport;
    g_Initialized = true;
    g_LinkUp = false;
    g_Connected = false;
    g_IP.clear();
  }

  const Ethernet::Transport::EventCallback eventCallback = [](Ethernet::Transport::Event event,
                                                              const std::string &ip) {
    handleTransportEvent(event, ip);
  };

  if (!transport.init(eventCallback) || !transport.start()) {
    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_Initialized = false;
      g_Transport = nullptr;
      g_LinkUp = false;
      g_Connected = false;
      g_IP.clear();
    }
    transport.stop();
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_OwnedTransport.reset();
    return false;
  }

  return true;
}

#if defined(ESP_PLATFORM) && defined(FURBLE_ETHERNET)

constexpr const char *LOG_TAG = "ethernet";
constexpr int ETH_SPI_SCLK_GPIO = 13;
constexpr int ETH_SPI_MOSI_GPIO = 11;
constexpr int ETH_SPI_MISO_GPIO = 12;
constexpr int ETH_SPI_CS_GPIO = 14;
constexpr int ETH_SPI_INT_GPIO = 10;
constexpr int ETH_SPI_RST_GPIO = 9;
constexpr int ETH_SPI_CLOCK_HZ = 20 * 1000 * 1000;

class EspEthTransport final: public Ethernet::Transport {
 public:
  ~EspEthTransport() override { stop(); }

  bool init(EventCallback callback) override {
    {
      std::lock_guard<std::mutex> lock(m_CallbackMutex);
      m_Callback = std::move(callback);
    }

    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = ETH_SPI_MOSI_GPIO;
    busConfig.miso_io_num = ETH_SPI_MISO_GPIO;
    busConfig.sclk_io_num = ETH_SPI_SCLK_GPIO;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      logError("SPI bus initialization", err);
      return false;
    }
    m_SpiBusInitialized = true;

    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.command_bits = 16;
    deviceConfig.address_bits = 8;
    deviceConfig.mode = 0;
    deviceConfig.clock_speed_hz = ETH_SPI_CLOCK_HZ;
    deviceConfig.spics_io_num = ETH_SPI_CS_GPIO;
    deviceConfig.queue_size = 20;

    eth_w5500_config_t w5500Config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &deviceConfig);
    w5500Config.int_gpio_num = ETH_SPI_INT_GPIO;

    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    m_Mac = esp_eth_mac_new_w5500(&w5500Config, &macConfig);
    if (m_Mac == nullptr) {
      ESP_LOGE(LOG_TAG, "W5500 MAC creation failed");
      stop();
      return false;
    }

    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    phyConfig.reset_gpio_num = ETH_SPI_RST_GPIO;
    m_Phy = esp_eth_phy_new_w5500(&phyConfig);
    if (m_Phy == nullptr) {
      ESP_LOGE(LOG_TAG, "W5500 PHY creation failed");
      stop();
      return false;
    }

    esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(m_Mac, m_Phy);
    err = esp_eth_driver_install(&ethConfig, &m_EthHandle);
    if (err != ESP_OK) {
      logError("Ethernet driver installation", err);
      stop();
      return false;
    }

    uint8_t macAddress[6] = {};
    err = esp_read_mac(macAddress, ESP_MAC_ETH);
    if (err == ESP_OK) {
      err = esp_eth_ioctl(m_EthHandle, ETH_CMD_S_MAC_ADDR, macAddress);
    }
    if (err != ESP_OK) {
      logError("Ethernet MAC address setup", err);
      stop();
      return false;
    }

    esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
    m_Netif = esp_netif_new(&netifConfig);
    if (m_Netif == nullptr) {
      ESP_LOGE(LOG_TAG, "Ethernet netif creation failed");
      stop();
      return false;
    }

    m_NetifGlue = esp_eth_new_netif_glue(m_EthHandle);
    if (m_NetifGlue == nullptr) {
      ESP_LOGE(LOG_TAG, "Ethernet netif glue creation failed");
      stop();
      return false;
    }
    err = esp_netif_attach(m_Netif, m_NetifGlue);
    if (err != ESP_OK) {
      logError("Ethernet netif attach", err);
      stop();
      return false;
    }

    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &EspEthTransport::eventHandler,
                                     this);
    if (err != ESP_OK) {
      logError("Ethernet event registration", err);
      stop();
      return false;
    }
    m_EthHandlerRegistered = true;

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &EspEthTransport::eventHandler,
                                     this);
    if (err != ESP_OK) {
      logError("Ethernet IP event registration", err);
      stop();
      return false;
    }
    m_IpHandlerRegistered = true;

    return true;
  }

  bool start(void) override {
    if (m_EthHandle == nullptr) {
      return false;
    }

    const esp_err_t err = esp_eth_start(m_EthHandle);
    if (err != ESP_OK) {
      logError("Ethernet driver start", err);
      return false;
    }
    return true;
  }

  void stop(void) override {
    if (m_IpHandlerRegistered) {
      esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &EspEthTransport::eventHandler);
      m_IpHandlerRegistered = false;
    }
    if (m_EthHandlerRegistered) {
      esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &EspEthTransport::eventHandler);
      m_EthHandlerRegistered = false;
    }
    if (m_EthHandle != nullptr) {
      esp_eth_stop(m_EthHandle);
      esp_eth_driver_uninstall(m_EthHandle);
      m_EthHandle = nullptr;
    }
    if (m_NetifGlue != nullptr) {
      esp_eth_del_netif_glue(m_NetifGlue);
      m_NetifGlue = nullptr;
    }
    if (m_Netif != nullptr) {
      esp_netif_destroy(m_Netif);
      m_Netif = nullptr;
    }
    if (m_Phy != nullptr) {
      m_Phy->del(m_Phy);
      m_Phy = nullptr;
    }
    if (m_Mac != nullptr) {
      m_Mac->del(m_Mac);
      m_Mac = nullptr;
    }
    if (m_SpiBusInitialized) {
      spi_bus_free(SPI2_HOST);
      m_SpiBusInitialized = false;
    }
    std::lock_guard<std::mutex> lock(m_CallbackMutex);
    m_Callback = nullptr;
  }

 private:
  static void logError(const char *operation, esp_err_t err) {
    ESP_LOGE(LOG_TAG, "%s failed: %s", operation, esp_err_to_name(err));
  }

  static void eventHandler(void *arg,
                           esp_event_base_t eventBase,
                           int32_t eventId,
                           void *eventData) {
    auto *transport = static_cast<EspEthTransport *>(arg);
    if (transport == nullptr) {
      return;
    }

    EventCallback callback;
    {
      std::lock_guard<std::mutex> lock(transport->m_CallbackMutex);
      callback = transport->m_Callback;
    }
    if (!callback) {
      return;
    }

    if ((eventBase == ETH_EVENT) && (eventId == ETHERNET_EVENT_CONNECTED)) {
      callback(Event::LINK_UP, "");
      return;
    }
    if ((eventBase == ETH_EVENT) && (eventId == ETHERNET_EVENT_DISCONNECTED)) {
      callback(Event::LINK_DOWN, "");
      return;
    }
    if ((eventBase == IP_EVENT) && (eventId == IP_EVENT_ETH_GOT_IP)) {
      char ip[16] = {};
      auto *gotIp = static_cast<ip_event_got_ip_t *>(eventData);
      if (gotIp != nullptr) {
        esp_ip4addr_ntoa(&gotIp->ip_info.ip, ip, sizeof(ip));
      }
      callback(Event::GOT_IP, ip);
    }
  }

  EventCallback m_Callback;
  std::mutex m_CallbackMutex;
  esp_eth_handle_t m_EthHandle = nullptr;
  esp_eth_mac_t *m_Mac = nullptr;
  esp_eth_phy_t *m_Phy = nullptr;
  esp_netif_t *m_Netif = nullptr;
  esp_eth_netif_glue_handle_t m_NetifGlue = nullptr;
  bool m_SpiBusInitialized = false;
  bool m_EthHandlerRegistered = false;
  bool m_IpHandlerRegistered = false;
};

#endif

}  // namespace

bool Ethernet::init(void) {
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    if (g_Initialized) {
      return true;
    }
  }

#if defined(ESP_PLATFORM) && defined(FURBLE_ETHERNET)
  const esp_err_t netifError = esp_netif_init();
  if (netifError != ESP_OK && netifError != ESP_ERR_INVALID_STATE) {
    ESP_LOGE("ethernet", "Network interface initialization failed: %s",
             esp_err_to_name(netifError));
    return false;
  }

  const esp_err_t eventError = esp_event_loop_create_default();
  if (eventError != ESP_OK && eventError != ESP_ERR_INVALID_STATE) {
    ESP_LOGE("ethernet", "Default event loop initialization failed: %s",
             esp_err_to_name(eventError));
    return false;
  }

  auto transport = std::make_unique<EspEthTransport>();
  auto *rawTransport = transport.get();
  return initializeTransport(*rawTransport, std::move(transport));
#else
  return false;
#endif
}

bool Ethernet::init(Transport &transport) {
  return initializeTransport(transport, nullptr);
}

void Ethernet::setNetworkUpCallback(NetworkUpCallback callback) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_NetworkUpCallback = std::move(callback);
}

void Ethernet::stop(void) {
  Ethernet::Transport *transport = nullptr;
  std::unique_ptr<Ethernet::Transport> ownedTransport;
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    if (!g_Initialized) {
      return;
    }
    g_Initialized = false;
    g_LinkUp = false;
    g_Connected = false;
    g_IP.clear();
    transport = g_Transport;
    g_Transport = nullptr;
    ownedTransport = std::move(g_OwnedTransport);
  }

  if (transport != nullptr) {
    transport->stop();
  }
  ownedTransport.reset();
}

bool Ethernet::isConnected(void) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_LinkUp && g_Connected;
}

std::string Ethernet::getIP(void) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_IP;
}

}  // namespace Furble
