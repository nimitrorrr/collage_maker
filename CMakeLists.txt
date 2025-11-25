cmake_minimum_required(VERSION 3.10)
project(CollageApp)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automoc for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Concurrent)

# Source files
set(SOURCES main.cpp)

# Create executable
add_executable(CollageApp ${SOURCES})

# Link Qt libraries
target_link_libraries(CollageApp 
    Qt5::Core 
    Qt5::Widgets 
    Qt5::Gui
    Qt5::Concurrent
)

# Windows specific settings
if(WIN32)
    set_target_properties(CollageApp PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Installation
install(TARGETS CollageApp DESTINATION bin)
