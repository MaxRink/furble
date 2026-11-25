plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

// The UI snapshot baseline lives next to the test that compares against it.
// The path is passed to the test JVM because the test task runs from this
// module directory and because a failed comparison has to name the file.
val appScreenshotDir = layout.projectDirectory.dir("src/test/screenshots").asFile.absolutePath

// Regeneration writes that baseline, which is also the test task's declared
// input, so update mode has to turn off result reuse to run at all.
val updateScreenshots = providers.gradleProperty("updateScreenshots").isPresent

android {
    namespace = "com.furble.companion"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.furble.companion"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables {
            useSupportLibrary = true
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    testOptions {
        unitTests {
            // Robolectric loads the merged debug manifest, the app theme and the
            // Compose resources, so the local test task needs them on disk.
            isIncludeAndroidResources = true
            all { test ->
                // The UI snapshot test renders with Robolectric's native Skia
                // runtime. Existing tests are plain JVM tests, so the property
                // never reaches them.
                test.systemProperty("robolectric.graphicsMode", "NATIVE")
                test.systemProperty("furble.screenshot.dir", appScreenshotDir)
                test.systemProperty(
                    "furble.screenshot.update",
                    updateScreenshots.toString(),
                )
            }
        }
    }
}

kotlin {
    jvmToolchain(17)
}

tasks.withType<Test>().configureEach {
    // The committed baseline is a test input. Without this a regenerated image
    // leaves testDebugUnitTest up to date and the comparison never runs.
    inputs.files(fileTree(appScreenshotDir) { include("*.png") })
        .withPropertyName("uiSnapshotBaselines")
    if (updateScreenshots) {
        // Regeneration writes the input it just read, so a previous result must
        // never be reused: no up-to-date check and no build cache entry.
        outputs.upToDateWhen { false }
        outputs.cacheIf { false }
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.08.00"))
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.activity:activity-ktx:1.13.0")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-core")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.core:core-ktx:1.19.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.11.0")
    implementation("com.google.android.gms:play-services-location:21.3.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")

    testImplementation("junit:junit:4.13.2")

    // Robolectric runs the Compose UI on the JVM so ./gradlew test can compare a
    // rendered bitmap against the committed baseline. ui-test-manifest adds the
    // empty, filterless ComponentActivity that the Compose test rule launches to
    // the debug manifest. The release manifest and the launcher entry are
    // unchanged, so the emulator smoke job still starts MainActivity.
    testImplementation("org.robolectric:robolectric:4.18.1")
    testImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
