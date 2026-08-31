#include "VirtualBleRuntime.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Furble {
namespace Sim {
namespace Ble {
namespace {

const char *resultNames[] = {
    "OK",
    "UNSUPPORTED",
    "INVALID_STATE",
    "INVALID_ARGUMENT",
    "NOT_FOUND",
    "LINK_DOWN",
    "SECURITY_REQUIRED",
    "AUTHENTICATION_FAILED",
    "NOT_PERMITTED",
    "ATT_ERROR",
    "TIMEOUT",
    "FAULT",
};

const char *attNames[] = {
    "SUCCESS",
    "INVALID_HANDLE",
    "READ_NOT_PERMITTED",
    "WRITE_NOT_PERMITTED",
    "INVALID_PDU",
    "INSUFFICIENT_AUTHENTICATION",
    "REQUEST_NOT_SUPPORTED",
    "INVALID_OFFSET",
    "INSUFFICIENT_AUTHORIZATION",
    "PREPARE_QUEUE_FULL",
    "ATTRIBUTE_NOT_FOUND",
    "ATTRIBUTE_NOT_LONG",
    "INSUFFICIENT_KEY_SIZE",
    "INVALID_ATTRIBUTE_VALUE_LENGTH",
    "UNLIKELY_ERROR",
    "INSUFFICIENT_ENCRYPTION",
    "UNSUPPORTED_GROUP_TYPE",
};

const char *operationNames[] = {
    "ADVERTISE", "CONNECT", "DISCONNECT", "DISCOVER", "READ",         "WRITE",
    "SUBSCRIBE", "NOTIFY",  "INDICATE",   "CONFIRM",  "SECURITY",     "CONNECTION_PARAMS",
    "MTU",       "PHY",     "RPA",        "BOND",     "REGISTRATION", "PHYSICAL_OUTCOME",
};

const char *physicalNames[] = {
    "UNKNOWN",       "NO_CHANGE",    "FOCUS",        "SHUTTER",   "VIDEO_STARTED",
    "VIDEO_STOPPED", "BULB_STARTED", "BULB_STOPPED", "LINK_LOST", "CAMERA_ERROR",
};

bool requiresEncryption(uint8_t permissions, bool read) {
  return (permissions & (read ? PERMISSION_READ_ENCRYPTED : PERMISSION_WRITE_ENCRYPTED)) != 0;
}

bool requiresAuthentication(uint8_t permissions, bool read) {
  return (permissions & (read ? PERMISSION_READ_AUTHENTICATED : PERMISSION_WRITE_AUTHENTICATED))
         != 0;
}

bool hasPermission(uint8_t permissions, bool read) {
  const uint8_t readPermissions =
      PERMISSION_READ | PERMISSION_READ_ENCRYPTED | PERMISSION_READ_AUTHENTICATED;
  const uint8_t writePermissions =
      PERMISSION_WRITE | PERMISSION_WRITE_ENCRYPTED | PERMISSION_WRITE_AUTHENTICATED;
  return (permissions & (read ? readPermissions : writePermissions)) != 0;
}

struct Attribute {
  uint16_t handle = 0;
  uint8_t properties = 0;
  uint8_t permissions = PERMISSION_NONE;
  const std::vector<uint8_t> *value = nullptr;
  bool descriptor = false;
  bool cccd = false;
  const Service *service = nullptr;
  const Characteristic *characteristic = nullptr;
  const Descriptor *descriptorValue = nullptr;
};

bool findAttribute(const PeerProfile &profile, uint16_t handle, Attribute &attribute) {
  for (const Service &service : profile.services) {
    for (const Characteristic &characteristic : service.characteristics) {
      if (characteristic.valueHandle == handle) {
        attribute.handle = handle;
        attribute.properties = characteristic.properties;
        attribute.permissions = characteristic.permissions;
        attribute.value = &characteristic.value;
        attribute.service = &service;
        attribute.characteristic = &characteristic;
        return true;
      }
      for (const Descriptor &descriptor : characteristic.descriptors) {
        if (descriptor.handle == handle) {
          attribute.handle = handle;
          attribute.permissions = descriptor.permissions;
          attribute.value = &descriptor.value;
          attribute.descriptor = true;
          attribute.cccd = descriptor.cccd;
          attribute.service = &service;
          attribute.characteristic = &characteristic;
          attribute.descriptorValue = &descriptor;
          return true;
        }
      }
    }
  }
  return false;
}

bool findCharacteristic(const PeerProfile &profile,
                        uint16_t valueHandle,
                        const Characteristic *&characteristic,
                        const Service *&service) {
  for (const Service &candidate : profile.services) {
    for (const Characteristic &entry : candidate.characteristics) {
      if (entry.valueHandle == valueHandle) {
        characteristic = &entry;
        service = &candidate;
        return true;
      }
    }
  }
  return false;
}

std::string registrationKey(const Address &identity, const std::string &application) {
  return identity.toString() + "\n" + std::to_string(static_cast<unsigned>(identity.type)) + "\n"
         + application;
}

bool validAddress(const Address &address) {
  if (address == Address {}) {
    return false;
  }
  switch (address.type) {
    case AddressType::PUBLIC:
    case AddressType::RANDOM_STATIC:
    case AddressType::RANDOM_PRIVATE_RESOLVABLE:
    case AddressType::RANDOM_PRIVATE_NON_RESOLVABLE:
      return true;
  }
  return false;
}

bool validPhy(Phy phy) {
  return phy == Phy::LE_1M || phy == Phy::LE_2M || phy == Phy::LE_CODED_S2
         || phy == Phy::LE_CODED_S8;
}

uint8_t galoisMultiply(uint8_t left, uint8_t right) {
  uint8_t product = 0;
  for (unsigned bit = 0; bit < 8; ++bit) {
    if ((right & 1U) != 0) {
      product ^= left;
    }
    const bool high = (left & 0x80U) != 0;
    left = static_cast<uint8_t>(left << 1);
    if (high) {
      left ^= 0x1bU;
    }
    right = static_cast<uint8_t>(right >> 1);
  }
  return product;
}

uint8_t rotateLeft(uint8_t value, unsigned bits) {
  return static_cast<uint8_t>((value << bits) | (value >> (8U - bits)));
}

uint8_t aesSbox(uint8_t value) {
  uint8_t inverse = 1;
  uint8_t power = value;
  unsigned exponent = 254;
  while (exponent != 0) {
    if ((exponent & 1U) != 0) {
      inverse = galoisMultiply(inverse, power);
    }
    power = galoisMultiply(power, power);
    exponent >>= 1;
  }
  if (value == 0) {
    inverse = 0;
  }
  return static_cast<uint8_t>(inverse ^ rotateLeft(inverse, 1) ^ rotateLeft(inverse, 2)
                              ^ rotateLeft(inverse, 3) ^ rotateLeft(inverse, 4) ^ 0x63U);
}

void aes128Encrypt(const std::array<uint8_t, 16> &key,
                   const std::array<uint8_t, 16> &input,
                   std::array<uint8_t, 16> &output) {
  std::array<uint8_t, 176> roundKeys {};
  std::copy(key.begin(), key.end(), roundKeys.begin());
  uint8_t rcon = 1;
  for (size_t offset = 16; offset < roundKeys.size(); offset += 4) {
    std::array<uint8_t, 4> word = {roundKeys[offset - 4], roundKeys[offset - 3],
                                   roundKeys[offset - 2], roundKeys[offset - 1]};
    if (offset % 16 == 0) {
      const uint8_t first = word[0];
      word[0] = aesSbox(word[1]) ^ rcon;
      word[1] = aesSbox(word[2]);
      word[2] = aesSbox(word[3]);
      word[3] = aesSbox(first);
      rcon = galoisMultiply(rcon, 2);
    }
    for (size_t index = 0; index < 4; ++index) {
      roundKeys[offset + index] = roundKeys[offset - 16 + index] ^ word[index];
    }
  }

  std::array<uint8_t, 16> state = input;
  auto addRoundKey = [&state, &roundKeys](size_t round) {
    for (size_t index = 0; index < state.size(); ++index) {
      state[index] ^= roundKeys[round * 16 + index];
    }
  };
  auto substitute = [&state]() {
    for (uint8_t &value : state) {
      value = aesSbox(value);
    }
  };
  auto shiftRows = [&state]() {
    std::array<uint8_t, 16> shifted {};
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        shifted[4 * column + row] = state[4 * ((column + row) % 4) + row];
      }
    }
    state = shifted;
  };
  auto mixColumns = [&state]() {
    for (size_t column = 0; column < 4; ++column) {
      const uint8_t a0 = state[4 * column];
      const uint8_t a1 = state[4 * column + 1];
      const uint8_t a2 = state[4 * column + 2];
      const uint8_t a3 = state[4 * column + 3];
      state[4 * column] = galoisMultiply(a0, 2) ^ galoisMultiply(a1, 3) ^ a2 ^ a3;
      state[4 * column + 1] = a0 ^ galoisMultiply(a1, 2) ^ galoisMultiply(a2, 3) ^ a3;
      state[4 * column + 2] = a0 ^ a1 ^ galoisMultiply(a2, 2) ^ galoisMultiply(a3, 3);
      state[4 * column + 3] = galoisMultiply(a0, 3) ^ a1 ^ a2 ^ galoisMultiply(a3, 2);
    }
  };
  addRoundKey(0);
  for (size_t round = 1; round < 10; ++round) {
    substitute();
    shiftRows();
    mixColumns();
    addRoundKey(round);
  }
  substitute();
  shiftRows();
  addRoundKey(10);
  output = state;
}

bool rpaMatchesIrk(const Address &address, const std::array<uint8_t, 16> &irk) {
  if (address.type != AddressType::RANDOM_PRIVATE_RESOLVABLE) {
    return false;
  }
  std::array<uint8_t, 16> plaintext {};
  plaintext[13] = address.bytes[3];
  plaintext[14] = address.bytes[4];
  plaintext[15] = address.bytes[5];
  std::array<uint8_t, 16> encrypted {};
  aes128Encrypt(irk, plaintext, encrypted);
  return encrypted[0] == address.bytes[0] && encrypted[1] == address.bytes[1]
         && encrypted[2] == address.bytes[2];
}

bool validProfile(const PeerProfile &profile) {
  const Identity &identity = profile.advertisement.identity;
  const size_t advertisingLimit = profile.advertisement.extended ? 1650U : 31U;
  const size_t advertisingBytes =
      profile.advertisement.name.size() + profile.advertisement.manufacturerData.size() + 4U;
  if (!validAddress(identity.current) || profile.services.empty() || profile.preferredMtu < 23
      || profile.preferredMtu > 517 || profile.advertisement.name.empty()
      || !profile.connectionParams.valid() || !validPhy(profile.preferredPhy)
      || advertisingBytes > advertisingLimit) {
    return false;
  }
  std::map<uint16_t, bool> handles;
  std::vector<std::pair<uint16_t, uint16_t>> ranges;
  uint16_t previousServiceStart = 0;
  std::vector<std::string> advertisedUuids;
  for (const Service &service : profile.services) {
    if (service.uuid.empty() || service.startHandle == 0 || service.startHandle > service.endHandle
        || service.startHandle <= previousServiceStart) {
      return false;
    }
    previousServiceStart = service.startHandle;
    for (const auto &range : ranges) {
      if (!(service.endHandle < range.first || service.startHandle > range.second)) {
        return false;
      }
    }
    ranges.emplace_back(service.startHandle, service.endHandle);
    uint16_t previousDeclaration = service.startHandle;
    for (const Characteristic &characteristic : service.characteristics) {
      if (characteristic.declarationHandle < service.startHandle
          || characteristic.declarationHandle > service.endHandle
          || characteristic.declarationHandle <= previousDeclaration
          || characteristic.valueHandle <= characteristic.declarationHandle
          || characteristic.valueHandle > service.endHandle || characteristic.uuid.empty()
          || handles.emplace(characteristic.declarationHandle, true).second == false
          || handles.emplace(characteristic.valueHandle, true).second == false) {
        return false;
      }
      previousDeclaration = characteristic.declarationHandle;
      uint16_t previousDescriptor = characteristic.valueHandle;
      for (const Descriptor &descriptor : characteristic.descriptors) {
        if (descriptor.handle <= previousDescriptor || descriptor.handle > service.endHandle
            || descriptor.uuid.empty() || !handles.emplace(descriptor.handle, true).second) {
          return false;
        }
        if (descriptor.cccd && descriptor.uuid != "00002902-0000-1000-8000-00805f9b34fb") {
          return false;
        }
        if (descriptor.cccd
            && (descriptor.value.size() != 2 || descriptor.value[0] != 0 || descriptor.value[1] != 0
                || (descriptor.permissions & PERMISSION_WRITE) == 0)) {
          return false;
        }
        previousDescriptor = descriptor.handle;
      }
    }
  }
  for (const std::string &advertised : profile.advertisement.serviceUuids) {
    if (std::find(advertisedUuids.begin(), advertisedUuids.end(), advertised)
        != advertisedUuids.end()) {
      return false;
    }
    advertisedUuids.push_back(advertised);
    const bool present =
        std::any_of(profile.services.begin(), profile.services.end(),
                    [&advertised](const Service &service) { return service.uuid == advertised; });
    if (!present) {
      return false;
    }
  }
  return true;
}

bool validOperationLayer(FaultLayer layer, Operation operation) {
  switch (layer) {
    case FaultLayer::ATT:
      return operation == Operation::DISCOVER || operation == Operation::READ
             || operation == Operation::WRITE || operation == Operation::SUBSCRIBE
             || operation == Operation::CONFIRM;
    case FaultLayer::GAP:
      return operation == Operation::ADVERTISE || operation == Operation::CONNECT
             || operation == Operation::DISCONNECT || operation == Operation::RPA;
    case FaultLayer::SMP:
      return operation == Operation::SECURITY || operation == Operation::BOND;
    case FaultLayer::CONTROLLER:
      return operation == Operation::CONNECTION_PARAMS || operation == Operation::MTU
             || operation == Operation::PHY;
    case FaultLayer::PEER:
      return operation == Operation::WRITE || operation == Operation::READ
             || operation == Operation::NOTIFY || operation == Operation::INDICATE
             || operation == Operation::REGISTRATION;
    case FaultLayer::PHYSICAL:
      return operation == Operation::PHYSICAL_OUTCOME;
  }
  return false;
}

}  // namespace

