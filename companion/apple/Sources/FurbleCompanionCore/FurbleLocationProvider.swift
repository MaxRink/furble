import Foundation

#if canImport(CoreLocation)
import CoreLocation
import Combine

/// Location is off until the user explicitly enables it. The provider emits
/// the same UTC fields and age semantics as the Android client.
///
/// While the toggle is on, updates continue in the background. On iOS this
/// requires the `location` background mode in the app Info.plist and shows the
/// system location indicator. When In Use authorization is enough for a
/// user-started session; Always is not required.
@MainActor
public final class FurbleLocationProvider: NSObject, ObservableObject, @preconcurrency CLLocationManagerDelegate {
  @Published public private(set) var enabled = false
  @Published public private(set) var authorization: CLAuthorizationStatus
  private let manager = CLLocationManager()
  private var sink: ((FurbleProtocol.LocationFix) -> Void)?
  private var wantsUpdates = false

  public override init() {
    authorization = manager.authorizationStatus
    super.init()
    manager.delegate = self
    manager.desiredAccuracy = kCLLocationAccuracyNearestTenMeters
    manager.distanceFilter = kCLDistanceFilterNone
    manager.pausesLocationUpdatesAutomatically = true
  }

  public func start(sink: @escaping (FurbleProtocol.LocationFix) -> Void) {
    self.sink = sink
    wantsUpdates = true
    guard CLLocationManager.locationServicesEnabled() else { return }
    if manager.authorizationStatus == .notDetermined {
      #if os(macOS)
      manager.requestAlwaysAuthorization()
      #else
      manager.requestWhenInUseAuthorization()
      #endif
    }
    guard isAuthorized(manager.authorizationStatus) else { return }
    enableTrackingLocation()
    enabled = true
    manager.startUpdatingLocation()
  }

  public func stop() {
    wantsUpdates = false
    enabled = false
    disableTrackingLocation()
    manager.stopUpdatingLocation()
    sink = nil
  }

  /// Keep fixes flowing while the camera session is active. iOS pauses
  /// stationary updates by default, which leaves the device without a fresh
  /// fix and makes it fall back to no fix.
  private func enableTrackingLocation() {
    manager.pausesLocationUpdatesAutomatically = false
    #if os(iOS)
    manager.allowsBackgroundLocationUpdates = true
    manager.showsBackgroundLocationIndicator = true
    #endif
  }

  private func disableTrackingLocation() {
    manager.pausesLocationUpdatesAutomatically = true
    #if os(iOS)
    manager.allowsBackgroundLocationUpdates = false
    manager.showsBackgroundLocationIndicator = false
    #endif
  }

  public func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
    authorization = manager.authorizationStatus
    guard wantsUpdates, CLLocationManager.locationServicesEnabled(),
      isAuthorized(manager.authorizationStatus) else { return }
    enableTrackingLocation()
    enabled = true
    manager.startUpdatingLocation()
  }

  public func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
    guard let location = locations.last, location.horizontalAccuracy >= 0 else { return }
    let components = Calendar(identifier: .gregorian).dateComponents(
      in: TimeZone(secondsFromGMT: 0) ?? .current, from: location.timestamp)
    guard let year = components.year, let month = components.month, let day = components.day,
      let hour = components.hour, let minute = components.minute, let second = components.second else { return }
    let age = max(0, Int(Date().timeIntervalSince(location.timestamp) * 1000))
    let accuracy = min(254, max(0, Int(location.horizontalAccuracy)))
    let altitudeValid = location.verticalAccuracy >= 0 && location.altitude.isFinite
    let fix = FurbleProtocol.LocationFix(
      positionValid: location.coordinate.latitude.isFinite && location.coordinate.longitude.isFinite,
      timeValid: true,
      altitudeValid: altitudeValid,
      satellites: 0,
      accuracyMeters: UInt8(accuracy),
      latitude: location.coordinate.latitude,
      longitude: location.coordinate.longitude,
      altitude: altitudeValid ? location.altitude : 0,
      year: UInt16(clamping: year), month: UInt8(clamping: month), day: UInt8(clamping: day),
      hour: UInt8(clamping: hour), minute: UInt8(clamping: minute), second: UInt8(clamping: second),
      centisecond: UInt8(clamping: Calendar.current.component(.nanosecond, from: location.timestamp) / 10_000_000),
      ageMilliseconds: UInt32(clamping: age))
    sink?(fix)
  }

  private func isAuthorized(_ status: CLAuthorizationStatus) -> Bool {
    #if os(macOS)
    return status == .authorizedAlways
    #else
    return status == .authorizedWhenInUse || status == .authorizedAlways
    #endif
  }
}
#endif
