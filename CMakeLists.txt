cmake_minimum_required(VERSION 3.7)
project(pintos-29)

# Uses the correct version of gcc for Mac.
# If your cross compilers aren't in this
# location, change this path to the right place.
# If you're on Linux, change this to the path of
# your normal version of gcc (probably /usr/bin/gcc).
set(CMAKE_C_COMPILER /usr/bin/gcc)

# Grab all the .c and .h files.
file(GLOB_RECURSE SOURCE_FILES
        src/*.c src/*.h
        )

# Allows us to include files from any directory in the src folder.
# Any new directories you create will need to be added here.
include_directories(
        src
        src/devices
        src/filesys
        src/lib
        src/lib/kernel
        src/lib/user
        src/threads
        src/userprog
        src/utils
)
include_directories(SYSTEM include)

# Our custom target to build everything needed for Task 0
add_custom_target(
        "Pintos"
        ALL
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/threads/
        COMMAND make
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/devices/
        COMMAND make
)

# Fake target is never actually built, but has to be here in order for
# CLion to "see" the files and index them correctly.
add_executable(fake ${SOURCE_FILES} src/vm/frame.h)
set_target_properties(fake PROPERTIES LINKER_LANGUAGE C)