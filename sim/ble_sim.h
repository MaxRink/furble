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

/** Number of advertisements the virtual radio has delivered. */
size_t bleAdvertisementCount(void);

/** Number of virtual peers currently registered. */
size_t blePeerCount(void);

}  // namespace Furble::Sim

#endif
