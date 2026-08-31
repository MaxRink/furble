#ifndef FURBLE_SIM_VIRTUAL_BLE_RUNTIME_H
#define FURBLE_SIM_VIRTUAL_BLE_RUNTIME_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Furble {
namespace Sim {
namespace Ble {

/*
 * This is a host-side transport boundary, not a C++ replacement for NimBLE.
 * Production code is allowed to use this boundary only after the pinned
 * esp-nimble-cpp host/controller has been qualified.  The default result of a
 * synthetic run therefore remains UNCERTIFIED in the camera oracle.
 */

enum class Result : uint8_t {
  OK,
  UNSUPPORTED,
  INVALID_STATE,
  INVALID_ARGUMENT,
  NOT_FOUND,
  LINK_DOWN,
  SECURITY_REQUIRED,
  AUTHENTICATION_FAILED,
  NOT_PERMITTED,
  ATT_ERROR,
  TIMEOUT,
  FAULT,
};

enum class Provenance : uint8_t {
  SYNTHETIC,
};

const char *resultName(Result result);

enum class AttStatus : uint8_t {
  SUCCESS = 0x00,
  INVALID_HANDLE = 0x01,
  READ_NOT_PERMITTED = 0x02,
  WRITE_NOT_PERMITTED = 0x03,
  INVALID_PDU = 0x04,
  INSUFFICIENT_AUTHENTICATION = 0x05,
  REQUEST_NOT_SUPPORTED = 0x06,
  INVALID_OFFSET = 0x07,
  INSUFFICIENT_AUTHORIZATION = 0x08,
  PREPARE_QUEUE_FULL = 0x09,
  ATTRIBUTE_NOT_FOUND = 0x0a,
  ATTRIBUTE_NOT_LONG = 0x0b,
  INSUFFICIENT_KEY_SIZE = 0x0c,
  INVALID_ATTRIBUTE_VALUE_LENGTH = 0x0d,
  UNLIKELY_ERROR = 0x0e,
  INSUFFICIENT_ENCRYPTION = 0x0f,
  UNSUPPORTED_GROUP_TYPE = 0x10,
  UNKNOWN_ERROR = 0x0e,
  PROCEDURE_ALREADY_IN_PROGRESS = 0x80,
  OUT_OF_RANGE = 0xff,
};

const char *attStatusName(AttStatus status);

enum CharacteristicProperty : uint8_t {
  PROPERTY_BROADCAST = 0x01,
  PROPERTY_READ = 0x02,
  PROPERTY_WRITE_NO_RESPONSE = 0x04,
  PROPERTY_WRITE = 0x08,
  PROPERTY_NOTIFY = 0x10,
  PROPERTY_INDICATE = 0x20,
  PROPERTY_AUTHENTICATED_SIGNED_WRITE = 0x40,
  PROPERTY_EXTENDED = 0x80,
};

enum Permission : uint8_t {
  PERMISSION_NONE = 0x00,
  PERMISSION_READ = 0x01,
  PERMISSION_READ_ENCRYPTED = 0x02,
  PERMISSION_READ_AUTHENTICATED = 0x04,
  PERMISSION_WRITE = 0x10,
  PERMISSION_WRITE_ENCRYPTED = 0x20,
  PERMISSION_WRITE_AUTHENTICATED = 0x40,
};

enum class AddressType : uint8_t {
  PUBLIC = 0,
  RANDOM_STATIC = 1,
  RANDOM_PRIVATE_RESOLVABLE = 2,
  RANDOM_PRIVATE_NON_RESOLVABLE = 3,
};

struct Address {
  std::array<uint8_t, 6> bytes {};
  AddressType type = AddressType::PUBLIC;

  bool operator==(const Address &other) const;
  bool operator!=(const Address &other) const;
  bool operator<(const Address &other) const;
  std::string toString() const;
};

struct Identity {
  Address current;
  Address identity;
  bool hasIdentity = false;
  std::array<uint8_t, 16> irk {};
  bool hasIrk = false;
};

enum class Phy : uint8_t {
  LE_1M,
  LE_2M,
  LE_CODED_S2,
  LE_CODED_S8,
};

struct ConnectionParams {
  uint16_t intervalMin = 6;
  uint16_t intervalMax = 12;
  uint16_t latency = 0;
  uint16_t supervisionTimeout = 100;

