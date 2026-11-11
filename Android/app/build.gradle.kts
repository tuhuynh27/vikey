import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// One version for every platform: project.yml's MARKETING_VERSION. Android versionCode = version * 10 + a rebuild digit
// (1.0.31 -> 100310; -PandroidRebuild=1 -> 100311): Google Play never takes the same versionCode twice, so a fixed
// build of the same version needs its own. (1.0.31 was 10031 under the old scheme; every new code is higher.)
val marketingVersion: String = file("../../project.yml").readLines().first { it.contains("MARKETING_VERSION") }.substringAfter('"').substringBefore('"')
val versionCodeFromVersion: Int = marketingVersion.split('.').let { (it[0].toInt() * 10000) + (it[1].toInt() * 100) + it[2].toInt() } * 10 +
    ((project.findProperty("androidRebuild") as String?)?.toInt() ?: 0)

android {
    namespace = "app.vikey"
    compileSdk = 36
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "app.vikey"
        minSdk = 26
        targetSdk = 36
        versionCode = versionCodeFromVersion
        versionName = marketingVersion
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { cppFlags += "-std=c++17"; arguments += listOf("-DANDROID_STL=c++_static") } }
    }
    signingConfigs {
        create("release") {
            val props = Properties()
            val f = rootProject.file("keystore.properties")
            if (f.exists()) {
                props.load(f.inputStream())
                storeFile = rootProject.file(props["storeFile"] as String)
                storePassword = props["storePassword"] as String
                keyAlias = props["keyAlias"] as String
                keyPassword = props["keyPassword"] as String
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (rootProject.file("keystore.properties").exists()) signingConfig = signingConfigs.getByName("release")
        }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    buildFeatures { compose = true; buildConfig = true }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    packaging { resources.excludes += "/META-INF/{AL2.0,LGPL2.1}" }
    sourceSets["main"].assets.srcDirs("src/main/assets", "../../App/Resources")   // Emoji.json shared with the Mac/iOS apps
    androidResources { ignoreAssetsPattern = "!Assets.xcassets:!Sounds" }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
}
