import Foundation

// CoreBluetooth delivers callbacks on the main queue when the client uses the
// default queue. Running the main run loop from an explicitly isolated main
// actor lets the headless CLI service those callbacks without an AppKit app.
let code = MainActor.assumeIsolated {
  CLIEntry.run(
    arguments: Array(CommandLine.arguments.dropFirst()),
    environment: ProcessInfo.processInfo.environment)
}
exit(code)
