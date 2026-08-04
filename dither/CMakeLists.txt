cmake_minimum_required(VERSION 3.10)
project(dither CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenCV REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall)

add_executable(srgbtest srgbtest.cpp srgb.cpp)
target_link_libraries(srgbtest PRIVATE ${OpenCV_LIBS})
target_include_directories(srgbtest PRIVATE ${OpenCV_INCLUDE_DIRS})

add_executable(temporaldither temporaldither.cpp)
target_link_libraries(temporaldither PRIVATE ${OpenCV_LIBS})
target_include_directories(temporaldither PRIVATE ${OpenCV_INCLUDE_DIRS})

add_executable(dither dither.cpp bluenoise.cpp srgb.cpp)
target_link_libraries(dither PRIVATE ${OpenCV_LIBS})
target_include_directories(dither PRIVATE ${OpenCV_INCLUDE_DIRS})

add_custom_target(deploy
    COMMAND "${CMAKE_SOURCE_DIR}/deploy.sh" "${CMAKE_SOURCE_DIR}"
    DEPENDS dither
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running deployment script"
)
