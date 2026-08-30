package com.furble.companion.protocol

/** The editor used for a setting value received from the firmware. */
enum class SettingEditorKind {
    SWITCH,
    ENUM,
    RANGE,
    UINT32_STEPPER,
    THEME,
    INTERVAL,
    READ_ONLY,
}

data class SettingOption(
    val value: Int,
    val label: String,
)

data class SettingRange(
    val min: Int,
    val max: Int,
    val step: Int = 1,
    val unit: String? = null,
)

/**
 * Static app metadata for the frozen firmware wire_id table.
 *
 * The names, keys, wire types and option values are copied from
 * src/FurbleSettings.cpp, src/FurbleConsole.cpp and the on-device UI. The
 * wire_id is the only join key. Unknown ids remain visible as read-only rows.
 */
data class SettingMetadata(
    val wireId: Int,
    val key: String,
    val name: String,
    val group: String,
    val wireType: Int,
    val editor: SettingEditorKind,
    val options: List<SettingOption> = emptyList(),
    val stringOptions: List<String> = emptyList(),
    val range: SettingRange? = null,
    val dangerous: Boolean = false,
)

object FurbleSettingMetadata {
    private fun option(value: Int, label: String) = SettingOption(value, label)

    private val displayOffOptions = listOf(
        option(0, "Dim"),
        option(1, "Off"),
        option(2, "Off, remote on"),
    )

    private val gpsRateOptions = listOf(
        option(0, "Default"),
        option(1, "1000 ms"),
        option(2, "500 ms"),
        option(3, "200 ms"),
        option(4, "100 ms"),
    )

    private val gpsConstellationOptions = listOf(
        option(0, "Default"),
        option(1, "GPS"),
        option(2, "BDS"),
        option(3, "GPS+BDS"),
        option(4, "GLONASS"),
        option(5, "GPS+GLO"),
        option(6, "BDS+GLO"),
        option(7, "All"),
    )

    private val gpsAssistanceOptions = listOf(
        option(0, "Off"),
        option(1, "Position and time"),
        option(2, "Position, time and cache"),
    )

    private val autoOffOptions = listOf(
        option(0, "Never"),
        option(5, "5 mins"),
        option(10, "10 mins"),
        option(30, "30 mins"),
        option(60, "60 mins"),
    )

    private val textSizeOptions = listOf(
        option(0, "Small"),
        option(1, "Normal"),
        option(2, "Large"),
    )

