plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

val fixedDirectoryUrl = "https://gnlink.shotan.net"
// Names the same server was signed in to under before the address was fixed, comma separated.
// A session stored under one of these exact origins may be presented to fixedDirectoryUrl, and
// the stored address is rewritten once the server has accepted it. Anything else is neither
// sent nor erased. Keep in step with kMigratableDirectoryOrigins (fixed_directory.hpp).
val migratableDirectoryOrigins = "https://rem.shotan.net"

// A release that was handed the test property is refused rather than quietly built with the
// fixed address: whoever passed it believes they are getting something else.
gradle.taskGraph.whenReady {
  val release = allTasks.any { it.project == project && it.name.contains("Release") }
  for (property in listOf("gnlink.testDirectoryUrl", "gnlink.testMigratableOrigins")) {
    if (release && project.hasProperty(property)) {
      throw GradleException(
        "$property is for debug builds; a release build always uses $fixedDirectoryUrl")
    }
  }
}

android {
  namespace = "com.remote60.androiddirect"
  compileSdk = 34

  defaultConfig {
    applicationId = "com.remote60.androiddirect"
    minSdk = 28
    targetSdk = 34
    versionCode = 25
    versionName = "0.2.26"

    externalNativeBuild {
      cmake {
        cppFlags += "-std=c++20"
      }
    }

    // The directory server: the one address this app signs in to, lists PCs from, connects
    // through, uploads its log to and derives its update endpoint from. The sign-in screen has
    // no field for it and nothing stored on the phone is read in its place.
    //
    // Written here and nowhere else. The debug build type below may replace it for a test; a
    // release build cannot be given another one.
    buildConfigField("String", "DIRECTORY_URL", "\"" + fixedDirectoryUrl + "\"")
    buildConfigField("String", "DIRECTORY_MIGRATABLE_ORIGINS",
      "\"" + migratableDirectoryOrigins + "\"")

    // Where updates come from, and the key that says a manifest is ours.
    //
    // Empty by default, and empty is a real answer rather than a stub: UpdateFlow reports "this
    // build has no update endpoint or trusted key" and does nothing, which is correct for every
    // build made before an operational key and a release host exist. Filling them in with
    // plausible-looking values would make a build that tries to check and fails, which is worse
    // than one that knows it cannot.
    buildConfigField("String", "UPDATE_MANIFEST_URL",
      "\"" + (project.findProperty("gnlink.updateManifestUrl") ?: "") + "\"")
    // The operational release key, raw X||Y (64 bytes, 128 hex characters) -- NOT the SPKI form.
    // Getting that wrong is silent: the SPKI encoding decodes and then fails a length check, so
    // every update is refused and the build looks exactly like one with nothing published.
    //
    // A default rather than a property, so what a candidate carries does not depend on remembering
    // a -P flag. The property still overrides, for a build that must not trust this key.
    buildConfigField("String", "UPDATE_PUBLIC_KEY_HEX",
      "\"" + (project.findProperty("gnlink.updatePublicKeyHex")
        ?: "8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a872a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e") + "\"")
  }

  buildFeatures {
    buildConfig = true
  }

  buildTypes {
    debug {
      // TEST BUILDS ONLY: -Pgnlink.testDirectoryUrl=http://10.0.2.2:18200 points a debug APK at
      // a fixture directory. There is no such line under `release`.
      (project.findProperty("gnlink.testDirectoryUrl") as String?)?.let {
        buildConfigField("String", "DIRECTORY_URL", "\"" + it + "\"")
        // A fixture has no former names unless the test names them.
        buildConfigField("String", "DIRECTORY_MIGRATABLE_ORIGINS",
          "\"" + (project.findProperty("gnlink.testMigratableOrigins") ?: "") + "\"")
      }
    }
    release {
      isMinifyEnabled = false
      proguardFiles(
        getDefaultProguardFile("proguard-android-optimize.txt"),
        "proguard-rules.pro"
      )
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  kotlinOptions {
    jvmTarget = "17"
  }

  externalNativeBuild {
    cmake {
      path = file("src/main/cpp/CMakeLists.txt")
      version = "3.22.1"
    }
  }
}

dependencies {
  implementation("androidx.core:core-ktx:1.12.0")

  // JVM unit tests only. The version-comparison contract is pure string logic shared with the
  // C++ and JS suites (apps/shared/version_compare_vectors.txt), so it needs a JVM and not a
  // device -- nothing here ships in the APK.
  testImplementation("junit:junit:4.13.2")
  // The real org.json, not the stub android.jar ships for unit tests. That stub throws on every
  // call unless returnDefaultValues is turned on, and turning it on would make JSONObject hand
  // back nulls and zeros -- so a parser test would pass while parsing nothing. Nothing here ships
  // in the APK; on a device the platform's own org.json is used.
  testImplementation("org.json:json:20240303")
}