const char *resultName(Result result) {
  const size_t index = static_cast<size_t>(result);
  return index < (sizeof(resultNames) / sizeof(resultNames[0])) ? resultNames[index] : "UNKNOWN";
}

const char *attStatusName(AttStatus status) {
  const uint8_t value = static_cast<uint8_t>(status);
  return value < (sizeof(attNames) / sizeof(attNames[0])) ? attNames[value] : "UNKNOWN";
}

const char *physicalOutcomeName(PhysicalOutcome outcome) {
  const size_t index = static_cast<size_t>(outcome);
  return index < (sizeof(physicalNames) / sizeof(physicalNames[0])) ? physicalNames[index]
                                                                    : "UNKNOWN";
}

const char *operationName(Operation operation) {
  const size_t index = static_cast<size_t>(operation);
  return index < (sizeof(operationNames) / sizeof(operationNames[0])) ? operationNames[index]
                                                                      : "UNKNOWN";
}

bool Address::operator==(const Address &other) const {
  return type == other.type && bytes == other.bytes;
}

bool Address::operator!=(const Address &other) const {
  return !(*this == other);
}

bool Address::operator<(const Address &other) const {
  if (bytes != other.bytes) {
    return bytes < other.bytes;
  }
  return type < other.type;
}

std::string Address::toString() const {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (size_t index = bytes.size(); index > 0; --index) {
    if (index != bytes.size()) {
      stream << ':';
    }
    stream << std::setw(2) << static_cast<unsigned int>(bytes[index - 1]);
  }
  return stream.str();
}

