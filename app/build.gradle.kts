import com.android.build.gradle.tasks.PackageAndroidArtifact
import java.nio.charset.StandardCharsets

plugins {
    alias(libs.plugins.android.application)
}

fun String.execute(): String =
    Runtime.getRuntime().exec(split("\\s".toRegex()).toTypedArray())
        .let { proc ->
            proc.waitFor()
            val result = proc.inputStream.use {
                it.readBytes()
            }.toString(StandardCharsets.UTF_8).trim()
            proc.destroy()
            result
        }


val gitCommitCount = "git rev-list HEAD --count".execute().toInt()
val gitCommitHash = "git rev-parse --verify --short HEAD".execute()

android {
    namespace = "io.github.a13e300.fusefixer"
    compileSdk {
        version = release(36)
    }

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "io.github.a13e300.fusefixer"
        minSdk = 31
        targetSdk = 36
        versionCode = gitCommitCount
        versionName = gitCommitHash

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                arguments += "-DCMAKE_CXX_STANDARD=20"
                cppFlags(
                    "-fno-rtti", "-fno-exceptions",
                    "-ffunction-sections", "-fdata-sections",
                    "-fasynchronous-unwind-tables", "-fno-unwind-tables",
                    "-fvisibility=hidden", "-fvisibility-inlines-hidden",
                )
            }
        }
        base.archivesName = "FuseFixer-${gitCommitCount}-${gitCommitHash}-${System.currentTimeMillis()}"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    cppFlags += arrayOf("-flto")
                }
            }
            signingConfig = signingConfigs["debug"]
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

    packaging {
        resources.excludes += "**"
    }

    // https://stackoverflow.com/a/77745844
    tasks.withType<PackageAndroidArtifact> {
        doFirst { appMetadata.asFile.orNull?.writeText("") }
    }

    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }
}

dependencies {
    compileOnly(libs.xposed.api)
    implementation(libs.androidx.core)
}