set(furble_sources
    FurbleBootScreen.cpp
    FurbleBtDebug.cpp
    FurbleCompanion.cpp
    FurbleCompanionService.cpp
    FurbleConsole.cpp
    FurbleControl.cpp
    FurbleEthernet.cpp
    FurbleFeedback.cpp
    FurbleGPS.cpp
    FurbleGPX.cpp
    FurbleIMU.cpp
    FurbleIMUMotionBMI270.cpp
    FurbleIMUMotionMPU6886.cpp
    FurbleIR.cpp
    FurbleOTA.cpp
    FurbleOTAMQTT.cpp
    FurbleOTAPartitionSink.cpp
    FurbleOTAPartitionSinkESP.cpp
    FurbleOTAPartitionSinkArduino.cpp
    FurbleOTAReplayStore.cpp
    FurbleOTAReplayStoreNVS.cpp
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleProvision.cpp
    FurbleSD.cpp
    FurbleSettings.cpp
    FurbleTimeKeeper.cpp
    FurbleTimeKeeperPolicy.cpp
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
       FurbleUIGesture.cpp
       FurbleUIIntervalometer.cpp)
endif()

# console and esp_driver_usb_serial_jtag are only referenced by the developer
# console. Nothing references them without FURBLE_CONSOLE, so the linker leaves
# them out and the release binaries are unchanged.
set(furble_private_requires
    console esp_driver_rmt esp_driver_sdspi esp_driver_usb_serial_jtag esp_pm fatfs sdmmc)
if(FURBLE_ETHERNET)
  list(APPEND furble_private_requires
       esp_driver_gpio esp_driver_spi esp_eth esp_event esp_netif)
endif()

if(FURBLE_NO_DISPLAY)
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_private_requires} esp_hw_support)
else()
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_private_requires} esp_hw_support
                         REQUIRES icons)
endif()
