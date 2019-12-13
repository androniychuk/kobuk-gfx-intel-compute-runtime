/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#ifdef SUPPORT_GEN8
#include "runtime/gen8/hw_cmds.h"
#endif
#ifdef SUPPORT_GEN9
#include "runtime/gen9/hw_cmds.h"
#endif
#ifdef SUPPORT_GEN11
#include "runtime/gen11/hw_cmds.h"
#endif
#ifdef SUPPORT_GEN12LP
#include "runtime/gen12lp/hw_cmds.h"
#endif
