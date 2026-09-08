plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.android")
}

android {
  namespace = "com.remote60.androiddirect"
  compileSdk = 34

  defaultConfig {
    applicationId = "com.remote60.androiddirect"
    minSdk = 28
    targetSdk = 34
    versionCode = 11
    versionName = "0.2.12"

    externalNativeBuild {
      cmake {
        cppFlags += "-std=c++20"
      }
    }
  }

  buildTypes {
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
}
