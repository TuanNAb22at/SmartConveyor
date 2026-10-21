cmake_minimum_required(VERSION 3.20)

project(SmartConveyorSortingSystem
    VERSION 1.0.0
    DESCRIPTION "Mo phong bang tai phan loai san pham bang C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SMART_CONVEYOR_CORE_SOURCES
    src/application/ConveyorController.cpp
    src/platform/simulation/MockPhotoelectricSensor.cpp
    src/platform/simulation/MockEncoder.cpp
    src/platform/simulation/MockMotor.cpp
    src/platform/simulation/MockDiverter.cpp
    src/diagnostics/ConsoleLogger.cpp
)

function(configure_conveyor_target target_name)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(WIN32)
    add_executable(smart_conveyor WIN32 app/windows_main.cpp ${SMART_CONVEYOR_CORE_SOURCES})
    target_link_libraries(smart_conveyor PRIVATE comdlg32)
    add_executable(smart_conveyor_cli app/main.cpp ${SMART_CONVEYOR_CORE_SOURCES})
    configure_conveyor_target(smart_conveyor)
    configure_conveyor_target(smart_conveyor_cli)
else()
    add_executable(smart_conveyor app/main.cpp ${SMART_CONVEYOR_CORE_SOURCES})
    configure_conveyor_target(smart_conveyor)
endif()
