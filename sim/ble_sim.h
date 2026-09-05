#ifndef FURBLE_SIM_BLE_SIM_H
#define FURBLE_SIM_BLE_SIM_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Furble::Sim {

/**
 * Virtual BLE peers behind the production connection stack.
 *
 * The simulator compiles the production Control, Camera, CameraList and Scan
 * sources against MockNimBLE, so there is no simulator connection policy left
 * to configure. What a scenario chooses instead is the radio environment: which
 * virtual cameras exist, what they advertise, and which transport faults they
 * apply. That is what this seam owns.
 *
 * A topology is a named set of peers, validated as a strict allowlist by the
 * scenario parser before any thread starts.
 */

/** Is this a known "seed ble_peers" topology? */
bool bleTopologyIsValid(const std::string &topology);

/**
 * Build a topology and persist its cameras to the simulator preferences.
 *
 * Runs at boot, before the UI exists, so the production CameraList::load()
 * finds saved cameras exactly as it does from NVS on device. Also starts the
 * virtual radio task that advertises the registered peers to the production
 * Scan while a scan is running.
 */
void bleStartPeers(const std::string &topology);

/** Stop the virtual radio and release every peer. */
void bleStopPeers(void);

/** Make the next NimBLEClient::connect() calls fail at the transport. */
void bleSetConnectFail(bool fail);

/**
 * Sever the live link of control target `index` at the transport.
 *
 * index < 0 drops every target. `deliverCallback` selects between the two real
 * link-loss shapes: true delivers the GAP disconnect event, so the camera's
 * onDisconnect clears its connected flag (a supervision timeout the stack has
 * already reported); false leaves the event queued, so the camera keeps
 * reporting connected over a link that is physically gone.
 *
 * Returns false when no target had a live link to drop.
 */
bool bleDropLink(int index, bool deliverCallback);

/**
 * Run the standby drop of the virtual peer backing control target `index`.
 *
 * The peer re-arms its handshake failure budget, announces the power state its
 * protocol announces (Ricoh sends CameraPower 0x00, Fujifilm is silent) and
 * severs the link, which is the autonomous flap observed on hardware. The
 * simulator schedules it from the scenario rather than from the peer's own
 * wall-clock timer so it lands at a known virtual time.
 */
bool blePeerStandbyDrop(int index);

/**
 * Persist every registered peer as a saved camera.
 *
 * Runs the peer advertisements through the production CameraList::match() and
 * saves the resulting cameras, so a scenario boots with saved cameras exactly
 * as a device does after the user scanned and connected once.
 */
void bleSaveRegisteredPeers(void);

/** Count a camera command that reached a camera task (observability only). */
void noteCameraCommand(int cmd);

/**
 * Make every Fujifilm peer withhold its registration confirmation.
 *
 * The camera answers every link and GATT operation but never confirms
 * registration, which is a camera sitting in its own settings screen. The
 * production connect then blocks in its registration wait, so a disconnect
 * arriving in that window has to cancel an in-flight connect: the plan 148
 * teardown wedge, and the shape of the 2026-08-28 multi-target wedge.
 */
bool bleSetWithholdRegistration(bool withhold);

/**
 * Hold every Fujifilm peer inside its security handshake for `ms` virtual ms.
 *
 * `NimBLEClient::secureConnection()` is the one wait inside a Fujifilm Secure
 * connect that takes no cancel token, and `Camera::connect()` holds
 * `Camera::m_Mutex` across it, so an attempt parked there is uncancellable and
 * a target task's `Camera::disconnect()` blocks behind it. The virtual peers
 * answered it in under a millisecond, so no scenario could cancel into a live
 * connect at all. Seeded with `secure_stall_ms`.
 */
void bleSetSecureStallMs(uint32_t ms);

/**
 * Cap the NimBLE client pool the way the board's sdkconfig caps it.
 *
 * `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` is 9 on every furble board. The mock pool
 * is unlimited by default, so a client leaked per connect cycle is invisible
 * here while on the device it ends the session for good: once the pool is out,
 * `NimBLEDevice::createClient()` returns nullptr and every later connect fails
 * until a reboot. Seeded with `ble_max_clients`; 0 keeps the pool unlimited.
 */
void bleSetMaxClients(size_t max);

/**
 * Model NimBLE's self-deleting client the way the controller does.
 *
 * `Camera::connect()` arms `setSelfDelete(true, true)` on a live session, so on
 * the device the NimBLE host task frees the client when onDisconnect fires. The
 * mock keeps that off by default, which means a simulator session leaves its
 * client behind on every clean teardown. That is a mock artifact rather than a
 * firmware leak, but it also means the simulator cannot tell the two apart, so
 * a scenario that walks many connect cycles turns it on. Seeded with
 * `ble_client_selfdelete`.
 */
void bleSetDeferredClientDelete(bool enabled);

/**
 * Did any Fujifilm peer's modelled handshake end on a link terminate rather
 * than on its own deadline?
 *
 * This is the only breaker-proof way to tell an aborted cancel from one that
 * merely outwaited the attempt. Virtual-time bounds cannot: the interactive
 * teardown polls on the UI thread, so how many 20 ms ticks it burns is set by
 * how long the host takes to let the connect task run, which is issue #279.
 */
bool bleSecureStallAborted(void);

/** Number of live NimBLE clients the mock currently holds. */
size_t bleLiveClientCount(void);

/** Number of advertisements the virtual radio has delivered. */
size_t bleAdvertisementCount(void);

/** Number of virtual peers currently registered. */
size_t blePeerCount(void);

}  // namespace Furble::Sim

#endif
