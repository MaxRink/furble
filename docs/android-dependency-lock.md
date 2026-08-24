# Android dependency lock

The companion app uses Android Gradle Plugin 9.2.0 with Gradle 9.4.1,
Kotlin 2.4.10, compileSdk and targetSdk 37, Compose BOM 2026.08.00, Activity
1.13.0, Core KTX 1.19.0, and Lifecycle 2.11.0. The exact values are mirrored
in [`tools/android-dependency-lock.json`](../tools/android-dependency-lock.json).
The Gradle wrapper also verifies the official distribution SHA-256 before use.

AGP 9 provides built-in Kotlin support, so the deprecated
`org.jetbrains.kotlin.android` plugin is removed. The Compose compiler plugin
remains applied. CI installs the API 37 platform and build tools explicitly
before running tests and the debug APK build.

These are Android-only changes. Firmware, simulator, and ESP-IDF dependency
locks are intentionally excluded and belong to separate compatibility work.

Primary release references:

- [AGP 9.2.0 release notes](https://developer.android.com/build/releases/agp-9-2-0-release-notes)
- [Kotlin releases](https://kotlinlang.org/docs/releases.html)
- [AndroidX stable channel](https://developer.android.com/jetpack/androidx/versions/stable-channel)
