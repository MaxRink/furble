# components/icons/

Generated LVGL image arrays. Do not hand-edit the pixel data.

- Each `icon_*.c` is a generated `lv_image_dsc_t`: 64x64 RGB565A8 with
  `LV_IMAGE_FLAGS_COMPRESSED` (the `_24` and `_20` variants are smaller
  sizes of the same format).
- Adding an icon means: generated .c file, entry in `CMakeLists.txt`
  `COMPONENT_SRCS`, extern declaration in `icons.h`.
- The IMU spirit-level menu uses the generated `icon_adjust` asset. Keep its
  source SVG and generated C/RGB565A8 variants in sync; do not hand-edit the
  pixel arrays.
- Adding icons affects the image cache budget. Compressed icons cost a
  12.3 KB decompress per 64x64 draw when they fall out of the cache, so check
  cache sizing when the icon set grows.
