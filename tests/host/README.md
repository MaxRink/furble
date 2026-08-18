# Host camera harness

The host harness compiles the production Fujifilm Basic client with macOS
clang or a normal Linux C++ compiler. It uses the in-memory NimBLE seam in
`nimble/` and the server-side `peer/FujifilmVirtualCamera` model. No radio,
ESP-IDF, PlatformIO, or Arduino dependency is required.

The current master branch does not contain the Tier B mock from the plan. This
directory carries the small compatible seam needed to build Tier C standalone.
When Tier B lands, the virtual peer API is the boundary to preserve.

Build and run:

```sh
cmake -S tests/host -B /tmp/furble-host-build
cmake --build /tmp/furble-host-build
ctest --test-dir /tmp/furble-host-build --output-on-failure
```

The replay test takes the synthetic capture path from the CMake source
directory. Real X100VI captures are intentionally not included. Plan 64's BT
journal work must produce reviewed, normalized vectors before this fixture can
be replaced with hardware evidence.