bool ConnectionParams::valid() const {
  return intervalMin >= 6 && intervalMin <= intervalMax && intervalMax <= 0x0c80 && latency <= 499
         && supervisionTimeout >= 100 && supervisionTimeout <= 0x0c80
         && static_cast<uint32_t>(supervisionTimeout) * 4U
                > static_cast<uint32_t>(intervalMax) * (static_cast<uint32_t>(latency) + 1U);
}

bool BondStore::contains(const Address &identity) const {
  return m_Records.find(identity) != m_Records.end();
}

bool BondStore::save(const BondRecord &record) {
  if (record.identity == Address {} || record.keySize < 7 || record.keySize > 16) {
    return false;
  }
  m_Records[record.identity] = record;
  return true;
}

bool BondStore::erase(const Address &identity) {
  return m_Records.erase(identity) != 0;
}

void BondStore::clear() {
  m_Records.clear();
}

size_t BondStore::size() const {
  return m_Records.size();
}

const BondRecord *BondStore::find(const Address &identity) const {
  const auto found = m_Records.find(identity);
  return found == m_Records.end() ? nullptr : &found->second;
}

bool RegistrationStore::contains(const Address &identity, const std::string &application) const {
  return m_Records.find(registrationKey(identity, application)) != m_Records.end();
}

bool RegistrationStore::save(const RegistrationRecord &record) {
  if (record.identity == Address {} || record.application.empty()) {
    return false;
  }
  m_Records[registrationKey(record.identity, record.application)] = record;
  return true;
}

bool RegistrationStore::erase(const Address &identity, const std::string &application) {
  return m_Records.erase(registrationKey(identity, application)) != 0;
}

void RegistrationStore::clear() {
  m_Records.clear();
}

size_t RegistrationStore::size() const {
  return m_Records.size();
}

const RegistrationRecord *RegistrationStore::find(const Address &identity,
                                                  const std::string &application) const {
  const auto found = m_Records.find(registrationKey(identity, application));
  return found == m_Records.end() ? nullptr : &found->second;
}

struct Link::State {
  uint32_t adapterId = 0;
  uint64_t generation = 0;
  Address address;
  Identity identity;
  PeerProfile profile;
  Peer *peer = nullptr;
  std::shared_ptr<Peer> owner;
  SecurityState security;
  ConnectionParams connectionParams;
  uint16_t mtu = 23;
  Phy phy = Phy::LE_1M;
  bool connected = false;
  std::map<uint16_t, uint8_t> subscriptions;
  std::map<uint16_t, bool> pendingIndications;
  std::map<uint16_t, uint64_t> indicationDeadlines;
  std::map<uint16_t, std::vector<uint8_t>> longWrites;
};

Link::Link(State *state) : m_State(state) {}

uint32_t Link::adapterId() const {
  return m_State == nullptr ? 0 : m_State->adapterId;
}

uint64_t Link::generation() const {
  return m_State == nullptr ? 0 : m_State->generation;
}

const Address &Link::address() const {
  return m_State->address;
}

const Identity &Link::identity() const {
  return m_State->identity;
}

const PeerProfile &Link::profile() const {
  return m_State->profile;
}

const SecurityState &Link::security() const {
  return m_State->security;
}

const ConnectionParams &Link::connectionParams() const {
  return m_State->connectionParams;
}

uint16_t Link::mtu() const {
  return m_State->mtu;
}

Phy Link::phy() const {
  return m_State->phy;
}

bool Link::connected() const {
  return m_State != nullptr && m_State->connected;
}

bool Link::subscribed(uint16_t valueHandle, bool indication) const {
  if (m_State == nullptr) {
    return false;
  }
  const auto found = m_State->subscriptions.find(valueHandle);
  return found != m_State->subscriptions.end()
         && ((indication && (found->second & 0x02U) != 0)
             || (!indication && (found->second & 0x01U) != 0));
}

struct VirtualBleRuntime::State {
  uint64_t nowMs = 0;
  uint32_t nextAdapter = 1;
  uint64_t nextGeneration = 1;
  uint64_t nextTraceIndex = 0;
  struct PendingEvent {
    uint64_t dueMs = 0;
    uint64_t sequence = 0;
    uint32_t adapterId = 0;
    uint64_t generation = 0;
    uint16_t handle = 0;
    PhysicalOutcome physicalOutcome = PhysicalOutcome::UNKNOWN;
  };
  struct PendingController {
    uint64_t dueMs = 0;
    uint64_t sequence = 0;
    uint32_t adapterId = 0;
    uint64_t generation = 0;
    Operation operation = Operation::MTU;
    ConnectionParams params;
    uint16_t mtu = 23;
    Phy phy = Phy::LE_1M;
  };
  uint64_t nextEventSequence = 1;
  std::vector<PendingEvent> pendingEvents;
  std::vector<PendingController> pendingController;
  std::vector<Address> retiredIdentities;
  std::map<uint32_t, std::unique_ptr<Link::State>> links;
  std::map<uint32_t, std::unique_ptr<Link>> views;
  std::vector<FaultOverlay> faults;
  std::vector<TraceEvent> trace;
  BondStore bonds;
  RegistrationStore registrations;
};

VirtualBleRuntime::VirtualBleRuntime() : m_State(std::make_unique<State>()) {}

VirtualBleRuntime::~VirtualBleRuntime() {
  if (m_State == nullptr) {
    return;
  }
  std::vector<uint32_t> adapters;
  for (const auto &entry : m_State->links) {
    if (entry.second->connected) {
      adapters.push_back(entry.first);
    }
  }
  for (const uint32_t adapter : adapters) {
    disconnect(adapter, 0x13);
  }
}

void VirtualBleRuntime::reset() {
  std::vector<uint32_t> adapters;
  for (const auto &entry : m_State->links) {
    if (entry.second->connected) {
      adapters.push_back(entry.first);
    }
  }
  for (const uint32_t adapter : adapters) {
    disconnect(adapter, 0x13);
  }
  m_State = std::make_unique<State>();
}

uint64_t VirtualBleRuntime::nowMs() const {
  return m_State->nowMs;
}