  bool valid() const;
};

struct Descriptor {
  std::string uuid;
  uint16_t handle = 0;
  uint8_t permissions = PERMISSION_NONE;
  std::vector<uint8_t> value;
  bool cccd = false;
};

struct Characteristic {
  std::string uuid;
  uint16_t declarationHandle = 0;
  uint16_t valueHandle = 0;
  uint8_t properties = 0;
  uint8_t permissions = PERMISSION_NONE;
  std::vector<uint8_t> value;
  std::vector<Descriptor> descriptors;
};

struct Service {
  std::string uuid;
  uint16_t startHandle = 0;
  uint16_t endHandle = 0;
  std::vector<Characteristic> characteristics;
};

struct Advertisement {
  Identity identity;
  std::string name;
  std::vector<std::string> serviceUuids;
  std::vector<uint8_t> manufacturerData;
  int8_t rssi = 0;
  bool extended = false;
};

struct PeerProfile {
  Advertisement advertisement;
  std::vector<Service> services;
  uint16_t preferredMtu = 23;
  Phy preferredPhy = Phy::LE_1M;
  ConnectionParams connectionParams;
  bool acceptsConnections = true;
  bool supportsMtuExchange = true;
  bool supportsPhyUpdate = true;
  bool supportsConnectionParams = true;
  bool supportsDiscovery = true;
};

enum class SmpState : uint8_t {
  IDLE,
  PAIRING,
  ENCRYPTED,
  FAILED,
};

struct SecurityState {
  bool encrypted = false;
  bool authenticated = false;
  bool bonded = false;
  uint8_t keySize = 0;
  bool applicationRegistered = false;
  SmpState smp = SmpState::IDLE;
};

struct BondRecord {
  Address identity;
  uint8_t keySize = 16;
  bool authenticated = false;
  std::array<uint8_t, 16> irk {};
  bool hasIrk = false;
};

struct RegistrationRecord {
  Address identity;
  std::string application;
  std::vector<uint8_t> token;
};

class BondStore {
 public:
  bool contains(const Address &identity) const;
  bool save(const BondRecord &record);
  bool erase(const Address &identity);
  void clear();
  size_t size() const;
  const BondRecord *find(const Address &identity) const;

 private:
  std::map<Address, BondRecord> m_Records;
};

class RegistrationStore {
 public:
  bool contains(const Address &identity, const std::string &application) const;
  bool save(const RegistrationRecord &record);
  bool erase(const Address &identity, const std::string &application);
  void clear();
  size_t size() const;
  const RegistrationRecord *find(const Address &identity, const std::string &application) const;

