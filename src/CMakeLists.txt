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
    FurbleOTA.cpp
    FurbleOTAMQTT.cpp
    FurbleOTAReplayStore.cpp
    FurbleOTAReplayStoreNVS.cpp
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleProvision.cpp
    FurbleSD.cpp
    FurbleSettings.cpp
    ../lib/furble/protocol/ProvisionTLV.cpp
    main.cpp)

# PlatformIO passes this profile through both a compiler define and a CMake
# argument. Keep the gate in the build system so a headless image never
# resolves LVGL or the icon component.
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
if(FURBLE_NO_DISPLAY)
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES console esp_driver_rmt esp_driver_sdspi esp_driver_usb_serial_jtag esp_pm fatfs sdmmc)
else()
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES console esp_driver_rmt esp_driver_sdspi esp_driver_usb_serial_jtag esp_pm fatfs sdmmc
                         REQUIRES icons)
endif()
