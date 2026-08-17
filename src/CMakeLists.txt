set(furble_sources
    FurbleCalibrate.cpp
    FurbleCompanion.cpp
    FurbleConsole.cpp
    FurbleControl.cpp
    FurbleGPS.cpp
    FurbleIR.cpp
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleSettings.cpp
    FurbleSpinValue.cpp
    FurbleUI.cpp
    FurbleUIBulb.cpp
    FurbleUIIntervalometer.cpp
    main.cpp)

# console and esp_driver_usb_serial_jtag are only referenced by the developer
# console. Nothing references them without FURBLE_CONSOLE, so the linker leaves
# them out and the release binaries are unchanged.
idf_component_register(SRCS ${furble_sources}
                       PRIV_REQUIRES console esp_driver_rmt esp_driver_usb_serial_jtag esp_pm
                       REQUIRES icons)
