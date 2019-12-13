/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdint.h>
#include <string>
#include <thread>

enum class DebugFunctionalityLevel {
    None,   // Debug functionality disabled
    Full,   // Debug functionality fully enabled
    RegKeys // Only registry key reads enabled
};

#if defined(_DEBUG)
constexpr DebugFunctionalityLevel globalDebugFunctionalityLevel = DebugFunctionalityLevel::Full;
#elif defined(_RELEASE_INTERNAL) || defined(_RELEASE_BUILD_WITH_REGKEYS)
constexpr DebugFunctionalityLevel globalDebugFunctionalityLevel = DebugFunctionalityLevel::RegKeys;
#else
constexpr DebugFunctionalityLevel globalDebugFunctionalityLevel = DebugFunctionalityLevel::None;
#endif

namespace NEO {
template <typename... Args>
void printDebugString(bool showDebugLogs, Args &&... args) {
    if (showDebugLogs) {
        fprintf(std::forward<Args>(args)...);
    }
}
#if defined(__clang__)
#define NO_SANITIZE __attribute__((no_sanitize("undefined")))
#else
#define NO_SANITIZE
#endif

class Kernel;
class GraphicsAllocation;
struct MultiDispatchInfo;
class SettingsReader;

template <typename T>
struct DebugVarBase {
    DebugVarBase(const T &defaultValue) : value(defaultValue) {}
    T get() const {
        return value;
    }
    void set(T data) {
        value = std::move(data);
    }
    T &getRef() {
        return value;
    }

  private:
    T value;
};

struct DebugVariables {
#define DECLARE_DEBUG_VARIABLE(dataType, variableName, defaultValue, description) \
    DebugVarBase<dataType> variableName{defaultValue};
#include "debug_variables.inl"
#undef DECLARE_DEBUG_VARIABLE
};

template <DebugFunctionalityLevel DebugLevel>
class DebugSettingsManager {
  public:
    DebugSettingsManager();
    ~DebugSettingsManager();

    DebugSettingsManager(const DebugSettingsManager &) = delete;
    DebugSettingsManager &operator=(const DebugSettingsManager &) = delete;

    static constexpr bool debugLoggingAvailable() {
        return DebugLevel == DebugFunctionalityLevel::Full;
    }

    static constexpr bool debugKernelDumpingAvailable() {
        return DebugLevel == DebugFunctionalityLevel::Full;
    }

    static constexpr bool kernelArgDumpingAvailable() {
        return DebugLevel == DebugFunctionalityLevel::Full;
    }

    static constexpr bool registryReadAvailable() {
        return (DebugLevel == DebugFunctionalityLevel::Full) || (DebugLevel == DebugFunctionalityLevel::RegKeys);
    }

    static constexpr bool disabled() {
        return DebugLevel == DebugFunctionalityLevel::None;
    }

    void getHardwareInfoOverride(std::string &hwInfoConfig);
    void dumpKernel(const std::string &name, const std::string &src);
    void logApiCall(const char *function, bool enter, int32_t errorCode);
    void logAllocation(GraphicsAllocation const *graphicsAllocation);
    size_t getInput(const size_t *input, int32_t index);
    const std::string getEvents(const uintptr_t *input, uint32_t numOfEvents);
    const std::string getMemObjects(const uintptr_t *input, uint32_t numOfObjects);

    MOCKABLE_VIRTUAL void writeToFile(std::string filename, const char *str, size_t length, std::ios_base::openmode mode);

    void dumpBinaryProgram(int32_t numDevices, const size_t *lengths, const unsigned char **binaries);
    void dumpKernelArgs(const Kernel *kernel);
    void dumpKernelArgs(const MultiDispatchInfo *multiDispatchInfo);
    void injectSettingsFromReader();

    const std::string getSizes(const uintptr_t *input, uint32_t workDim, bool local) {
        if (false == debugLoggingAvailable()) {
            return "";
        }

        std::stringstream os;
        std::string workSize;
        if (local) {
            workSize = "localWorkSize";
        } else {
            workSize = "globalWorkSize";
        }

        for (uint32_t i = 0; i < workDim; i++) {
            if (input != nullptr) {
                os << workSize << "[" << i << "]: \t" << input[i] << "\n";
            }
        }
        return os.str();
    }

    const std::string infoPointerToString(const void *paramValue, size_t paramSize) {
        if (false == debugLoggingAvailable()) {
            return "";
        }

        std::stringstream os;
        if (paramValue) {
            switch (paramSize) {
            case sizeof(uint32_t):
                os << *(uint32_t *)paramValue;
                break;
            case sizeof(uint64_t):
                os << *(uint64_t *)paramValue;
                break;
            case sizeof(uint8_t):
                os << (uint32_t)(*(uint8_t *)paramValue);
                break;
            default:
                break;
            }
        }
        return os.str();
    }

