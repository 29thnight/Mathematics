# A consumer of the installed package, built the way a user would build one:
# its own project, no knowledge of the source tree, only find_package. CI
# installs Mathematics to a scratch prefix and points CMAKE_PREFIX_PATH here.
#
# It is deliberately not part of the main build. Built from inside the source
# tree it would resolve headers through the build interface and prove nothing
# about the install.
cmake_minimum_required(VERSION 3.24)
project(mathematics_package_consumer LANGUAGES CXX)

# Asking for a version exercises the installed version file as well: 1.0 must
# accept this release under SameMajorVersion.
find_package(Mathematics 1.0 REQUIRED)
message(STATUS "Found Mathematics ${Mathematics_VERSION} at ${Mathematics_DIR}")

add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE Mathematics::Mathematics)

# Built the way a careful user builds: every warning on and every warning fatal.
# A warning from an installed header is one the user can neither fix nor avoid
# except by suppressing it around the include, and under /WX or -Werror it is a
# build failure in their project, not ours. AVX2 is on so the widest code path
# is the one compiled -- 1.0.0 shipped a preprocessor warning (C4067 on MSVC
# 19.44) that sat inside an AVX-only block, where a baseline build never looks.
#
# CMake would hide exactly that from this project: an imported target's include
# directories are treated as system headers (-external:W0 on MSVC, -isystem
# elsewhere), so a find_package consumer never sees a warning from them. Users
# who add the source tree with add_subdirectory or FetchContent, or who pass a
# plain include path, do. Turning the treatment off makes this build see what
# they see.
set_target_properties(package_consumer PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
if(MSVC)
    target_compile_options(package_consumer PRIVATE /W4 /WX /permissive- /arch:AVX2)
    # clang-cl has only a conforming preprocessor and rejects the switch as an
    # unused argument, which /WX makes fatal.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(package_consumer PRIVATE
            /Zc:preprocessor /Zc:__cplusplus)
    endif()
else()
    target_compile_options(package_consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_compile_options(package_consumer PRIVATE -mavx2 -mfma)
    endif()
endif()

enable_testing()
add_test(NAME package_consumer COMMAND package_consumer)