void VirtualBleRuntime::advance(uint64_t milliseconds) {
  m_State->nowMs += milliseconds;
  for (;;) {
    auto next =
        std::min_element(m_State->pendingEvents.begin(), m_State->pendingEvents.end(),
                         [](const State::PendingEvent &left, const State::PendingEvent &right) {
                           return left.dueMs < right.dueMs
                                  || (left.dueMs == right.dueMs && left.sequence < right.sequence);
                         });
    if (next == m_State->pendingEvents.end() || next->dueMs > m_State->nowMs) {
      break;
    }
    const State::PendingEvent event = *next;
    m_State->pendingEvents.erase(next);
    const auto found = m_State->links.find(event.adapterId);
    if (found == m_State->links.end() || !found->second->connected
        || found->second->generation != event.generation) {
      result(Operation::PHYSICAL_OUTCOME, event.adapterId, event.generation, Result::INVALID_STATE,
             AttStatus::UNLIKELY_ERROR, event.handle, {}, PhysicalOutcome::UNKNOWN,
             "stale generation event discarded");
      continue;
    }
    Link::State *state = found->second.get();
    FaultOverlay *fault =
        faultFor(FaultLayer::PHYSICAL, Operation::PHYSICAL_OUTCOME, event.handle, event.generation);
    if (fault != nullptr) {
      if (fault->dropLink) {
        disconnectForFault(state);
      }
      result(Operation::PHYSICAL_OUTCOME, event.adapterId, event.generation,
             fault->unsupported ? Result::UNSUPPORTED : fault->result,
             fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, event.handle,
             {}, PhysicalOutcome::UNKNOWN, "physical fault overlay");
      continue;
    }
    Link link(state);
    found->second->peer->onPhysicalOutcome(link, event.handle, event.physicalOutcome);
    result(Operation::PHYSICAL_OUTCOME, event.adapterId, event.generation, Result::OK,
           AttStatus::SUCCESS, event.handle, {}, event.physicalOutcome, "scheduled peer event");
  }
  for (;;) {
    auto next = std::min_element(
        m_State->pendingController.begin(), m_State->pendingController.end(),
        [](const State::PendingController &left, const State::PendingController &right) {
          return left.dueMs < right.dueMs
                 || (left.dueMs == right.dueMs && left.sequence < right.sequence);
        });
    if (next == m_State->pendingController.end() || next->dueMs > m_State->nowMs) {
      break;
    }
    const State::PendingController event = *next;
    m_State->pendingController.erase(next);
    const auto found = m_State->links.find(event.adapterId);
    if (found == m_State->links.end() || !found->second->connected
        || found->second->generation != event.generation) {
      result(event.operation, event.adapterId, event.generation, Result::INVALID_STATE,
             AttStatus::UNLIKELY_ERROR, 0, {}, PhysicalOutcome::UNKNOWN,
             "stale controller event discarded");
      continue;
    }
    Link::State *state = found->second.get();
    std::vector<uint8_t> value;
    if (event.operation == Operation::CONNECTION_PARAMS) {
      state->connectionParams = event.params;
    } else if (event.operation == Operation::MTU) {
      state->mtu = event.mtu;
      value = {static_cast<uint8_t>(state->mtu & 0xff),
               static_cast<uint8_t>((state->mtu >> 8) & 0xff)};
    } else if (event.operation == Operation::PHY) {
      state->phy = event.phy;
      value = {static_cast<uint8_t>(state->phy)};
    }
    result(event.operation, event.adapterId, event.generation, Result::OK, AttStatus::SUCCESS, 0,
           value, PhysicalOutcome::UNKNOWN, "controller event applied");
  }
  for (const auto &entry : m_State->links) {
    Link::State *state = entry.second.get();
    for (auto &pending : state->indicationDeadlines) {
      if (state->pendingIndications[pending.first] && pending.second <= m_State->nowMs) {
        state->pendingIndications[pending.first] = false;
        result(Operation::CONFIRM, state->adapterId, state->generation, Result::TIMEOUT,
               AttStatus::UNLIKELY_ERROR, pending.first, {}, PhysicalOutcome::UNKNOWN,
               "indication confirmation timeout");
      }
    }
  }
}

uint32_t VirtualBleRuntime::nextAdapterId() const {
  return m_State->nextAdapter;
}

OperationResult VirtualBleRuntime::result(Operation operation,
                                          uint32_t adapterId,
                                          uint64_t generation,
                                          Result resultValue,
                                          AttStatus status,
                                          uint16_t handle,
                                          const std::vector<uint8_t> &payload,
                                          PhysicalOutcome physicalOutcome,
                                          const std::string &detail) {
  OperationResult operationResult;
  operationResult.result = resultValue;
  operationResult.attStatus = status;
  operationResult.adapterId = adapterId;
  operationResult.generation = generation;
  operationResult.physicalOutcome = physicalOutcome;
  operationResult.provenance = Provenance::SYNTHETIC;
  operationResult.certificationEligible = false;
  operationResult.value = payload;

  TraceEvent event;
  event.index = m_State->nextTraceIndex++;
  event.atMs = m_State->nowMs;
  event.adapterId = adapterId;
  event.generation = generation;
  event.operation = operation;
  event.handle = handle;
  event.attStatus = status;
  event.result = resultValue;
  event.provenance = Provenance::SYNTHETIC;
  event.certificationEligible = false;
  event.physicalOutcome = physicalOutcome;
  event.payload = payload;
  event.detail = detail;
  const Link::State *linkState = nullptr;
  const auto found = m_State->links.find(adapterId);
  if (found != m_State->links.end()) {
    linkState = found->second.get();
  }
  if (linkState != nullptr) {
    event.security = linkState->security;
  }
  m_State->trace.push_back(std::move(event));
  return operationResult;
}

OperationResult VirtualBleRuntime::unsupported(Operation operation,
                                               uint32_t adapterId,
                                               uint64_t generation,
                                               const std::string &detail) {
  return result(operation, adapterId, generation, Result::UNSUPPORTED,
                AttStatus::REQUEST_NOT_SUPPORTED, 0, {}, PhysicalOutcome::UNKNOWN, detail);
}

FaultOverlay *VirtualBleRuntime::faultFor(FaultLayer layer,
                                          Operation operation,
                                          uint16_t handle,
                                          uint64_t generation) {
  for (FaultOverlay &fault : m_State->faults) {
    if (fault.remaining != 0 && fault.layer == layer && fault.operation == operation
        && (fault.handle == 0 || fault.handle == handle)
        && (fault.generation == 0 || fault.generation == generation)) {
      --fault.remaining;
      return &fault;
    }
  }
  return nullptr;
}

void VirtualBleRuntime::schedulePhysical(uint32_t adapterId,
                                         uint64_t generation,
                                         uint16_t handle,
                                         PhysicalOutcome outcome,
                                         uint64_t delayMs) {
  if (outcome == PhysicalOutcome::UNKNOWN) {
    return;
  }
  m_State->pendingEvents.push_back({m_State->nowMs + delayMs, m_State->nextEventSequence++,
                                    adapterId, generation, handle, outcome});
}

void VirtualBleRuntime::disconnectForFault(Link::State *state) {
  if (state == nullptr || !state->connected) {
    return;
  }
  state->connected = false;
  Link link(state);
  state->peer->onDisconnect(link, 0x08);
  retire(state);
  result(Operation::DISCONNECT, state->adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
         0, {}, PhysicalOutcome::LINK_LOST, "fault overlay disconnected link");
}

void VirtualBleRuntime::retire(Link::State *state) {
  if (state == nullptr || !state->identity.hasIdentity) {
    return;
  }
  m_State->retiredIdentities.push_back(state->identity.identity);
  if (m_State->retiredIdentities.size() > 32) {
    m_State->retiredIdentities.erase(m_State->retiredIdentities.begin());
  }
}