 private:
  std::map<std::string, RegistrationRecord> m_Records;
};

enum class PhysicalOutcome : uint8_t {
  UNKNOWN,
  NO_CHANGE,
  FOCUS,
  SHUTTER,
  VIDEO_STARTED,
  VIDEO_STOPPED,
  BULB_STARTED,
  BULB_STOPPED,
  LINK_LOST,
  CAMERA_ERROR,
};

const char *physicalOutcomeName(PhysicalOutcome outcome);

enum class Operation : uint8_t {
  ADVERTISE,
  CONNECT,
  DISCONNECT,
  DISCOVER,
  READ,
  WRITE,
  SUBSCRIBE,
  NOTIFY,
  INDICATE,
  CONFIRM,
  SECURITY,
  CONNECTION_PARAMS,
  MTU,
  PHY,
  RPA,
  BOND,
  REGISTRATION,
  PHYSICAL_OUTCOME,
};

const char *operationName(Operation operation);

enum class FaultLayer : uint8_t {
  ATT,
  GAP,
  SMP,
  CONTROLLER,
  PEER,
  PHYSICAL,
};

struct FaultOverlay {
  FaultLayer layer = FaultLayer::ATT;
  Operation operation = Operation::CONNECT;
  uint16_t handle = 0;
  uint64_t generation = 0;
  AttStatus status = AttStatus::UNLIKELY_ERROR;
  Result result = Result::FAULT;
  uint32_t remaining = 1;
  bool dropLink = false;
  bool unsupported = false;
};

struct TraceEvent {
  uint64_t index = 0;
  uint64_t atMs = 0;
  uint32_t adapterId = 0;
  uint64_t generation = 0;
  Provenance provenance = Provenance::SYNTHETIC;
  bool certificationEligible = false;
  Operation operation = Operation::CONNECT;
  uint16_t handle = 0;
  AttStatus attStatus = AttStatus::SUCCESS;
  Result result = Result::OK;
  SecurityState security;
  PhysicalOutcome physicalOutcome = PhysicalOutcome::UNKNOWN;
  bool indication = false;
  std::vector<uint8_t> payload;
  std::string detail;
};

struct OperationResult {
  Result result = Result::INVALID_STATE;
  AttStatus attStatus = AttStatus::UNLIKELY_ERROR;
  uint32_t adapterId = 0;
  uint64_t generation = 0;
  Provenance provenance = Provenance::SYNTHETIC;
  bool certificationEligible = false;
  PhysicalOutcome physicalOutcome = PhysicalOutcome::UNKNOWN;
  std::vector<uint8_t> value;

  bool ok() const { return result == Result::OK && attStatus == AttStatus::SUCCESS; }
};

class Link;

class Peer {
 public:
  virtual ~Peer() = default;
  virtual const PeerProfile &profile() const = 0;
  virtual void onConnected(Link &link) = 0;
  virtual OperationResult onWrite(Link &link,
                                  uint16_t handle,
                                  const std::vector<uint8_t> &value,
                                  bool withResponse) = 0;
  virtual OperationResult onRead(Link &link, uint16_t handle) = 0;
  virtual void onPhysicalOutcome(Link &link, uint16_t handle, PhysicalOutcome outcome) = 0;
  virtual void onConfirm(Link &link, uint16_t handle) = 0;
  virtual void onDisconnect(Link &link, uint8_t reason) = 0;
};

class Link {
 public:
  uint32_t adapterId() const;
  uint64_t generation() const;
  const Address &address() const;
  const Identity &identity() const;
  const PeerProfile &profile() const;
  const SecurityState &security() const;
  const ConnectionParams &connectionParams() const;
  uint16_t mtu() const;
  Phy phy() const;
  bool connected() const;
  bool subscribed(uint16_t valueHandle, bool indication) const;

 private:
  friend class VirtualBleRuntime;
  struct State;
  explicit Link(State *state);
  State *m_State;
};

class VirtualBleRuntime {
 public:
  VirtualBleRuntime();
  ~VirtualBleRuntime();

  VirtualBleRuntime(const VirtualBleRuntime &) = delete;
  VirtualBleRuntime &operator=(const VirtualBleRuntime &) = delete;

  void reset();
  uint64_t nowMs() const;
  void advance(uint64_t milliseconds);
  uint32_t nextAdapterId() const;

  OperationResult advertise(Peer &peer);
  OperationResult connect(Peer &peer, const Address &address);
  OperationResult connect(const std::shared_ptr<Peer> &peer, const Address &address);
  OperationResult disconnect(uint32_t adapterId, uint8_t reason = 0x13);
  OperationResult discover(uint32_t adapterId);
  OperationResult read(uint32_t adapterId,
                       uint16_t handle,
                       size_t offset = 0,
                       size_t maxLength = 0);
  OperationResult write(uint32_t adapterId,
                        uint16_t handle,
                        const std::vector<uint8_t> &value,
                        bool withResponse,
                        size_t offset = 0);
  OperationResult writeLong(uint32_t adapterId,
                            uint16_t handle,
                            const std::vector<uint8_t> &value,
                            size_t offset,
                            bool finalChunk,
                            bool withResponse = true);
  OperationResult subscribe(uint32_t adapterId, uint16_t valueHandle, bool indication);
  OperationResult notify(uint32_t adapterId,
                         uint16_t valueHandle,
                         const std::vector<uint8_t> &value);
  OperationResult indicate(uint32_t adapterId,
                           uint16_t valueHandle,
                           const std::vector<uint8_t> &value);
  OperationResult confirm(uint32_t adapterId, uint16_t valueHandle);
  OperationResult secure(uint32_t adapterId, bool authenticated = false, uint8_t keySize = 16);
  OperationResult registerApplication(uint32_t adapterId,
                                      const std::string &application,
                                      const std::vector<uint8_t> &token = {});
  OperationResult updateConnectionParams(uint32_t adapterId, const ConnectionParams &params);
  OperationResult exchangeMtu(uint32_t adapterId, uint16_t mtu);
  OperationResult updatePhy(uint32_t adapterId, Phy phy);
  OperationResult resolveRpa(uint32_t adapterId, const Address &current);

