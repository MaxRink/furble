set(furble_sources
    FurbleBootScreen.cpp
    FurbleBtDebug.cpp
    FurbleCompanion.cpp
    FurbleCompanionService.cpp
    FurbleConsole.cpp
    FurbleControl.cpp
    FurbleFeedback.cpp
    FurbleGPS.cpp
    FurbleGPX.cpp
    FurbleIR.cpp
    FurbleMQTT.cpp
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleSD.cpp
    FurbleSettings.cpp
    main.cpp)

# PlatformIO passes the displayless profile as a CMake cache variable. It is
# visible here in the main configure pass, so the display-only sources can be
# gated, but NOT in the early requirements expansion pass, so REQUIRES below
# must stay unconditional. The icons component empties itself on
# CONFIG_FURBLE_NO_DISPLAY instead, and the linker drops unreferenced LVGL.
if(NOT FURBLE_NO_DISPLAY)
  list(APPEND furble_sources
       FurbleCalibrate.cpp
       FurbleSpinValue.cpp
       FurbleUI.cpp
       FurbleUIAudit.cpp
       FurbleUIBulb.cpp
       FurbleUIIntervalometer.cpp)
endif()

# console and esp_driver_usb_serial_jtag are only referenced by the developer
# console. Nothing references them without FURBLE_CONSOLE, so the linker leaves
# them out and the release binaries are unchanged.
idf_component_register(SRCS ${furble_sources}
                       PRIV_REQUIRES console esp_driver_rmt esp_driver_usb_serial_jtag esp_pm
                                     mqtt json esp_netif
                       REQUIRES icons)
