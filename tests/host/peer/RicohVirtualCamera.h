#ifndef FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H
#define FURBLE_HOST_RICOH_VIRTUAL_CAMERA_H

#include <string>
#include <vector>

#include "MockNimBLE.h"

namespace Furble {
namespace Host {

class RicohVirtualCamera final: public NimBLEMockPeer {
 public:
  struct Config {
    std::string name = "RICOH GR III";
    NimBLEAddress address = NimBLEAddress(0x223344556677ULL, 0);
    bool camera_bonded = false;
    bool accept_numeric_comparison = true;
  };

  struct Write {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
  };

  RicohVirtualCamera();
  explicit RicohVirtualCamera(const Config &config);

  NimBLEAdvertisedDevice advertisement() const;
  bool cameraBonded() const;
  void removeCameraBond();
  const std::vector<Write> &writes() const;
  void clearEvents();

  bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) override;
  void disconnect(NimBLEClient &client, int reason) override;
  bool hasService(const NimBLEUUID &service) const override;
  bool hasCharacteristic(const NimBLEUUID &service,
                         const NimBLEUUID &characteristic) const override;
  bool discoverCharacteristic(NimBLEClient &client,
                              const NimBLEUUID &service,
                              const NimBLEUUID &characteristic) override;
  bool canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const override;
  bool write(NimBLEClient &client,
             const NimBLEUUID &service,
             const NimBLEUUID &characteristic,
             const std::vector<uint8_t> &value,
             bool response) override;
  NimBLEAttValue read(NimBLEClient &client,
                      const NimBLEUUID &service,
                      const NimBLEUUID &characteristic) override;
  bool subscribe(NimBLEClient &client,
                 const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 bool notification,
                 NimBLERemoteCharacteristic *remote,
                 const NimBLENotifyCallback &callback,
                 bool response) override;
  bool secureConnection(NimBLEClient &client) override;
  bool updateConnectionParams(NimBLEClient &client,
                              uint16_t min_interval,
                              uint16_t max_interval,
                              uint16_t latency,
                              uint16_t timeout) override;
  int getRssi() const override;

  static const NimBLEUUID &shootingFlavorCharacteristicUUID();
  static const NimBLEUUID &operationRequestCharacteristicUUID();

 private:
  static bool matches(const NimBLEUUID &left, const NimBLEUUID &right);
  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  std::vector<Write> m_Writes;
};

}  // namespace Host
}  // namespace Furble

#endif