OperationResult VirtualBleRuntime::advertise(Peer &peer) {
  const FaultOverlay *fault = faultFor(FaultLayer::GAP, Operation::ADVERTISE, 0);
  if (fault != nullptr) {
    return result(Operation::ADVERTISE, 0, 0,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (!validProfile(peer.profile())) {
    return result(Operation::ADVERTISE, 0, 0, Result::INVALID_ARGUMENT, AttStatus::UNLIKELY_ERROR,
                  0, {}, PhysicalOutcome::UNKNOWN, "invalid GATT or advertising database");
  }
  return result(Operation::ADVERTISE, 0, 0, Result::OK, AttStatus::SUCCESS, 0,
                peer.profile().advertisement.manufacturerData);
}

OperationResult VirtualBleRuntime::connect(Peer &peer, const Address &address) {
  return connectPeer(peer, std::shared_ptr<Peer>(&peer, [](Peer *) {}), address);
}

OperationResult VirtualBleRuntime::connect(const std::shared_ptr<Peer> &peer,
                                           const Address &address) {
  if (peer == nullptr) {
    return result(Operation::CONNECT, 0, 0, Result::INVALID_ARGUMENT, AttStatus::INVALID_HANDLE, 0,
                  {}, PhysicalOutcome::UNKNOWN, "null peer");
  }
  return connectPeer(*peer, peer, address);
}

OperationResult VirtualBleRuntime::connectPeer(Peer &peer,
                                               const std::shared_ptr<Peer> &owner,
                                               const Address &address) {
  if (m_State->nextAdapter == 0 || m_State->nextGeneration == 0) {
    return result(Operation::CONNECT, 0, 0, Result::INVALID_STATE, AttStatus::UNLIKELY_ERROR, 0, {},
                  PhysicalOutcome::UNKNOWN, "adapter or generation counter exhausted");
  }
  const uint32_t adapterId = m_State->nextAdapter++;
  const uint64_t generation = m_State->nextGeneration++;
  FaultOverlay *fault = faultFor(FaultLayer::GAP, Operation::CONNECT, 0);
  if (fault != nullptr) {
    return result(Operation::CONNECT, adapterId, generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }

  const PeerProfile &profile = peer.profile();
  const Identity &identity = profile.advertisement.identity;
  if (!validProfile(profile)) {
    return result(Operation::CONNECT, adapterId, generation, Result::INVALID_ARGUMENT,
                  AttStatus::UNLIKELY_ERROR, 0, {}, PhysicalOutcome::UNKNOWN,
                  "invalid GATT or advertising database");
  }
  if (!profile.acceptsConnections
      || (address != identity.current && (!identity.hasIdentity || address != identity.identity))) {
    return result(Operation::CONNECT, adapterId, generation, Result::NOT_FOUND,
                  AttStatus::INVALID_HANDLE, 0, {}, PhysicalOutcome::UNKNOWN,
                  "advertisement identity mismatch or peer refused connection");
  }
  if (!profile.connectionParams.valid()) {
    return result(Operation::CONNECT, adapterId, generation, Result::INVALID_ARGUMENT,
                  AttStatus::UNLIKELY_ERROR, 0, {}, PhysicalOutcome::UNKNOWN,
                  "peer advertised invalid connection parameters");
  }

  auto linkState = std::make_unique<Link::State>();
  linkState->adapterId = adapterId;
  linkState->generation = generation;
  linkState->address = address;
  linkState->identity = identity;
  linkState->profile = profile;
  linkState->peer = &peer;
  linkState->owner = owner;
  linkState->connectionParams = profile.connectionParams;
  linkState->mtu = std::min<uint16_t>(23, profile.preferredMtu == 0 ? 23 : profile.preferredMtu);
  linkState->phy = profile.preferredPhy;
  linkState->connected = true;
  if (identity.hasIdentity) {
    const BondRecord *bond = m_State->bonds.find(identity.identity);
    if (bond != nullptr) {
      linkState->security.bonded = true;
      linkState->security.encrypted = true;
      linkState->security.authenticated = bond->authenticated;
      linkState->security.keySize = bond->keySize;
      linkState->security.smp = SmpState::ENCRYPTED;
    }
  }
  if (identity.hasIdentity) {
    linkState->security.applicationRegistered =
        m_State->registrations.find(identity.identity, "furble") != nullptr;
  }
  const uint64_t traceGeneration = linkState->generation;
  m_State->links.emplace(adapterId, std::move(linkState));
  m_State->views.emplace(adapterId,
                         std::unique_ptr<Link>(new Link(m_State->links.at(adapterId).get())));
  Link link(m_State->links.at(adapterId).get());
  peer.onConnected(link);
  return result(Operation::CONNECT, adapterId, traceGeneration, Result::OK, AttStatus::SUCCESS, 0,
                {}, PhysicalOutcome::UNKNOWN, "connected");
}

OperationResult VirtualBleRuntime::disconnect(uint32_t adapterId, uint8_t reason) {
  Link::State *state = nullptr;
  const auto found = m_State->links.find(adapterId);
  if (found != m_State->links.end()) {
    state = found->second.get();
  }
  if (state == nullptr || !state->connected) {
    return result(Operation::DISCONNECT, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  const uint64_t generation = state->generation;
  FaultOverlay *fault = faultFor(FaultLayer::GAP, Operation::DISCONNECT, 0, generation);
  if (fault != nullptr) {
    return result(Operation::DISCONNECT, adapterId, generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  state->connected = false;
  Link link(state);
  state->peer->onDisconnect(link, reason);
  retire(state);
  return result(Operation::DISCONNECT, adapterId, generation, Result::OK, AttStatus::SUCCESS, 0, {},
                PhysicalOutcome::LINK_LOST);
}

OperationResult VirtualBleRuntime::discover(uint32_t adapterId) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::DISCOVER, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  if (!state->profile.supportsDiscovery) {
    return unsupported(Operation::DISCOVER, adapterId, state->generation,
                       "peer does not expose discovery through this boundary");
  }
  FaultOverlay *fault = faultFor(FaultLayer::ATT, Operation::DISCOVER, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::DISCOVER, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  return result(Operation::DISCOVER, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                0, {}, PhysicalOutcome::UNKNOWN, "complete");
}

OperationResult VirtualBleRuntime::read(uint32_t adapterId,
                                        uint16_t handle,
                                        size_t offset,
                                        size_t maxLength) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::READ, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, handle);
  }
  FaultOverlay *fault = faultFor(FaultLayer::ATT, Operation::READ, handle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::READ, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, handle);
  }
  Attribute attribute;
  if (!findAttribute(state->profile, handle, attribute)) {
    return result(Operation::READ, adapterId, state->generation, Result::NOT_FOUND,
                  AttStatus::INVALID_HANDLE, handle);
  }
  if (!attribute.descriptor && (attribute.properties & PROPERTY_READ) == 0) {
    return result(Operation::READ, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::READ_NOT_PERMITTED, handle);
  }
  if (!hasPermission(attribute.permissions, true)) {
    return result(Operation::READ, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::READ_NOT_PERMITTED, handle);
  }
  if (requiresEncryption(attribute.permissions, true) && !state->security.encrypted) {
    return result(Operation::READ, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_ENCRYPTION, handle);
  }
  if (requiresAuthentication(attribute.permissions, true) && !state->security.authenticated) {
    return result(Operation::READ, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_AUTHENTICATION, handle);
  }
  FaultOverlay *peerFault = faultFor(FaultLayer::PEER, Operation::READ, handle, state->generation);
  if (peerFault != nullptr) {
    if (peerFault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::READ, adapterId, state->generation,
                  peerFault->unsupported ? Result::UNSUPPORTED : peerFault->result,
                  peerFault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : peerFault->status,
                  handle);
  }
  Link readLink(state);
  const OperationResult peerResult = state->peer->onRead(readLink, handle);
  if (!peerResult.ok()) {
    return result(Operation::READ, adapterId, state->generation, peerResult.result,
                  peerResult.attStatus, handle, peerResult.value, peerResult.physicalOutcome);
  }
  if (offset > peerResult.value.size()) {
    return result(Operation::READ, adapterId, state->generation, Result::ATT_ERROR,
                  AttStatus::INVALID_OFFSET, handle);
  }
  const size_t available = peerResult.value.size() - offset;
  const size_t packetLimit = state->mtu - 1;
  const size_t requested = maxLength == 0 ? packetLimit : std::min(maxLength, packetLimit);
  const size_t length = std::min(available, requested);
  std::vector<uint8_t> chunk(peerResult.value.begin() + static_cast<ptrdiff_t>(offset),
                             peerResult.value.begin() + static_cast<ptrdiff_t>(offset + length));
  return result(Operation::READ, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                handle, chunk, peerResult.physicalOutcome,
                offset + length < peerResult.value.size() ? "read blob continuation" : "complete");
}

OperationResult VirtualBleRuntime::write(uint32_t adapterId,
                                         uint16_t handle,
                                         const std::vector<uint8_t> &value,
                                         bool withResponse,
                                         size_t offset) {
  return writeLong(adapterId, handle, value, offset, true, withResponse);
}

OperationResult VirtualBleRuntime::writeLong(uint32_t adapterId,
                                             uint16_t handle,
                                             const std::vector<uint8_t> &value,
                                             size_t offset,
                                             bool finalChunk,
                                             bool withResponse) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::WRITE, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, handle, value);
  }
  FaultOverlay *fault = faultFor(FaultLayer::ATT, Operation::WRITE, handle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::WRITE, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, handle,
                  value);
  }
  Attribute attribute;
  if (!findAttribute(state->profile, handle, attribute)) {
    return result(Operation::WRITE, adapterId, state->generation, Result::NOT_FOUND,
                  AttStatus::INVALID_HANDLE, handle, value);
  }
  const uint8_t requiredProperty = withResponse ? PROPERTY_WRITE : PROPERTY_WRITE_NO_RESPONSE;
  if (attribute.descriptor) {
    if (!attribute.cccd || !hasPermission(attribute.permissions, false)) {
      return result(Operation::WRITE, adapterId, state->generation, Result::NOT_PERMITTED,
                    AttStatus::WRITE_NOT_PERMITTED, handle, value);
    }
    if (value.size() != 2 || value[0] > 3 || value[1] != 0
        || (value[0] == 1 && (attribute.characteristic->properties & PROPERTY_NOTIFY) == 0)
        || (value[0] == 2 && (attribute.characteristic->properties & PROPERTY_INDICATE) == 0)) {
      return result(Operation::WRITE, adapterId, state->generation, Result::ATT_ERROR,
                    AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, handle, value);
    }
  } else if ((attribute.properties & requiredProperty) == 0) {
    return result(Operation::WRITE, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::WRITE_NOT_PERMITTED, handle, value);
  }
  if (!hasPermission(attribute.permissions, false)) {
    return result(Operation::WRITE, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::WRITE_NOT_PERMITTED, handle, value);
  }
  if (requiresEncryption(attribute.permissions, false) && !state->security.encrypted) {
    return result(Operation::WRITE, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_ENCRYPTION, handle, value);
  }
  if (requiresAuthentication(attribute.permissions, false) && !state->security.authenticated) {
    return result(Operation::WRITE, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_AUTHENTICATION, handle, value);
  }
  if (attribute.descriptor) {
    if ((value[0] & 1U) != 0 && (attribute.characteristic->properties & PROPERTY_NOTIFY) == 0) {
      return result(Operation::WRITE, adapterId, state->generation, Result::ATT_ERROR,
                    AttStatus::REQUEST_NOT_SUPPORTED, handle, value);
    }
    if ((value[0] & 2U) != 0 && (attribute.characteristic->properties & PROPERTY_INDICATE) == 0) {
      return result(Operation::WRITE, adapterId, state->generation, Result::ATT_ERROR,
                    AttStatus::REQUEST_NOT_SUPPORTED, handle, value);
    }
  }
  const size_t packetLimit = state->mtu - 3;
  if (!withResponse) {
    if (offset != 0 || value.size() > packetLimit || !finalChunk) {
      return result(Operation::WRITE, adapterId, state->generation, Result::ATT_ERROR,
                    AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, handle, value,
                    PhysicalOutcome::UNKNOWN, "no-response writes cannot be prepared");
    }
    return result(Operation::WRITE, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                  handle, value, PhysicalOutcome::UNKNOWN, "no-response accepted");
  }
  std::vector<uint8_t> operationValue = value;
  if (offset != 0 || value.size() > packetLimit || !finalChunk) {
    std::vector<uint8_t> &buffer = state->longWrites[handle];
    if (offset != buffer.size() || value.size() > packetLimit) {
      return result(Operation::WRITE, adapterId, state->generation, Result::ATT_ERROR,
                    offset != buffer.size() ? AttStatus::INVALID_OFFSET
                                            : AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH,
                    handle, value);
    }
    buffer.insert(buffer.end(), value.begin(), value.end());
    operationValue = buffer;
    if (!finalChunk) {
      return result(Operation::WRITE, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                    handle, value, PhysicalOutcome::UNKNOWN, "prepared chunk");
    }
    state->longWrites.erase(handle);
  }
  FaultOverlay *peerFault = faultFor(FaultLayer::PEER, Operation::WRITE, handle, state->generation);
  if (peerFault != nullptr) {
    if (peerFault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::WRITE, adapterId, state->generation,
                  peerFault->unsupported ? Result::UNSUPPORTED : peerFault->result,
                  peerFault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : peerFault->status,
                  handle, operationValue);
  }
  Link writeLink(state);
  const OperationResult peerResult =
      state->peer->onWrite(writeLink, handle, operationValue, withResponse);
  if (!peerResult.ok()) {
    return result(Operation::WRITE, adapterId, state->generation, peerResult.result,
                  peerResult.attStatus, handle, operationValue, PhysicalOutcome::UNKNOWN);
  }
  if (peerResult.physicalOutcome != PhysicalOutcome::UNKNOWN) {
    schedulePhysical(adapterId, state->generation, handle, peerResult.physicalOutcome, 1);
  }
  if (attribute.descriptor) {
    state->subscriptions[attribute.characteristic->valueHandle] = value[0];
  }
  return result(Operation::WRITE, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                handle, operationValue, PhysicalOutcome::UNKNOWN,
                withResponse ? "ATT complete" : "no-response accepted");
}

