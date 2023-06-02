#
# Copyright (C) 2018-2023 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

set(NEO_OCL_VERSION_MAJOR 23)
set(NEO_OCL_VERSION_MINOR 22)

if(NOT DEFINED NEO_VERSION_BUILD)
  set(NEO_VERSION_BUILD 026516)
  set(NEO_REVISION 026516)
else()
  set(NEO_REVISION NEO_VERSION_BUILD)
endif()

# OpenCL pacakge version
set(NEO_OCL_DRIVER_VERSION "${NEO_OCL_VERSION_MAJOR}.${NEO_OCL_VERSION_MINOR}.${NEO_VERSION_BUILD}")

# Level-Zero package version
set(NEO_L0_VERSION_MAJOR 1)
set(NEO_L0_VERSION_MINOR 3)
