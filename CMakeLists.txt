cmake_minimum_required(VERSION 3.0.0)
project(sftrnd VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

if(WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
endif()

include(CTest)
enable_testing()

add_executable(sftrnd main.cpp)

set(VCPKG_TARGET_TRIPLET x64-windows-static)
# Find packages, the following line is only necessary for VCPKG users.
include($ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
# include_directories($ENV{VCPKG_ROOT}/installed/x64-windows-static/include)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
target_link_libraries(
        sftrnd PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