OperationResult VirtualBleRuntime::subscribe(uint32_t adapterId,
                                             uint16_t valueHandle,
                                             bool indication) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::SUBSCRIBE, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, valueHandle);
  }
  FaultOverlay *fault =
      faultFor(FaultLayer::ATT, Operation::SUBSCRIBE, valueHandle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::SUBSCRIBE, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status,
                  valueHandle);
  }
  const Characteristic *characteristic = nullptr;
  const Service *service = nullptr;
  if (!findCharacteristic(state->profile, valueHandle, characteristic, service)) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::NOT_FOUND,
                  AttStatus::INVALID_HANDLE, valueHandle);
  }
  const uint8_t property = indication ? PROPERTY_INDICATE : PROPERTY_NOTIFY;
  if ((characteristic->properties & property) == 0) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::REQUEST_NOT_SUPPORTED, valueHandle);
  }
  const Descriptor *cccd = nullptr;
  for (const Descriptor &descriptor : characteristic->descriptors) {
    if (descriptor.cccd) {
      cccd = &descriptor;
      break;
    }
  }
  if (cccd == nullptr) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::NOT_FOUND,
                  AttStatus::ATTRIBUTE_NOT_FOUND, valueHandle);
  }
  if (!hasPermission(cccd->permissions, false)) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::WRITE_NOT_PERMITTED, cccd->handle);
  }
  if (requiresEncryption(cccd->permissions, false) && !state->security.encrypted) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_ENCRYPTION, cccd->handle);
  }
  if (requiresAuthentication(cccd->permissions, false) && !state->security.authenticated) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_AUTHENTICATION, cccd->handle);
  }
  const uint8_t existing = state->subscriptions[valueHandle];
  const uint8_t requested = static_cast<uint8_t>(existing | (indication ? 2U : 1U));
  const std::vector<uint8_t> value = {requested, 0};
  const OperationResult transaction = write(adapterId, cccd->handle, value, true);
  if (!transaction.ok()) {
    return result(Operation::SUBSCRIBE, adapterId, state->generation, transaction.result,
                  transaction.attStatus, cccd->handle, value);
  }
  (void)service;
  return result(Operation::SUBSCRIBE, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                cccd->handle, value);
}

OperationResult VirtualBleRuntime::confirm(uint32_t adapterId, uint16_t valueHandle) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::CONFIRM, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, valueHandle);
  }
  FaultOverlay *fault =
      faultFor(FaultLayer::ATT, Operation::CONFIRM, valueHandle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::CONFIRM, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status,
                  valueHandle);
  }
  if (!state->pendingIndications[valueHandle]) {
    return result(Operation::CONFIRM, adapterId, state->generation, Result::INVALID_STATE,
                  AttStatus::UNLIKELY_ERROR, valueHandle);
  }
  state->pendingIndications[valueHandle] = false;
  state->indicationDeadlines.erase(valueHandle);
  Link confirmLink(state);
  state->peer->onConfirm(confirmLink, valueHandle);
  return result(Operation::CONFIRM, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                valueHandle);
}

