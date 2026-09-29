if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set. Export the pinned ESP-IDF environment before configuring.")
endif()
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "ESP_MATTER_PATH is not set. Export the pinned ESP-Matter environment before configuring.")
endif()

function(require_git_revision label repository expected_revision)
    execute_process(
        COMMAND git -C "${repository}" rev-parse HEAD
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE actual_revision
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT "${git_result}" STREQUAL "0")
        message(FATAL_ERROR "Could not read the ${label} Git revision at '${repository}'.")
    endif()
    if(NOT "${actual_revision}" STREQUAL "${expected_revision}")
        message(FATAL_ERROR
            "${label} revision mismatch. This project is pinned to ${expected_revision}, "
            "but '${repository}' is at ${actual_revision}. See the Build and flash section in README.md.")
    endif()
endfunction()

require_git_revision(
    "ESP-IDF v5.5.3"
    "$ENV{IDF_PATH}"
    "2c211b236707889e8400c4dc5644dd5c4ee071e0"
)
require_git_revision(
    "ESP-Matter"
    "$ENV{ESP_MATTER_PATH}"
    "94d54bc3353c1740a358dad1328ea4c20feebe5d"
)
require_git_revision(
    "ESP-Matter ConnectedHomeIP submodule"
    "$ENV{ESP_MATTER_PATH}/connectedhomeip/connectedhomeip"
    "8f943388af4d12dc5c484eae21b22723e03c3616"
)