    // Expects pairs of args (even number of args)
    template <typename... Types>
    void logInputs(Types &&... params) {
        if (debugLoggingAvailable()) {
            if (this->flags.LogApiCalls.get()) {
                std::unique_lock<std::mutex> theLock(mtx);
                std::thread::id thisThread = std::this_thread::get_id();
                std::stringstream ss;
                ss << "------------------------------\n";
                printInputs(ss, "ThreadID", thisThread, params...);
                ss << "------------------------------" << std::endl;
                writeToFile(logFileName, ss.str().c_str(), ss.str().length(), std::ios::app);
            }
        }
    }

    template <typename FT>
    void logLazyEvaluateArgs(bool predicate, FT &&callable) {
        if (debugLoggingAvailable()) {
            if (predicate) {
                callable();
            }
        }
    }

    template <typename... Types>
    void log(bool enableLog, Types... params) {
        if (debugLoggingAvailable()) {
            if (enableLog) {
                std::unique_lock<std::mutex> theLock(mtx);
                std::thread::id thisThread = std::this_thread::get_id();
                std::stringstream ss;
                print(ss, "ThreadID", thisThread, params...);
                writeToFile(logFileName, ss.str().c_str(), ss.str().length(), std::ios::app);
            }
        }
    }

    DebugVariables flags;
    void *injectFcn = nullptr;

    const char *getLogFileName() {
        return logFileName.c_str();
    }

    void setLogFileName(std::string filename) {
        logFileName = filename;
    }
    void setReaderImpl(SettingsReader *newReaderImpl) {
        readerImpl.reset(newReaderImpl);
    }
    SettingsReader *getReaderImpl() {
        return readerImpl.get();
    }

    const char *getAllocationTypeString(GraphicsAllocation const *graphicsAllocation);

  protected:
    std::unique_ptr<SettingsReader> readerImpl;
    std::mutex mtx;
    std::string logFileName;

    // Required for variadic template with 0 args passed
    void printInputs(std::stringstream &ss) {}

    // Prints inputs in format: InputName: InputValue \newline
    template <typename T1, typename... Types>
    void printInputs(std::stringstream &ss, T1 first, Types... params) {
        if (debugLoggingAvailable()) {
            const size_t argsLeft = sizeof...(params);

            ss << "\t" << first;
            if (argsLeft % 2) {
                ss << ": ";
            } else {
                ss << std::endl;
            }
            printInputs(ss, params...);
        }
    }

    // Required for variadic template with 0 args passed
    void print(std::stringstream &ss) {}

    template <typename T1, typename... Types>
    void print(std::stringstream &ss, T1 first, Types... params) {
        if (debugLoggingAvailable()) {
            const size_t argsLeft = sizeof...(params);

            ss << first << " ";
            if (argsLeft == 0) {
                ss << std::endl;
            }
            print(ss, params...);
        }
    }

    template <typename DataType>
    static void dumpNonDefaultFlag(const char *variableName, const DataType &variableValue, const DataType &defaultValue);
    void dumpFlags() const;
    static const char *settingsDumpFileName;
};

extern DebugSettingsManager<globalDebugFunctionalityLevel> DebugManager;

template <DebugFunctionalityLevel DebugLevel>
const char *DebugSettingsManager<DebugLevel>::settingsDumpFileName = "igdrcl_dumped.config";

template <bool Enabled>
class DebugSettingsApiEnterWrapper {
  public:
    DebugSettingsApiEnterWrapper(const char *funcName, const int *errorCode)
        : funcName(funcName), errorCode(errorCode) {
        if (Enabled) {
            DebugManager.logApiCall(funcName, true, 0);
        }
    }
    ~DebugSettingsApiEnterWrapper() {
        if (Enabled) {
            DebugManager.logApiCall(funcName, false, (errorCode != nullptr) ? *errorCode : 0);
        }
    }
    const char *funcName;
    const int *errorCode;
};
}; // namespace NEO

#define DBG_LOG_LAZY_EVALUATE_ARGS(DBG_MANAGER, PREDICATE, LOG_FUNCTION, ...) \
    DBG_MANAGER.logLazyEvaluateArgs(DBG_MANAGER.flags.PREDICATE.get(), [&] { DBG_MANAGER.LOG_FUNCTION(__VA_ARGS__); })

#define DBG_LOG(PREDICATE, ...) \
    DBG_LOG_LAZY_EVALUATE_ARGS(NEO::DebugManager, PREDICATE, log, NEO::DebugManager.flags.PREDICATE.get(), __VA_ARGS__)

#define DBG_LOG_INPUTS(...) \
    DBG_LOG_LAZY_EVALUATE_ARGS(NEO::DebugManager, LogApiCalls, logInputs, __VA_ARGS__)