OperationResult VirtualBleRuntime::notify(uint32_t adapterId,
                                          uint16_t valueHandle,
                                          const std::vector<uint8_t> &value) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::NOTIFY, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, valueHandle, value);
  }
  FaultOverlay *fault =
      faultFor(FaultLayer::PEER, Operation::NOTIFY, valueHandle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::NOTIFY, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status,
                  valueHandle, value);
  }
  const auto subscription = state->subscriptions.find(valueHandle);
  if (subscription == state->subscriptions.end() || (subscription->second & 0x01U) == 0) {
    return result(Operation::NOTIFY, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::REQUEST_NOT_SUPPORTED, valueHandle, value);
  }
  if (value.size() > static_cast<size_t>(state->mtu - 3)) {
    return result(Operation::NOTIFY, adapterId, state->generation, Result::ATT_ERROR,
                  AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, valueHandle, value);
  }
  return result(Operation::NOTIFY, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                valueHandle, value);
}

OperationResult VirtualBleRuntime::indicate(uint32_t adapterId,
                                            uint16_t valueHandle,
                                            const std::vector<uint8_t> &value) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::INDICATE, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, valueHandle, value);
  }
  FaultOverlay *fault =
      faultFor(FaultLayer::PEER, Operation::INDICATE, valueHandle, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::INDICATE, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status,
                  valueHandle, value);
  }
  const auto subscription = state->subscriptions.find(valueHandle);
  if (subscription == state->subscriptions.end() || (subscription->second & 0x02U) == 0) {
    return result(Operation::INDICATE, adapterId, state->generation, Result::NOT_PERMITTED,
                  AttStatus::REQUEST_NOT_SUPPORTED, valueHandle, value);
  }
  if (state->pendingIndications[valueHandle]) {
    return result(Operation::INDICATE, adapterId, state->generation, Result::INVALID_STATE,
                  AttStatus::PROCEDURE_ALREADY_IN_PROGRESS, valueHandle, value);
  }
  if (value.size() > static_cast<size_t>(state->mtu - 3)) {
    return result(Operation::INDICATE, adapterId, state->generation, Result::ATT_ERROR,
                  AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, valueHandle, value);
  }
  state->pendingIndications[valueHandle] = true;
  state->indicationDeadlines[valueHandle] =
      m_State->nowMs + static_cast<uint64_t>(state->connectionParams.supervisionTimeout) * 10U;
  OperationResult operationResult =
      result(Operation::INDICATE, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
             valueHandle, value, PhysicalOutcome::UNKNOWN, "confirmation required");
  m_State->trace.back().indication = true;
  return operationResult;
}

OperationResult VirtualBleRuntime::secure(uint32_t adapterId, bool authenticated, uint8_t keySize) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::SECURITY, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  FaultOverlay *fault = faultFor(FaultLayer::SMP, Operation::SECURITY, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::SECURITY, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (keySize < 7 || keySize > 16) {
    state->security.smp = SmpState::FAILED;
    return result(Operation::SECURITY, adapterId, state->generation, Result::AUTHENTICATION_FAILED,
                  AttStatus::INSUFFICIENT_KEY_SIZE, 0);
  }
  state->security.smp = SmpState::PAIRING;
  state->security.encrypted = true;
  state->security.authenticated = authenticated;
  state->security.keySize = keySize;
  if (state->identity.hasIdentity) {
    state->security.bonded = true;
    m_State->bonds.save({state->identity.identity, keySize, authenticated, state->identity.irk,
                         state->identity.hasIrk});
  }
  state->security.smp = SmpState::ENCRYPTED;
  return result(Operation::SECURITY, adapterId, state->generation, Result::OK, AttStatus::SUCCESS,
                0);
}

OperationResult VirtualBleRuntime::registerApplication(uint32_t adapterId,
                                                       const std::string &application,
                                                       const std::vector<uint8_t> &token) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::REGISTRATION, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  FaultOverlay *fault = faultFor(FaultLayer::PEER, Operation::REGISTRATION, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::REGISTRATION, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (!state->security.encrypted) {
    return result(Operation::REGISTRATION, adapterId, state->generation, Result::SECURITY_REQUIRED,
                  AttStatus::INSUFFICIENT_ENCRYPTION, 0);
  }
  if (!state->identity.hasIdentity || application.empty()
      || !m_State->registrations.save({state->identity.identity, application, token})) {
    return result(Operation::REGISTRATION, adapterId, state->generation, Result::INVALID_ARGUMENT,
                  AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, 0);
  }
  state->security.applicationRegistered = application == "furble";
  return result(Operation::REGISTRATION, adapterId, state->generation, Result::OK,
                AttStatus::SUCCESS, 0, token);
}

OperationResult VirtualBleRuntime::updateConnectionParams(uint32_t adapterId,
                                                          const ConnectionParams &params) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::CONNECTION_PARAMS, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  if (!params.valid()) {
    return result(Operation::CONNECTION_PARAMS, adapterId, state->generation,
                  Result::INVALID_ARGUMENT, AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, 0);
  }
  FaultOverlay *fault =
      faultFor(FaultLayer::CONTROLLER, Operation::CONNECTION_PARAMS, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::CONNECTION_PARAMS, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (!state->profile.supportsConnectionParams) {
    return unsupported(Operation::CONNECTION_PARAMS, adapterId, state->generation,
                       "peer does not support connection parameter updates");
  }
  m_State->pendingController.push_back({m_State->nowMs + 1, m_State->nextEventSequence++, adapterId,
                                        state->generation, Operation::CONNECTION_PARAMS, params, 0,
                                        state->phy});
  return result(Operation::CONNECTION_PARAMS, adapterId, state->generation, Result::OK,
                AttStatus::SUCCESS, 0, {}, PhysicalOutcome::UNKNOWN, "controller update queued");
}

OperationResult VirtualBleRuntime::exchangeMtu(uint32_t adapterId, uint16_t mtu) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::MTU, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  if (mtu < 23 || mtu > 517) {
    return result(Operation::MTU, adapterId, state->generation, Result::INVALID_ARGUMENT,
                  AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, 0);
  }
  FaultOverlay *fault = faultFor(FaultLayer::CONTROLLER, Operation::MTU, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::MTU, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (!state->profile.supportsMtuExchange) {
    return unsupported(Operation::MTU, adapterId, state->generation,
                       "peer does not support MTU exchange");
  }
  const uint16_t negotiated = std::min(mtu, state->profile.preferredMtu);
  m_State->pendingController.push_back({m_State->nowMs + 1,
                                        m_State->nextEventSequence++,
                                        adapterId,
                                        state->generation,
                                        Operation::MTU,
                                        {},
                                        negotiated,
                                        state->phy});
  return result(Operation::MTU, adapterId, state->generation, Result::OK, AttStatus::SUCCESS, 0,
                {static_cast<uint8_t>(negotiated & 0xff), static_cast<uint8_t>(negotiated >> 8)},
                PhysicalOutcome::UNKNOWN, "controller exchange queued");
}

OperationResult VirtualBleRuntime::updatePhy(uint32_t adapterId, Phy phy) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::PHY, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  if (!validPhy(phy)) {
    return result(Operation::PHY, adapterId, state->generation, Result::INVALID_ARGUMENT,
                  AttStatus::INVALID_ATTRIBUTE_VALUE_LENGTH, 0);
  }
  FaultOverlay *fault = faultFor(FaultLayer::CONTROLLER, Operation::PHY, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::PHY, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  if (!state->profile.supportsPhyUpdate) {
    return unsupported(Operation::PHY, adapterId, state->generation,
                       "peer does not support PHY update");
  }
  m_State->pendingController.push_back({m_State->nowMs + 1,
                                        m_State->nextEventSequence++,
                                        adapterId,
                                        state->generation,
                                        Operation::PHY,
                                        {},
                                        state->mtu,
                                        phy});
  return result(Operation::PHY, adapterId, state->generation, Result::OK, AttStatus::SUCCESS, 0,
                {static_cast<uint8_t>(phy)}, PhysicalOutcome::UNKNOWN, "controller update queued");
}