  bool addFault(const FaultOverlay &fault);
  void clearFaults();
  const std::vector<TraceEvent> &trace() const;
  std::vector<TraceEvent> takeTrace();
  const BondStore &bonds() const;
  BondStore &bonds();
  const RegistrationStore &registrations() const;
  RegistrationStore &registrations();
  size_t liveLinks() const;
  size_t retiredIdentityCount() const;
  bool certificationEligible() const;
  const Link *link(uint32_t adapterId) const;
  Link *link(uint32_t adapterId);

 private:
  struct State;
  std::unique_ptr<State> m_State;

  OperationResult unsupported(Operation operation,
                              uint32_t adapterId,
                              uint64_t generation,
                              const std::string &detail);
  OperationResult result(Operation operation,
                         uint32_t adapterId,
                         uint64_t generation,
                         Result result,
                         AttStatus status,
                         uint16_t handle,
                         const std::vector<uint8_t> &payload = {},
                         PhysicalOutcome physicalOutcome = PhysicalOutcome::UNKNOWN,
                         const std::string &detail = {});
  FaultOverlay *faultFor(FaultLayer layer,
                         Operation operation,
                         uint16_t handle,
                         uint64_t generation = 0);
  bool faultValid(const FaultOverlay &fault) const;
  void disconnectForFault(Link::State *state);
  void schedulePhysical(uint32_t adapterId,
                        uint64_t generation,
                        uint16_t handle,
                        PhysicalOutcome outcome,
                        uint64_t delayMs);
  void retire(Link::State *state);
  OperationResult connectPeer(Peer &peer,
                              const std::shared_ptr<Peer> &owner,
                              const Address &address);
};

class SyntheticConformancePeer final: public Peer {
 public:
  explicit SyntheticConformancePeer(PeerProfile profile);

  const PeerProfile &profile() const override;
  void onConnected(Link &link) override;
  OperationResult onWrite(Link &link,
                          uint16_t handle,
                          const std::vector<uint8_t> &value,
                          bool withResponse) override;
  OperationResult onRead(Link &link, uint16_t handle) override;
  void onPhysicalOutcome(Link &link, uint16_t handle, PhysicalOutcome outcome) override;
  void onConfirm(Link &link, uint16_t handle) override;
  void onDisconnect(Link &link, uint8_t reason) override;

  void setPhysicalOutcome(uint16_t handle, PhysicalOutcome outcome);
  void setWriteResult(uint16_t handle, const OperationResult &result);
  void setReadResult(uint16_t handle, const OperationResult &result);
  PhysicalOutcome physicalOutcome(uint16_t handle) const;
  size_t confirmations() const;
  size_t disconnects() const;
  bool leakFree() const;

 private:
  PeerProfile m_Profile;
  std::map<uint16_t, PhysicalOutcome> m_PhysicalOutcomes;
  std::map<uint16_t, PhysicalOutcome> m_LastPhysicalOutcomes;
  std::map<uint16_t, OperationResult> m_WriteResults;
  std::map<uint16_t, OperationResult> m_ReadResults;
  size_t m_Confirmations = 0;
  size_t m_Disconnects = 0;
  size_t m_LiveLinks = 0;
};

}  // namespace Ble
}  // namespace Sim
}  // namespace Furble

#endif
