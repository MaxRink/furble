set(ICON_SRCS
    icon_add_a_photo.c
    icon_add_a_photo_24.c
    icon_all_inclusive.c
    icon_arrow_back_24.c
    icon_arrow_downward_24.c
    icon_arrow_forward_24.c
    icon_arrow_upward_24.c
    icon_battery_android_0.c
    icon_battery_android_frame_2.c
    icon_battery_android_frame_4.c
    icon_battery_android_frame_6.c
    icon_battery_android_frame_full.c
    icon_bluetooth.c
    icon_bluetooth_24.c
    icon_camera.c
    icon_camera_24.c
    icon_cell_tower.c
    icon_cell_tower_24.c
    icon_center_focus_strong.c
    icon_center_focus_strong_24.c
    icon_check_24.c
    icon_clear_all_24.c
    icon_delete.c
    icon_delete_24.c
    icon_info.c
    icon_info_24.c
    icon_linked_camera.c
    icon_linked_camera_24.c
    icon_location_disabled.c
    icon_location_searching.c
    icon_location_searching_24.c
    icon_lock.c
    icon_lock_24.c
    icon_lock_open_right.c
    icon_lock_open_right_24.c
    icon_my_location.c
    icon_no_photography.c
    icon_no_photography_24.c
    icon_north_east_20.c
    icon_north_west_20.c
    icon_notifications_active.c
    icon_notifications_active_24.c
    icon_palette.c
    icon_palette_24.c
    icon_power_settings_new.c
    icon_power_settings_new_24.c
    icon_remote_gen.c
    icon_remote_gen_24.c
    icon_restart_alt_24.c
    icon_save_24.c
    icon_settings.c
    icon_settings_24.c
    icon_settings_brightness.c
    icon_settings_brightness_24.c
    icon_settings_remote.c
    icon_settings_remote_24.c
    icon_south_east_20.c
    icon_south_west_20.c
    icon_timer.c
    icon_timer_24.c
    icon_troubleshoot.c
    icon_troubleshoot_24.c
    icon_undo.c
    icon_wand_stars.c
    icon_wand_stars_24.c)

# The app REQUIRES this component unconditionally, because component
# requirements are expanded in an early CMake pass that sees neither the
# sdkconfig nor the profile cache variable. The displayless profile therefore
# empties the component here instead, where CONFIG_FURBLE_NO_DISPLAY is set.
if(CONFIG_FURBLE_NO_DISPLAY)
  idf_component_register()
else()
  idf_component_register(SRCS ${ICON_SRCS} INCLUDE_DIRS ".")
  target_compile_options(${COMPONENT_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE=1)
endif()