OperationResult VirtualBleRuntime::resolveRpa(uint32_t adapterId, const Address &current) {
  Link::State *state = link(adapterId) == nullptr ? nullptr : link(adapterId)->m_State;
  if (state == nullptr || !state->connected) {
    return result(Operation::RPA, adapterId, state == nullptr ? 0 : state->generation,
                  Result::LINK_DOWN, AttStatus::UNLIKELY_ERROR, 0);
  }
  FaultOverlay *fault = faultFor(FaultLayer::GAP, Operation::RPA, 0, state->generation);
  if (fault != nullptr) {
    if (fault->dropLink) {
      disconnectForFault(state);
    }
    return result(Operation::RPA, adapterId, state->generation,
                  fault->unsupported ? Result::UNSUPPORTED : fault->result,
                  fault->unsupported ? AttStatus::REQUEST_NOT_SUPPORTED : fault->status, 0);
  }
  const BondRecord *bond =
      state->identity.hasIdentity ? m_State->bonds.find(state->identity.identity) : nullptr;
  const bool exactCaptureRpa = !state->identity.hasIrk && current == state->identity.current;
  const bool irkRpa = state->identity.hasIrk && bond != nullptr && bond->hasIrk
                      && rpaMatchesIrk(current, bond->irk);
  if (!state->identity.hasIdentity || current.type != AddressType::RANDOM_PRIVATE_RESOLVABLE
      || (!exactCaptureRpa && !irkRpa)) {
    return result(Operation::RPA, adapterId, state->generation, Result::NOT_FOUND,
                  AttStatus::INSUFFICIENT_AUTHENTICATION, 0);
  }
  state->address = current;
  return result(Operation::RPA, adapterId, state->generation, Result::OK, AttStatus::SUCCESS, 0, {},
                PhysicalOutcome::UNKNOWN, "resolved to identity");
}

bool VirtualBleRuntime::addFault(const FaultOverlay &fault) {
  if (fault.remaining == 0 || !faultValid(fault)) {
    return false;
  }
  m_State->faults.push_back(fault);
  return true;
}

bool VirtualBleRuntime::faultValid(const FaultOverlay &fault) const {
  if (!validOperationLayer(fault.layer, fault.operation)) {
    return false;
  }
  if (fault.generation == 0 && fault.operation != Operation::ADVERTISE
      && fault.operation != Operation::CONNECT) {
    return false;
  }
  if (fault.layer != FaultLayer::PHYSICAL && fault.operation == Operation::PHYSICAL_OUTCOME) {
    return false;
  }
  if (fault.dropLink && fault.layer == FaultLayer::ATT && fault.operation == Operation::CONFIRM) {
    return false;
  }
  return true;
}

void VirtualBleRuntime::clearFaults() {
  m_State->faults.clear();
}

const std::vector<TraceEvent> &VirtualBleRuntime::trace() const {
  return m_State->trace;
}

std::vector<TraceEvent> VirtualBleRuntime::takeTrace() {
  std::vector<TraceEvent> trace = std::move(m_State->trace);
  m_State->trace.clear();
  return trace;
}

const BondStore &VirtualBleRuntime::bonds() const {
  return m_State->bonds;
}

BondStore &VirtualBleRuntime::bonds() {
  return m_State->bonds;
}

const RegistrationStore &VirtualBleRuntime::registrations() const {
  return m_State->registrations;
}

RegistrationStore &VirtualBleRuntime::registrations() {
  return m_State->registrations;
}

size_t VirtualBleRuntime::liveLinks() const {
  size_t count = 0;
  for (const auto &entry : m_State->links) {
    count += entry.second->connected ? 1U : 0U;
  }
  return count;
}

size_t VirtualBleRuntime::retiredIdentityCount() const {
  return m_State->retiredIdentities.size();
}

bool VirtualBleRuntime::certificationEligible() const {
  return false;
}

const Link *VirtualBleRuntime::link(uint32_t adapterId) const {
  const auto found = m_State->links.find(adapterId);
  const auto view = m_State->views.find(adapterId);
  return found == m_State->links.end() || view == m_State->views.end() ? nullptr
                                                                       : view->second.get();
}

Link *VirtualBleRuntime::link(uint32_t adapterId) {
  const auto found = m_State->links.find(adapterId);
  const auto view = m_State->views.find(adapterId);
  return found == m_State->links.end() || view == m_State->views.end() ? nullptr
                                                                       : view->second.get();
}

SyntheticConformancePeer::SyntheticConformancePeer(PeerProfile profile)
    : m_Profile(std::move(profile)) {}

const PeerProfile &SyntheticConformancePeer::profile() const {
  return m_Profile;
}

void SyntheticConformancePeer::onConnected(Link &) {
  ++m_LiveLinks;
}

OperationResult SyntheticConformancePeer::onWrite(Link &,
                                                  uint16_t handle,
                                                  const std::vector<uint8_t> &value,
                                                  bool) {
  const auto found = m_WriteResults.find(handle);
  if (found != m_WriteResults.end()) {
    return found->second;
  }
  OperationResult result;
  result.result = Result::OK;
  result.attStatus = AttStatus::SUCCESS;
  result.value = value;
  const auto physical = m_PhysicalOutcomes.find(handle);
  result.physicalOutcome =
      physical == m_PhysicalOutcomes.end() ? PhysicalOutcome::UNKNOWN : physical->second;
  return result;
}

OperationResult SyntheticConformancePeer::onRead(Link &, uint16_t handle) {
  const auto found = m_ReadResults.find(handle);
  if (found != m_ReadResults.end()) {
    return found->second;
  }
  OperationResult result;
  result.result = Result::OK;
  result.attStatus = AttStatus::SUCCESS;
  for (const Service &service : m_Profile.services) {
    for (const Characteristic &characteristic : service.characteristics) {
      if (characteristic.valueHandle == handle) {
        result.value = characteristic.value;
        return result;
      }
      for (const Descriptor &descriptor : characteristic.descriptors) {
        if (descriptor.handle == handle) {
          result.value = descriptor.value;
          return result;
        }
      }
    }
  }
  result.result = Result::NOT_FOUND;
  result.attStatus = AttStatus::INVALID_HANDLE;
  return result;
}

void SyntheticConformancePeer::onConfirm(Link &, uint16_t) {
  ++m_Confirmations;
}

void SyntheticConformancePeer::onPhysicalOutcome(Link &, uint16_t handle, PhysicalOutcome outcome) {
  m_LastPhysicalOutcomes[handle] = outcome;
}

void SyntheticConformancePeer::onDisconnect(Link &, uint8_t) {
  ++m_Disconnects;
  if (m_LiveLinks != 0) {
    --m_LiveLinks;
  }
}

void SyntheticConformancePeer::setPhysicalOutcome(uint16_t handle, PhysicalOutcome outcome) {
  m_PhysicalOutcomes[handle] = outcome;
}

void SyntheticConformancePeer::setWriteResult(uint16_t handle, const OperationResult &result) {
  m_WriteResults[handle] = result;
}

void SyntheticConformancePeer::setReadResult(uint16_t handle, const OperationResult &result) {
  m_ReadResults[handle] = result;
}

PhysicalOutcome SyntheticConformancePeer::physicalOutcome(uint16_t handle) const {
  const auto found = m_LastPhysicalOutcomes.find(handle);
  return found == m_LastPhysicalOutcomes.end() ? PhysicalOutcome::UNKNOWN : found->second;
}

size_t SyntheticConformancePeer::confirmations() const {
  return m_Confirmations;
}

size_t SyntheticConformancePeer::disconnects() const {
  return m_Disconnects;
}

bool SyntheticConformancePeer::leakFree() const {
  return m_LiveLinks == 0;
}

}  // namespace Ble
}  // namespace Sim
}  // namespace Furble
