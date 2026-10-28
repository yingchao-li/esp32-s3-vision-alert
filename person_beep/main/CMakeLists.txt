set(PERSON_MODEL_FILE "${CMAKE_CURRENT_LIST_DIR}/../models/pedestrian_detect_pico_s8_v1.espdl")
file(SHA256 "${PERSON_MODEL_FILE}" MODEL_SHA256)
if(NOT MODEL_SHA256 STREQUAL "3eb5827e12322728fe99ea9c86d71ea38aa5a5b70064abf7d75bc90140b74015")
    message(FATAL_ERROR "Person model SHA256 mismatch. See models/README.md.")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/model_data.S.in" "${CMAKE_CURRENT_BINARY_DIR}/model_data.S" @ONLY)
idf_component_register(
    SRCS app_main.cpp buzzer.cpp detector.cpp "${CMAKE_CURRENT_BINARY_DIR}/model_data.S"
    INCLUDE_DIRS .
    REQUIRES esp-dl esp32-camera esp_timer esp_psram spi_flash esp_driver_gpio
             esp_driver_rmt mbedtls freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
