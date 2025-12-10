plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "io.github.a13e300.fusefixer"
    compileSdk {
        version = release(36)
    }

    defaultConfig {
        applicationId = "io.github.a13e300.fusefixer"
        minSdk = 31
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                arguments += "-DCMAKE_CXX_STANDARD=20"
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
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {
    compileOnly(libs.xposed.api)
}