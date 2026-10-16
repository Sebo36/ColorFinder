cmake_minimum_required(VERSION 3.20)
project(ColorFinder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc highgui)

add_executable(imageColorer imageColorer.cpp model_paths.h imageColorer.rc)
target_include_directories(imageColorer PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(imageColorer PRIVATE ${OpenCV_LIBS})

add_executable(picker WIN32 picker.cpp model_paths.h picker.rc)
target_include_directories(picker PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(picker PRIVATE ${OpenCV_LIBS} user32)

add_executable(setup WIN32 setup.cpp setup.rc)
target_link_libraries(setup PRIVATE shell32 ole32 user32)