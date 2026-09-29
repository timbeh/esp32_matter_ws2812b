# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    # ESP-IDF normally infers IDF_TARGET from sdkconfig inside project.cmake,
    # but ESP-Matter needs its device path before project.cmake is included.
    # Mirror ESP-IDF's target selection here so CLI reconfigure works without
    # relying on an IDE-specific IDF_TARGET environment setting.
    set(_project_idf_target "$ENV{IDF_TARGET}")
    if(NOT _project_idf_target)
        set(_project_idf_target "${IDF_TARGET}")
    endif()
    if(NOT _project_idf_target AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
        file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig" _sdkconfig_target_line
             REGEX "^CONFIG_IDF_TARGET=\"[a-zA-Z0-9_]+\"$")
        if(_sdkconfig_target_line MATCHES "^CONFIG_IDF_TARGET=\"([a-zA-Z0-9_]+)\"$")
            set(_project_idf_target "${CMAKE_MATCH_1}")
        endif()
    endif()
    if(NOT _project_idf_target)
        message(FATAL_ERROR "IDF_TARGET is unknown before ESP-Matter setup. Run 'idf.py set-target <target>' first or set ESP_MATTER_DEVICE_PATH.")
    endif()

    if("${_project_idf_target}" STREQUAL "esp32")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32c3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c3_devkit_m)
    elseif("${_project_idf_target}" STREQUAL "esp32c2")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c2_devkit_m)
    elseif("${_project_idf_target}" STREQUAL "esp32h2")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32h2_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32s3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32s3_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32c6")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c6_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32c61")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c61_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32c5")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c5_devkit_c)
    elseif("${_project_idf_target}" STREQUAL "esp32p4")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/hollow)
    else()
        message(FATAL_ERROR "Unsupported IDF_TARGET '${_project_idf_target}'")
    endif()
endif(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# Fail early when the external SDK checkouts don't match this repository's pins.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/check-build-versions.cmake)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append})

project(matter_led_matrix)

# WARNING: This is just an example for using key for decrypting the encrypted OTA image
# Please do not use it as is.
if(CONFIG_ENABLE_ENCRYPTED_OTA)
    target_add_binary_data(matter_led_matrix.elf "esp_image_encryption_key.pem" TEXT)
endif()

if(CONFIG_IDF_TARGET_ESP32C2)
    include(relinker)
endif()

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
