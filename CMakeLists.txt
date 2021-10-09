cmake_minimum_required(VERSION 3.10)

# set the project name
project(qbootctl)

set(BootCtrl_Files BootControl.cpp
        boot_control_impl_qcom.cpp
        gpt-utils.cpp)

include_directories(include)

# add the executable
add_executable(qbootctl ${BootCtrl_Files})

target_link_libraries(qbootctl libz.so)