    private val metadata = listOf(
        SettingMetadata(1, "brightness", "Brightness", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.RANGE, range = SettingRange(16, 240, 16)),
        SettingMetadata(
            2, "inactivity", "Inactivity", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.RANGE,
            options = listOf(
                option(0, "Never"),
                option(1, "30 secs"),
                option(2, "60 secs"),
                option(4, "2 mins"),
                option(10, "5 mins"),
                option(20, "10 mins"),
            ),
            range = SettingRange(0, 20, 1, "timeout"),
        ),
        SettingMetadata(24, "display_off", "Screen off", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = displayOffOptions),
        SettingMetadata(3, "theme", "Theme", "Display", FurbleProtocol.SettingType.STRING,
            SettingEditorKind.THEME, stringOptions = listOf("Dark", "Default", "Mono Furble")),
        SettingMetadata(40, "text_size", "Text size", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = textSizeOptions),
        SettingMetadata(4, "tx_power", "TX Power", "Bluetooth", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "Low (P3)"), option(1, "Medium (P6)"), option(2, "High (P9)")),
            dangerous = true),
        SettingMetadata(28, "tx_adaptive", "Adaptive", "Bluetooth", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH, dangerous = true),
        SettingMetadata(5, "gps", "GPS", "GPS", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(45, "imu", "IMU", "Sensors", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(
            63, "imu_wake", "Wake Gesture", "Sensors", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(
                option(0, "Off"),
                option(1, "Tap"),
                option(2, "Shake"),
                option(3, "Tap or shake"),
            ),
        ),
        SettingMetadata(64, "imu_trigger", "Double-Tap Shutter", "Sensors", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(6, "gps_baud", "GPS Baud", "GPS", FurbleProtocol.SettingType.UINT32,
            SettingEditorKind.ENUM, options = listOf(option(9600, "9600"), option(115200, "115200"))),
        SettingMetadata(13, "gps_rate", "GPS Rate", "GPS", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = gpsRateOptions),
        SettingMetadata(14, "gps_nmea", "GPS Sentences", "GPS", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(15, "gps_constel", "GPS Constellation", "GPS", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = gpsConstellationOptions),
        SettingMetadata(
            25, "gps_power", "GPS Power", "GPS", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(
                option(0, "Always on"),
                option(1, "Standby (PCAS12)"),
                option(2, "Rail cycling"),
            ),
        ),
        SettingMetadata(
            26, "gps_duty", "GPS Duty", "GPS", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(
                option(0, "No standby"),
                option(5, "5 s"),
                option(10, "10 s"),
                option(15, "15 s"),
            ),
        ),
        SettingMetadata(
            41, "gps_assist", "GPS Assistance", "GPS", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = gpsAssistanceOptions,
        ),
        SettingMetadata(7, "interval", "Interval", "Intervalometer", FurbleProtocol.SettingType.BLOB,
            SettingEditorKind.INTERVAL),
        SettingMetadata(8, "multiconnect", "Multi-Connect", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(9, "reconnect", "Infinite-ReConnect", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(16, "recon_backoff", "Reconnect Backoff", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(10, "fauxNY", "FauxNY", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(11, "autoconnect", "Auto-Connect", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(12, "companion", "Companion", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH, dangerous = true),
        SettingMetadata(30, "preset_picker", "Preset Picker", "Connections", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(
            27, "button_mode", "Button Mode", "Controls", FurbleProtocol.SettingType.STRING,
            SettingEditorKind.ENUM, stringOptions = listOf("two-button", "one-button"),
        ),
        SettingMetadata(17, "cpu_freq", "CPU Speed", "Power", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(80, "80 MHz"), option(160, "160 MHz"), option(240, "240 MHz")),
            dangerous = true),
        SettingMetadata(18, "batt_style", "Battery Style", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "Icon"), option(1, "Percent"), option(2, "Both"))),
        SettingMetadata(19, "show_title", "Show Title", "Display", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(20, "sleep_conn", "Sleep while connected", "Power", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH, dangerous = true),
        SettingMetadata(21, "scan_mode", "Scan Mode", "Bluetooth", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "Full"), option(1, "Balanced"), option(2, "Low"))),
        SettingMetadata(22, "scan_timeout", "Scan Timeout", "Bluetooth", FurbleProtocol.SettingType.UINT32,
            SettingEditorKind.UINT32_STEPPER,
            options = listOf(
                option(0, "Never"),
                option(30, "30 secs"),
                option(60, "60 secs"),
                option(120, "120 secs"),
            ),
            range = SettingRange(0, 120, 30, "seconds")),
        SettingMetadata(
            37, "auto_off", "Auto off", "Power", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM, options = autoOffOptions,
        ),
        SettingMetadata(
            38, "low_batt", "Low battery", "Power", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "None"), option(1, "Warn"), option(2, "Warn then off")),
        ),
        SettingMetadata(39, "sd_gpx", "GPX Logging", "GPS", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(44, "boot_splash", "Boot screen", "Display", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(
            36, "display_mode", "Display Mode", "Display", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "GUI"), option(1, "Console")),
        ),
        SettingMetadata(29, "conn_saver", "Connection power save", "Bluetooth", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(31, "ir", "Infrared", "Infrared", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
        SettingMetadata(32, "ir_proto", "IR Protocol", "Infrared", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(option(0, "Nikon"), option(1, "Sony"), option(2, "Canon"), option(3, "Canon 2s"))),
        SettingMetadata(33, "fb_output", "Feedback", "Feedback", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.ENUM,
            options = listOf(
                option(0, "Off"),
                option(1, "Sound"),
                option(2, "Light"),
                option(3, "Vibrate"),
                option(4, "Sound and Light"),
            )),
        SettingMetadata(34, "fb_events", "Feedback Events", "Feedback", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.RANGE, range = SettingRange(0, 255, 1, "mask")),
        SettingMetadata(35, "fb_volume", "Volume", "Feedback", FurbleProtocol.SettingType.UINT8,
            SettingEditorKind.RANGE, range = SettingRange(0, 255)),
        SettingMetadata(23, "watchdog", "Watchdog", "Power", FurbleProtocol.SettingType.BOOL,
            SettingEditorKind.SWITCH),
    )

    /** Keyed by the frozen firmware wire_id. */
    val byWireId: Map<Int, SettingMetadata> = metadata.associateBy { it.wireId }
}
