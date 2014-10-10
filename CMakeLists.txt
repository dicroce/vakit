cmake_minimum_required(VERSION 2.8)
project(vakit)

set(SOURCES source/bitstream.cpp
            source/nal_types.cpp
            source/vah264_encoder.cpp)

set(COMMON_LIBS cppkit avkit va va-drm)

include(common.cmake NO_POLICY_SCOPE)
include(../devel_artifacts/build/base_lib.cmake NO_POLICY_SCOPE)
