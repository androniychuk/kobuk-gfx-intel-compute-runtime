/*
 * Copyright (C) 2019-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/command_stream/queue_throttle.h"
#include "shared/source/command_stream/task_count_helper.h"
#include "shared/source/helpers/device_bitfield.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

namespace NEO {
class MemoryManager;
class CommandStreamReceiver;
class Thread;
class ProductHelper;

using SteadyClock = std::chrono::steady_clock;

struct TimeoutParams {
    std::chrono::microseconds maxTimeout;
    std::chrono::microseconds timeout;
    int timeoutDivisor;
    bool directSubmissionEnabled;
};

struct WaitForPagingFenceRequest {
    CommandStreamReceiver *csr;
    uint64_t pagingFenceValue;
};

class DirectSubmissionController {
  public:
    static constexpr size_t defaultTimeout = 5'000;
    DirectSubmissionController();
    virtual ~DirectSubmissionController();

    void setTimeoutParamsForPlatform(const ProductHelper &helper);
    void registerDirectSubmission(CommandStreamReceiver *csr);
    void unregisterDirectSubmission(CommandStreamReceiver *csr);

    void startThread();
    void startControlling();
    void stopThread();

    static bool isSupported();

    void enqueueWaitForPagingFence(CommandStreamReceiver *csr, uint64_t pagingFenceValue);
    void drainPagingFenceQueue();

  protected:
    struct DirectSubmissionState {
        DirectSubmissionState(DirectSubmissionState &&other) {
            isStopped = other.isStopped.load();
            taskCount = other.taskCount.load();
        }
        DirectSubmissionState &operator=(const DirectSubmissionState &other) {
            if (this == &other) {
                return *this;
            }
            this->isStopped = other.isStopped.load();
            this->taskCount = other.taskCount.load();
            return *this;
        }

        DirectSubmissionState() = default;
        ~DirectSubmissionState() = default;

        DirectSubmissionState(const DirectSubmissionState &other) = delete;
        DirectSubmissionState &operator=(DirectSubmissionState &&other) = delete;

        std::atomic_bool isStopped{true};
        std::atomic<TaskCountType> taskCount{0};
    };

    static void *controlDirectSubmissionsState(void *self);
    void checkNewSubmissions();
    MOCKABLE_VIRTUAL bool sleep(std::unique_lock<std::mutex> &lock);
    MOCKABLE_VIRTUAL SteadyClock::time_point getCpuTimestamp();

    void adjustTimeout(CommandStreamReceiver *csr);
    void recalculateTimeout();
    void applyTimeoutForAcLineStatusAndThrottle(bool acLineConnected);
    void updateLastSubmittedThrottle(QueueThrottle throttle);
    size_t getTimeoutParamsMapKey(QueueThrottle throttle, bool acLineStatus);

    void handlePagingFenceRequests(std::unique_lock<std::mutex> &lock, bool checkForNewSubmissions);
    MOCKABLE_VIRTUAL bool timeoutElapsed();

    uint32_t maxCcsCount = 1u;
    std::array<uint32_t, DeviceBitfield().size()> ccsCount = {};
    std::unordered_map<CommandStreamReceiver *, DirectSubmissionState> directSubmissions;
    std::mutex directSubmissionsMutex;

    std::unique_ptr<Thread> directSubmissionControllingThread;
    std::atomic_bool keepControlling = true;
    std::atomic_bool runControlling = false;

    SteadyClock::time_point timeSinceLastCheck{};
    SteadyClock::time_point lastTerminateCpuTimestamp{};
    std::chrono::microseconds maxTimeout{defaultTimeout};
    std::chrono::microseconds timeout{defaultTimeout};
    int timeoutDivisor = 1;
    std::unordered_map<size_t, TimeoutParams> timeoutParamsMap;
    QueueThrottle lowestThrottleSubmitted = QueueThrottle::HIGH;
    bool adjustTimeoutOnThrottleAndAcLineStatus = false;

    std::condition_variable condVar;
    std::mutex condVarMutex;

    std::queue<WaitForPagingFenceRequest> pagingFenceRequests;
};
} // namespace NEO