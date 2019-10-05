/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/utilities/arrayref.h"
#include "core/utilities/stackvec.h"

#include "CL/cl.h"

#include <type_traits>

namespace NEO {

class Event;

class EventBuilder {
  public:
    template <typename EventType, typename... ArgsT>
    void create(ArgsT &&... args) {
        event = new EventType(std::forward<ArgsT>(args)...);
    }

    EventBuilder() = default;
    EventBuilder(const EventBuilder &) = delete;
    EventBuilder &operator=(const EventBuilder &) = delete;
    EventBuilder(EventBuilder &&) = delete;
    EventBuilder &operator=(EventBuilder &&) = delete;

    ~EventBuilder();

    Event *getEvent() const {
        return event;
    }

    void addParentEvent(Event &newParentEvent);
    void addParentEvent(Event *newParentEvent) {
        if (newParentEvent != nullptr) {
            addParentEvent(*newParentEvent);
        }
    }

    void addParentEvents(ArrayRef<const cl_event> newParentEvents);

    void finalize();

    Event *finalizeAndRelease() {
        finalize();
        Event *retEvent = this->event;
        this->event = nullptr;
        finalized = false;
        return retEvent;
    }

  protected:
    void clear();

    Event *event = nullptr;
    bool finalized = false;
    StackVec<Event *, 16> parentEvents;
    bool doNotRegister = false;
};
} // namespace NEO
