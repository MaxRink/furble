#ifndef FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H
#define FURBLE_HOST_FUJIFILM_VIRTUAL_CAMERA_H

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "MockNimBLE.h"
#include "PeerStall.h"

namespace Furble {
namespace Host {

/**
 * A small Fujifilm Basic peer for host protocol tests.
 *
 * It models the server side of the unencrypted Fujifilm GATT flow. The class
 * has no radio implementation. NimBLEMockPeer supplies the client transport,
 * so the same event sequence can later be driven by a real peer adapter.
 */
class FujifilmVirtualCamera final: public NimBLEMockPeer {
 public:
  struct Config {
    std::string name = "FUJIFILM X100VI";
    NimBLEAddress address = NimBLEAddress(0x112233445566ULL, 0);
    std::array<uint8_t, 4> token = {0xa1, 0xb2, 0xc3, 0xd4};
    bool secure = false;
    // Bench capture, 2026-09-02: an X100VI in pairing mode advertises the bare
    // local name "X100VI" and a five byte serial of 31 43 34 46 39, which is
    // the ASCII text "1C4F9". The console lines are quoted in
    // plans/167-fujifilm-device-name.md. The peer carries those bytes so the
    // host tests derive the displayed name from a realistic advertisement
    // rather than an arbitrary one.
    std::array<uint8_t, 5> serial = {'1', 'C', '4', 'F', '9'};
    std::vector<NimBLEUUID> advertised_services;
  };

  struct Write {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
    bool response = false;
  };

  struct Notification {
    std::string service;
    std::string characteristic;
    std::vector<uint8_t> payload;
    bool indication = false;
  };

  FujifilmVirtualCamera();
  explicit FujifilmVirtualCamera(const Config &config);
  ~FujifilmVirtualCamera() override;

  NimBLEAdvertisedDevice advertisement() const;

  /** Ask the client for a geotag update using the camera's normal notification. */
  bool requestGeotag();

  /** Replay a notification from a normalized capture. */
  bool emitNotification(const NimBLEUUID &service,
                        const NimBLEUUID &characteristic,
                        const std::vector<uint8_t> &payload,
                        bool indication = false);

  // Model a stale-session reconnect. When enabled the camera still holds the
  // CCCD subscriptions from the previous session, so an acknowledged optional
  // CCCD write (response = true) never gets its ATT write response and, on real
  // hardware, blocks the connect. Required Secure configuration indications
  // remain acknowledged because a normal X100VI requires those responses. The
  // mock returns false for stale optional writes to stand in for the block.
  void setStaleSubscribeSession(bool stale);

  // Withhold the registration-accepted notification while still answering all
  // link and GATT operations. This models a camera in its settings screen and
  // is used to prove that a link-only connect cannot become active.
  void setWithholdRegistration(bool withhold);

  /** Use a custom CHR_NOT1 payload for a registration callback test. */
  void setRegistrationPayload(const std::vector<uint8_t> &payload);

  // Invoke the notification callback retained from the previous BLE session.
  // The production callback carries a session generation and must reject it.
  bool emitStaleRegistration(void);

