set(furble_sources
    FurbleBoard.cpp
    FurbleBootScreen.cpp
    FurbleBootTimeline.cpp
    FurbleBtDebug.cpp
    FurbleCompanion.cpp
    FurbleCompanionAuth.cpp
    FurbleCompanionDebug.cpp
    FurbleCompanionCrypto.cpp
    FurbleCompanionIdentity.cpp
    FurbleCompanionService.cpp
    FurbleConsole.cpp
    FurbleControl.cpp
    FurbleCrashLog.cpp
    FurbleEthernet.cpp
    FurbleFeedback.cpp
    FurbleGPS.cpp
    FurbleGPSUbx.cpp
    FurbleGPX.cpp
    FurbleIMU.cpp
    FurbleIMUMotionBMI270.cpp
    FurbleIMUMotionMPU6886.cpp
    FurbleIR.cpp
    FurbleMemoryPressure.cpp
    FurbleOTA.cpp
    FurbleOTAAdmission.cpp
    FurbleOTACommand.cpp
    FurbleOTAHTTPS.cpp
    FurbleOTAMQTT.cpp
    FurbleOTAPartitionSink.cpp
    FurbleOTAPartitionSinkESP.cpp
    FurbleOTAPartitionSinkArduino.cpp
    FurbleOTAReplayStore.cpp
    FurbleOTAReplayStoreNVS.cpp
    FurbleOTAStatus.cpp
    FurblePinShare.cpp
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleProvision.cpp
    FurbleSD.cpp
    FurbleSettings.cpp
    FurbleNetworkSettings.cpp
    FurbleTimeKeeper.cpp
    FurbleTimeKeeperPolicy.cpp
    FurbleTimezone.cpp
    FurbleTimezoneGPS.cpp
    ../lib/furble/protocol/ProvisionTLV.cpp
    FurbleWiFi.cpp
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
    console esp_driver_rmt esp_driver_sdspi esp_driver_usb_serial_jtag esp_pm fatfs sdmmc esp_wifi esp_netif esp_event nvs_flash lwip)
if(DEFINED FURBLE_MQTT AND FURBLE_MQTT)
  list(APPEND furble_sources FurbleMQTT.cpp)
  list(APPEND furble_private_requires mqtt json)
endif()
if(DEFINED FURBLE_WEBUI AND FURBLE_WEBUI)
  list(APPEND furble_sources FurbleWebUI.cpp)
  list(APPEND furble_private_requires esp_https_server json mbedtls)
endif()
if(FURBLE_ETHERNET)
  list(APPEND furble_private_requires
       esp_driver_gpio esp_driver_spi esp_eth esp_event esp_netif)
endif()

# The LilyGO family seam. The panel, the family facts and the board I/O are one
# feature: a LilyGO image without them has no display and no input. The profile
# macro reaches CMake as well as the compiler so the sources and their GPIO/SPI
# requirements are resolved only for those envs.
if(FURBLE_LILYGO)
  list(APPEND furble_sources
       FurbleDisplay.cpp
       FurbleLilyGo.cpp
       FurbleLilyGoDevice.cpp)
  list(APPEND furble_private_requires esp_driver_gpio esp_driver_spi)
endif()

if(FURBLE_NO_DISPLAY)
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_private_requires} esp_hw_support)
else()
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_private_requires} esp_hw_support
                         REQUIRES icons)
endif()

# The AXP2101 and XL9555 register drivers are plain sources in lib/hal, which
# PlatformIO exposes as a library. Name the directory explicitly so the firmware
# include path does not depend on that library discovery. This has to follow
# idf_component_register(): before it, COMPONENT_LIB names no target yet.
if(FURBLE_LILYGO)
  target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../lib/hal)
endif()
