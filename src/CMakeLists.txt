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
    FurblePlatform.cpp
    FurblePower.cpp
    FurbleSD.cpp
    FurbleSettings.cpp
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
       FurbleUIIntervalometer.cpp)
endif()

# FURBLE_MQTT defaults on. When it is set to 0 the MQTT source and its component
# dependencies (esp-mqtt pulls esp_tls and the mbedtls certificate bundle, plus
# json for cJSON) drop out entirely, so a no-MQTT image carries none of that
# code. esp_tls and mbedtls are declared by esp-mqtt in its own idf_component.yml
# and resolve transitively, so they must not be listed here.
set(furble_priv_requires
    console esp_driver_rmt esp_driver_sdspi esp_driver_usb_serial_jtag esp_pm
    fatfs sdmmc esp_wifi esp_netif esp_event nvs_flash lwip)
if(NOT DEFINED FURBLE_MQTT OR FURBLE_MQTT)
  list(APPEND furble_sources FurbleMQTT.cpp)
  list(APPEND furble_priv_requires mqtt json)
endif()

# console and esp_driver_usb_serial_jtag are only referenced by the developer
# console. Nothing references them without FURBLE_CONSOLE, so the linker leaves
# them out and the release binaries are unchanged.
if(FURBLE_NO_DISPLAY)
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_priv_requires})
else()
  idf_component_register(SRCS ${furble_sources}
                         PRIV_REQUIRES ${furble_priv_requires}
                         REQUIRES icons)
endif()
