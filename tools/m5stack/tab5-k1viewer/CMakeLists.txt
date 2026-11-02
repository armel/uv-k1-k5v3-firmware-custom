cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Application version, shown on the "connect your UV-K1" screen.
project(tab5_k1viewer VERSION 1.2.0)

# esp_video, the camera support pulled in by the Tab5 BSP and unused here,
# still uses ISP names that ESP-IDF 5.5 deprecates: keep its build quiet.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp_video" IN_LIST build_components)
    idf_component_get_property(esp_video_lib espressif__esp_video COMPONENT_LIB)
    target_compile_options(${esp_video_lib} PRIVATE -Wno-deprecated-declarations)
endif()
