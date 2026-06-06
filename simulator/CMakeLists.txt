cmake_minimum_required(VERSION 3.16)
project(meshcore_tdeck_sim C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(APP_TARGET_SIMULATOR=1)

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
)
FetchContent_MakeAvailable(lvgl)

find_package(SDL2)
find_package(X11)

if(SDL2_FOUND AND X11_FOUND)
    add_executable(meshcore_tdeck_sim
        main_lvgl.cpp
        sim_service.cpp
        ../common/app/app_controller.cpp
        ../common/app/app_types.cpp
        ../common/app/mock_data.cpp
        ../common/ui/app_ui.cpp
    )

    target_compile_definitions(meshcore_tdeck_sim PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
    target_include_directories(meshcore_tdeck_sim PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${lvgl_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
        ${X11_INCLUDE_DIR}
    )
    target_link_libraries(meshcore_tdeck_sim PRIVATE lvgl ${SDL2_LIBRARIES} ${X11_LIBRARIES})
else()
    add_custom_target(meshcore_tdeck_sim
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping LVGL SDL simulator: SDL2/X11 development files are not installed")
    message(WARNING "SDL2/X11 development files not found; meshcore_tdeck_sim target will be skipped")
endif()

add_executable(navigation_test
    test_navigation.cpp
)

target_include_directories(navigation_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

add_executable(app_controller_test
    test_app_controller.cpp
    ../common/app/app_controller.cpp
    ../common/app/app_types.cpp
    ../common/app/mock_data.cpp
)

target_include_directories(app_controller_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

add_executable(lvgl_ui_test
    test_lvgl_ui.cpp
    ../common/app/app_controller.cpp
    ../common/app/app_types.cpp
    ../common/app/mock_data.cpp
    ../common/ui/app_ui.cpp
)

target_compile_definitions(lvgl_ui_test PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
target_include_directories(lvgl_ui_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${lvgl_SOURCE_DIR}/src
)
target_link_libraries(lvgl_ui_test PRIVATE lvgl)
