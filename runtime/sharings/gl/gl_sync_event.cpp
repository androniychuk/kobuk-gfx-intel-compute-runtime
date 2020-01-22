/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/sharings/gl/gl_sync_event.h"

#include "public/cl_gl_private_intel.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/context/context.h"
#include "runtime/device/device.h"
#include "runtime/event/async_events_handler.h"
#include "runtime/event/event_builder.h"
#include "runtime/helpers/get_info.h"
#include "runtime/helpers/timestamp_packet.h"
#include "runtime/platform/platform.h"
#include "runtime/sharings/gl/gl_sharing.h"

namespace NEO {
GlSyncEvent::GlSyncEvent(Context &context, const GL_CL_SYNC_INFO &sync)
    : Event(&context, nullptr, CL_COMMAND_GL_FENCE_SYNC_OBJECT_KHR, eventNotReady, eventNotReady),
      glSync(std::make_unique<GL_CL_SYNC_INFO>(sync)) {
    transitionExecutionStatus(CL_SUBMITTED);
}

GlSyncEvent::~GlSyncEvent() { ctx->getSharing<GLSharingFunctions>()->releaseSync(glSync->pSync); }

GlSyncEvent *GlSyncEvent::create(Context &context, cl_GLsync sync, cl_int *errCode) {
    GLContextGuard guard(*context.getSharing<GLSharingFunctions>());

    ErrorCodeHelper err(errCode, CL_SUCCESS);
    GL_CL_SYNC_INFO syncInfo = {sync, nullptr};

    context.getSharing<GLSharingFunctions>()->retainSync(&syncInfo);
    DEBUG_BREAK_IF(!syncInfo.pSync);

    EventBuilder eventBuilder;
    eventBuilder.create<GlSyncEvent>(context, syncInfo);
    return static_cast<GlSyncEvent *>(eventBuilder.finalizeAndRelease());
}

void GlSyncEvent::updateExecutionStatus() {
    GLContextGuard guard(*ctx->getSharing<GLSharingFunctions>());
    int retVal = 0;

    ctx->getSharing<GLSharingFunctions>()->getSynciv(glSync->pSync, GL_SYNC_STATUS, &retVal);
    if (retVal == GL_SIGNALED) {
        setStatus(CL_COMPLETE);
    }
}

uint32_t GlSyncEvent::getTaskLevel() {
    if (peekExecutionStatus() == CL_COMPLETE) {
        return 0;
    }
    return Event::eventNotReady;
}
} // namespace NEO
