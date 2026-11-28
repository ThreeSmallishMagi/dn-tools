cmake_minimum_required(VERSION 3.10)
project(dither CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenCV REQUIRED)
# libstdc++ runs std::execution::par algorithms on TBB, which OpenCV already loads.
find_package(TBB REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall)

add_executable(srgbtest srgbtest.cpp)
target_link_libraries(srgbtest PRIVATE dither_lib)

add_executable(temporaldither temporaldither.cpp)
target_link_libraries(temporaldither PRIVATE ${OpenCV_LIBS})
target_include_directories(temporaldither PRIVATE ${OpenCV_INCLUDE_DIRS})

# Everything except the process entry point, so that a test can link against it
# rather than reaching it through a subprocess and a PNG. See doc/ARCHITECTURE.md.
add_library(dither_lib STATIC
  dither.cpp engine.cpp cli.cpp mask.cpp generators.cpp
  bluenoise.cpp spectralmask.cpp markov.cpp markovcalib.cpp inpaint.cpp
  directmask.cpp inpaintcalib.cpp dftcalib.cpp
  maskutil.cpp maskcache.cpp calib.cpp samples.cpp defaults.cpp srgb.cpp)
target_link_libraries(dither_lib PUBLIC ${OpenCV_LIBS} TBB::tbb)
target_include_directories(dither_lib PUBLIC ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dither main.cpp)
target_link_libraries(dither PRIVATE dither_lib)

# Synthesises a texture with the generator's own code, so the result can be
# looked at rather than only scored. See maketexture.cpp.
add_executable(maketexture maketexture.cpp)
target_link_libraries(maketexture PRIVATE dither_lib)

# Unit tests: the checks that are about an invariant of the code rather than a
# property of an image. Links the library and calls the functions; test/run
# invokes it like any other check.
add_executable(unittest unittest.cpp)
target_link_libraries(unittest PRIVATE dither_lib)
