buildscript {
    dependencies {
        // AGP 9.2 bundles KGP 2.3.10. Keep the Compose compiler and built-in
        // Kotlin compiler on the explicitly selected current Kotlin release.
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:2.4.10")
    }
}

plugins {
    id("com.android.application") version "9.2.0" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.10" apply false
}
