idf_component_register(
    SRCS app_main.cpp camera_manager.cpp ai_detector.cpp model_manager.cpp object_tracker.cpp
         obstacle_analyzer.cpp buzzer_controller.cpp web_server.cpp preview_manager.cpp
         wifi_manager.cpp performance_monitor.cpp system_health.cpp
    INCLUDE_DIRS . "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES esp-dl esp32-camera esp_timer esp_psram spi_flash esp_driver_gpio esp_driver_rmt
             mbedtls freertos esp_wifi esp_event esp_netif esp_http_server nvs_flash json)

if(CONFIG_VISION_FOUR_CLASS_MODEL)
    set(manifest "${CMAKE_CURRENT_LIST_DIR}/../models/four_class.json")
    set(family_arg --four)
elseif(CONFIG_VISION_MODEL_YOLO11_COCO)
    set(manifest "${CMAKE_CURRENT_LIST_DIR}/../models/yolo11n_coco.json")
    set(family_arg --yolo11)
else()
    set(manifest "${CMAKE_CURRENT_LIST_DIR}/../models/person_baseline.json")
    set(family_arg)
endif()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/verify_model.py"
    "${manifest}" ${family_arg} --header "${CMAKE_CURRENT_BINARY_DIR}/model_spec.hpp"
    RESULT_VARIABLE verification OUTPUT_VARIABLE MODEL_FILE ERROR_VARIABLE detail OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT verification EQUAL 0)
    message(FATAL_ERROR "Real model verification failed: ${detail}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}" "${MODEL_FILE}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/model_data.S.in" "${CMAKE_CURRENT_BINARY_DIR}/model_data.S" @ONLY)
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/model_data.S")
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/dashboard.html" TEXT)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)

# Diagnose the native load result and stop on failed arena allocation. The
# pinned dependency remains unmodified; only this build-local source is used.
idf_component_get_property(dl_dir espressif__esp-dl COMPONENT_DIR)
idf_component_get_property(dl_lib espressif__esp-dl COMPONENT_LIB)
set(dl_original "${dl_dir}/dl/model/src/dl_model_base.cpp")
set(dl_diagnostic "${CMAKE_CURRENT_BINARY_DIR}/dl_model_base_diagnostic.cpp")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/instrument_model.py"
    "${dl_original}" "${dl_diagnostic}" RESULT_VARIABLE patched ERROR_VARIABLE patch_detail)
if(NOT patched EQUAL 0)
    message(FATAL_ERROR "ESP-DL diagnostic instrumentation failed: ${patch_detail}")
endif()
get_target_property(dl_sources ${dl_lib} SOURCES)
set(dl_kept)
set(dl_replaced FALSE)
foreach(source IN LISTS dl_sources)
    if(source MATCHES "(^|/)dl_model_base\\.cpp$")
        set(dl_replaced TRUE)
    else()
        list(APPEND dl_kept "${source}")
    endif()
endforeach()
if(NOT dl_replaced)
    message(FATAL_ERROR "ESP-DL model source not found in component sources")
endif()
set_property(TARGET ${dl_lib} PROPERTY SOURCES "${dl_kept};${dl_diagnostic}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${dl_original}" "${CMAKE_CURRENT_LIST_DIR}/../tools/instrument_model.py")
