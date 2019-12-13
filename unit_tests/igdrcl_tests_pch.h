/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/gmm_helper/gmm_lib.h"
#include "core/helpers/abort.h"
#include "core/helpers/debug_helpers.h"
#include "core/helpers/ptr_math.h"
#include "core/memory_manager/memory_constants.h"
#include "core/sku_info/sku_info_base.h"
#include "runtime/gen_common/hw_cmds.h"
#include "runtime/helpers/completion_stamp.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/helpers/kmd_notify_properties.h"
#include "test.h"
#include "unit_tests/gen_common/gen_cmd_parse.h"

#include "third_party/opencl_headers/CL/cl.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdio.h>
#include <string>
#include <vector>
