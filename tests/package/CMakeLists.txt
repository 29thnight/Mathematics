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

enable_testing()
add_test(NAME package_consumer COMMAND package_consumer)
