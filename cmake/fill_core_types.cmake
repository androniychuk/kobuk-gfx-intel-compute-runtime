#
# Copyright (C) 2020-2021 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

list(APPEND ALL_CORE_TYPES "GEN8")
list(APPEND ALL_CORE_TYPES "GEN9")
list(APPEND ALL_CORE_TYPES "GEN11")
list(APPEND ALL_CORE_TYPES "GEN12LP")
list(APPEND ALL_CORE_TYPES "XE_HP_CORE")
list(APPEND ALL_CORE_TYPES "XE_HPG_CORE")
list(APPEND ALL_CORE_TYPES "XE_HPC_CORE")
list(APPEND XEHP_AND_LATER_CORE_TYPES "XE_HP_CORE" "XE_HPG_CORE" "XE_HPC_CORE")
list(APPEND DG2_AND_LATER_CORE_TYPES "XE_HPG_CORE" "XE_HPC_CORE")
list(APPEND PVC_AND_LATER_CORE_TYPES "XE_HPC_CORE")
