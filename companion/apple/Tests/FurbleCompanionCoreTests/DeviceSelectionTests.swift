import Foundation
import Testing
@testable import FurbleCompanionCore

struct DeviceSelectionTests {
  private let identifier = UUID(uuidString: "11111111-2222-3333-4444-555555555555")!

  @Test func anySelectionMatchesEveryPeripheral() {
    #expect(FurbleDeviceSelection.any.matches(name: nil, identifier: identifier))
    #expect(FurbleDeviceSelection.any.matches(name: "Furble-1A2B", identifier: identifier))
  }

  @Test func nameSelectionIgnoresCaseAndDiacritics() {
    let selection = FurbleDeviceSelection.nameContains("furbLe-")
    #expect(selection.matches(name: "FURBLE-1A2B", identifier: identifier))
    #expect(!selection.matches(name: "Other-1A2B", identifier: identifier))
    #expect(!selection.matches(name: nil, identifier: identifier))
    #expect(!FurbleDeviceSelection.nameContains("").matches(
      name: "Furble-1A2B", identifier: identifier))
    let accented = FurbleDeviceSelection.nameContains("cafe")
    #expect(accented.matches(name: "Café-1", identifier: identifier))
  }

  @Test func identifierSelectionUsesTheMacPeripheralUUID() {
    let selection = FurbleDeviceSelection.identifier(identifier)
    #expect(selection.matches(name: "renamed", identifier: identifier))
    #expect(!selection.matches(
      name: "Furble-1A2B", identifier: UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!))
  }

  @Test func discoveredDeviceUsesANameFallback() {
    let unnamed = FurbleDiscoveredDevice(id: identifier, name: nil, rssi: -60)
    #expect(unnamed.displayName == "unnamed furble")
    let empty = FurbleDiscoveredDevice(id: identifier, name: "", rssi: -60)
    #expect(empty.displayName == "unnamed furble")
    let named = FurbleDiscoveredDevice(id: identifier, name: "Furble-1A2B", rssi: -60)
    #expect(named.displayName == "Furble-1A2B")
    #expect(named.id == identifier)
  }

  @Test func selectionCanMatchADiscoveredValue() {
    let device = FurbleDiscoveredDevice(id: identifier, name: "Furble-1A2B", rssi: -60)
    #expect(FurbleDeviceSelection.nameContains("1a2b").matches(device))
    #expect(FurbleDeviceSelection.identifier(identifier).matches(device))
  }
}