  // Fault injection for adversarial connect and command error paths.
  //
  // suppressService models a camera that does not expose a GATT service at all,
  // so m_Client->getService() returns nullptr. This stands in for an out-of-spec
  // or firmware-variant peer, or a partial GATT discovery. A vendor connect that
  // only logs a null service and then dereferences it crashes here.
  //
  // suppressCharacteristic models a service that is present but missing one
  // characteristic, so getCharacteristic() returns nullptr. A vendor connect
  // that tolerates the null instead of failing reports a connected camera whose
  // command path is silently dead.
  //
  // failWrite models an ATT write that the peer rejects (returns an error
  // status). A handshake write that fails must abort the connect and reclaim the
  // client without leaking it from the fixed-size pool.
  void suppressService(const NimBLEUUID &service);
  void suppressCharacteristic(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void failWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // FlappyPeer: an autonomous standby camera model (the GR IV class observed on
  // hardware 2026-08-28, mapped onto the Fujifilm handshake). The peer accepts
  // every BLE connect, fails the pairing handshake write for fail_attempts
  // attempts, then completes one handshake and severs the link drop_after_ms
  // later on its own timer (the time-compressed ~20 s standby drop; the
  // Fujifilm protocol has no power notification, so the drop is silent). After
  // the drop the failure budget re-arms, so a reconnect loop churns against
  // the peer without any per-attempt scripting from the test. setFlappy(0, 0)
  // disables the mode and joins the drop timer; a peer with the mode enabled
  // must be disabled or destroyed before NimBLEDevice::resetMock() frees the
  // client its timer may still reference.
  void setFlappy(uint32_t fail_attempts, uint32_t drop_after_ms);

  // Run the standby drop body directly, without the wall-clock timer. The
  // simulator runs on virtual time, where a thread sleeping on the host clock
  // would neither fire at the modelled moment nor stay deterministic, so it
  // schedules the drop itself and calls this. Returns false when no link is up.
  bool triggerStandbyDrop();

  // dropLinkOnWrite models a supervision-timeout link loss that lands in the
  // middle of the connect handshake: when the central writes the named
  // characteristic the peer severs the link and delivers onDisconnect inline,
  // then reports the write as failed. The half-finished _connect must unwind
  // cleanly, leave the camera disconnected, and reclaim the client rather than
  // leaking it or dereferencing a torn-down link.
  void dropLinkOnWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // dropLinkDuringConnect models the peer resetting (a power-cycle) in the middle
  // of the connect handshake. When the central writes the named characteristic
  // the peer completes the write normally, then severs the link with an inline
  // self-deleting drop (NimBLEClient::mockDropLinkSelfDelete): onDisconnect fires
  // and, if the client is armed for delete-on-disconnect, the client is freed
  // right there, exactly as the NimBLE host task frees a setSelfDelete client.
  // Unlike dropLinkOnWrite the write still reports success, so _connect() keeps
  // going and performs its next m_Client dereference. If the connect path does
  // not own the client lifetime for the whole handshake, that dereference lands
  // on the freed client, which is the mid-connect use-after-free this guards.
  void dropLinkDuringConnect(const NimBLEUUID &service, const NimBLEUUID &characteristic);

  // faultNextOperation installs a one-shot fault that runs inside the next
  // read() or write(), after the operation result is computed and immediately
  // before it returns to the NimBLE transport. It models a concurrent event,
  // such as the reconnect path's service rediscovery, landing while the GATT
  // operation is still in flight. The fault may free the remote service and
  // characteristic objects (NimBLEClient::dropServiceCache), so the peer
  // touches nothing after running it. Do not combine with the link-drop
  // faults above; those already own the end-of-operation window.
  void faultNextOperation(std::function<void(NimBLEClient &)> fault);

  // Clear every injected fault (suppressed services and characteristics, failed
  // writes, mid-handshake drops and the stale-subscribe flag). The fuzz harness
  // reuses one persistent peer across many lifecycle operations and calls this to
  // return the peer to a healthy baseline between operations.
  void clearFaults();

  const Config &config() const;
  const std::vector<Write> &writes() const;
  const std::vector<Notification> &notifications() const;
  const std::vector<uint8_t> &lastGeotag() const;
  const std::string &identifier() const;
  bool connected() const;
  void setSecureConnectionResult(bool result);
  // Model the stale-bond secureConnection() block observed on hardware.
  //
  // NimBLE's secureConnection() is a blocking call with its own internal
  // timeout, not a poll loop, so nothing inside Camera::connect() can shorten
  // it and the plan 148 cancel token cannot reach it. On an X100VI whose bond
  // the camera has deleted, it blocks for the full pairing timeout. Setting a
  // stall here reproduces that: the control task is parked inside the attempt
  // holding Camera::m_Mutex exactly as it is on the device.
  //
  // The wait is a condition variable rather than a sleep, because the block is
  // only half the behaviour. NimBLE returns from a parked secureConnection()
  // when the link is terminated under it, which is the whole reason
  // Camera::abortBlockingConnect() issues that terminate. A sleep would model
  // the wedge but not the escape, and a test built on it could only ever prove
  // that the stall expired on its own. The peer's own disconnect() releases the
  // wait and the call then returns false, the verdict NimBLE gives when the
  // link dies under the handshake. 0 disables the stall.
  //
  // The wait runs on the clock PeerStall.h installs. The host harness keeps the
  // wall clock, where a real millisecond is the point. The simulator runs a
  // virtual clock the host clock knows nothing about, so a wall-clock park
  // there would neither land at the modelled moment nor be deterministic; it
  // installs a virtual-time delay instead and the wait polls the terminate
  // between slices rather than sleeping on the condition variable. Same
  // semantics either way: expire on the deadline, or wake early on the
  // terminate and report the abort.
  void setSecureConnectionStallMs(uint32_t stallMs);
  // Did a stall end because the link was terminated rather than by its own
  // deadline? This is the difference between an abort that works and a test
  // that merely outwaited the block.
  bool secureStallWasAborted() const;
  // How many times the handshake has been entered, so a repeated-cycle test can
  // prove every cycle really reached the blocking call.
  uint32_t secureStallEntries() const;
  // Model a standby camera whose encryption handshake dies with the link (the
  // Ricoh rc=520 shape, plans/147): secureConnection fails only after the drop
  // has cleared the link state. Distinct from setSecureConnectionResult(false),
  // which models a camera that refuses the encryption but stays on the link,
  // the definitive stale-bond signature from the PR #93 X100VI trace.
  void setSecureConnectionDropsLink(bool drop);
  // Model the X100VI stale-bond signature captured on hardware 2026-09-02
  // (bench-logs/stale-bond-245-run2). After the pairing is deleted on the
  // camera only, the link comes up and the encryption handshake then times out
  // and takes the link with it, attempt after attempt, with no refusal ever
  // arriving. `attempts` is how many consecutive secure handshakes time out
  // before the camera accepts one again; kSecureTimeoutAlways never accepts,
  // and 0 disables the mode. The rc=520 variant, where the failure reaches the
  // caller before the disconnect event is delivered, is covered by wrapping
  // this peer in SecureTimeoutPeer instead.
  void setSecureTimeouts(uint32_t attempts);
  static constexpr uint32_t kSecureTimeoutAlways = UINT32_MAX;
  // Model a camera that deleted its pairing but is sitting in pairing mode: it
  // refuses the encryption while a bond exists (dead keys) and accepts a fresh
  // pairing once the stale bond is gone, all on the same link. This is the
  // in-link recovery the stale-bond path attempts before giving up.
  void setRefuseWhileBonded(bool refuse);

  // Model the stale-bond secureConnection() block observed on hardware.
  //
  // NimBLE's secureConnection() is a blocking call with its own internal
  // timeout, not a poll loop, so nothing inside Camera::connect() can shorten
  // it and the plan 148 cancel token cannot reach it. On an X100VI whose bond
  // the camera has deleted, it blocks for the full pairing timeout. Setting a
  // stall here reproduces that: the control task is parked inside the attempt
  // holding Camera::m_Mutex exactly as it is on the device.
  //
  // The wait is a condition variable rather than a sleep, because the block is
  // only half the behaviour. NimBLE returns from a parked secureConnection()
  // when the link is terminated under it, which is the whole reason
  // Camera::abortBlockingConnect() issues that terminate. A sleep would model
  // the wedge but not the escape, and a test built on it could only ever prove
  // that the stall expired on its own. The peer's own disconnect() releases the
  // wait and the call then returns false, the verdict NimBLE gives when the
  // link dies under the handshake. 0 disables the stall.
  void setSecureConnectionStallMs(uint32_t stallMs);
  // Did a stall end because the link was terminated rather than by its own
  // deadline? This is the difference between an abort that works and a test
  // that merely outwaited the block.
  bool secureStallWasAborted() const;
  // How many times the handshake has been entered, so a repeated-cycle test can
  // prove every cycle really reached the blocking call.
  uint32_t secureStallEntries() const;
  void setRequireLongConnParamsAfterIdentifier(bool require);
  void setDelayRegistrationConnParamsUntilFastRequest(bool delay);
  void dropLinkOnSubscribe(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  void requestConnParamsDuringConnect(const ble_gap_upd_params &params);
  // Model a Secure camera that sends its required registration parameters
  // while CCCD discovery is still in progress.  Real cameras may defer this
  // request until the first indication subscription.
  void requestConnParamsOnSubscribe(const NimBLEUUID &service,
                                    const NimBLEUUID &characteristic,
                                    const ble_gap_upd_params &params);
  // Some Secure cameras drop the link if the central switches away from the
  // registration profile before shutter-service discovery is complete.
  void setRejectFastBeforeShutterDiscovery(bool reject);
  bool registrationConnParamsAccepted() const;
  bool tokenAccepted() const;
  bool configured() const;
  bool geotagRequested() const;
  size_t accessAfterDrop() const;
  bool subscriptionRequestedWithResponse(const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic) const;

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

  static const NimBLEUUID &pairServiceUUID();
  static const NimBLEUUID &pairCharacteristicUUID();
  static const NimBLEUUID &identifierCharacteristicUUID();
  static const NimBLEUUID &configurationServiceUUID();
  static const NimBLEUUID &configurationNotificationUUID();
  static const NimBLEUUID &geotagRequestCharacteristicUUID();
  static const NimBLEUUID &configurationIndication1UUID();
  static const NimBLEUUID &configurationIndication2UUID();
  static const NimBLEUUID &configurationIndication3UUID();
  static const NimBLEUUID &shutterServiceUUID();
  static const NimBLEUUID &shutterCharacteristicUUID();
  static const NimBLEUUID &geotagServiceUUID();
  static const NimBLEUUID &geotagCharacteristicUUID();
  static const NimBLEUUID &advertisedServiceUUID();

 private:
  struct Subscription {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *remote = nullptr;
    NimBLENotifyCallback callback;
    bool notification = false;
    bool response = false;
  };

  // The stall wait, on whichever clock PeerStall.h has installed. Called with
  // m_StallMutex held; may release and retake it.
  bool waitForStallLocked(std::unique_lock<std::mutex> &lock, uint32_t stallMs);

  bool isServiceSuppressed(const NimBLEUUID &service) const;
  bool isPairHandshakeWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool flappyConsumeHandshakeFailure();
  void armFlappyDrop(NimBLEClient &client);
  void requestFlappyCancel();
  void cancelFlappyTimer();
  bool isCharacteristicSuppressed(const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic) const;
  bool isWriteFailed(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropOnWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropDuringConnect(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  bool isDropOnSubscribe(const NimBLEUUID &service, const NimBLEUUID &characteristic) const;
  void runOperationFault(NimBLEClient &client);

  Config m_Config;
  NimBLEClient *m_Client = nullptr;
  bool m_Connected = false;
  bool m_SecureConnectionResult = true;
  // Guards the stall handshake. Held only around the wait and the wake, and
  // nests no other lock: the wake runs on the cancelling thread inside
  // NimBLEClient::disconnect() while the connect thread waits here.
  mutable std::mutex m_StallMutex;
  std::condition_variable m_StallSignal;
  uint32_t m_SecureConnectionStallMs = 0;
  bool m_StallLinkDown = false;
  bool m_StallAborted = false;
  uint32_t m_StallEntries = 0;
  bool m_SecureConnectionDropsLink = false;
  uint32_t m_SecureTimeoutsRemaining = 0;
  bool m_RefuseWhileBonded = false;
  // Guards the stall handshake. Held only around the wait and the wake, and
  // nests no other lock: the wake runs on the cancelling thread inside
  // NimBLEClient::disconnect() while the connect thread waits here.
  mutable std::mutex m_StallMutex;
  std::condition_variable m_StallSignal;
  uint32_t m_SecureConnectionStallMs = 0;
  bool m_StallLinkDown = false;
  bool m_StallAborted = false;
  uint32_t m_StallEntries = 0;
  bool m_RequireLongConnParamsAfterIdentifier = false;
  bool m_DelayRegistrationConnParamsUntilFastRequest = false;
  bool m_ConnParamsNegotiated = false;
  bool m_RequestConnParamsDuringConnect = false;
  bool m_RequestConnParamsOnSubscribe = false;
  bool m_RejectFastBeforeShutterDiscovery = false;
  bool m_ShutterCharacteristicRequested = false;
  NimBLEUUID m_ConnParamsSubscribeService;
  NimBLEUUID m_ConnParamsSubscribeCharacteristic;
  ble_gap_upd_params m_RegistrationConnParams {};
  bool m_RegistrationConnParamsAccepted = false;
  bool m_StaleSubscribeSession = false;
  bool m_WithholdRegistration = false;
  std::vector<uint8_t> m_RegistrationPayload = {0x01, 0x00};
  bool m_HaveStaleRegistration = false;
  Subscription m_StaleRegistration;
  std::vector<NimBLEUUID> m_SuppressedServices;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_SuppressedCharacteristics;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_FailedWrites;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropOnWrite;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropDuringConnect;
  std::vector<std::pair<NimBLEUUID, NimBLEUUID>> m_DropOnSubscribe;
  std::function<void(NimBLEClient &)> m_OperationFault;
  mutable size_t m_AccessAfterDrop = 0;
  bool m_DroppedLink = false;
  bool m_TokenAccepted = false;
  bool m_Configured = false;
  bool m_GeotagRequested = false;
  std::string m_Identifier;
  std::vector<Write> m_Writes;
  std::vector<Notification> m_Notifications;
  std::vector<uint8_t> m_LastGeotag;
  std::map<std::string, Subscription> m_Subscriptions;

  // FlappyPeer state. The recursive mutex lets the drop timer re-enter
  // disconnect() through mockDropLink() on its own thread, and lets any other
  // thread cancel the timer: a canceller that loses the race blocks until the
  // in-flight drop finishes, so the timer never touches a client freed by the
  // canceller's teardown.
  bool m_FlappyEnabled = false;
  uint32_t m_FlappyFailAttempts = 0;
  uint32_t m_FlappyFailRemaining = 0;
  uint32_t m_FlappyDropAfterMs = 0;
  bool m_FlappyCancel = false;
  std::recursive_mutex m_FlappyMutex;
  std::condition_variable_any m_FlappyCv;
  std::thread m_FlappyThread;

  static std::string key(const NimBLEUUID &service, const NimBLEUUID &characteristic);
};

}  // namespace Host
}  // namespace Furble

#endif
