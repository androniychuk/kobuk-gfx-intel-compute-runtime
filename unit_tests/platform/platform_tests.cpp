/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/device/device.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/helpers/options.h"
#include "runtime/platform/extensions.h"
#include "runtime/sharings/sharing_factory.h"
#include "unit_tests/fixtures/mock_aub_center_fixture.h"
#include "unit_tests/fixtures/platform_fixture.h"
#include "unit_tests/helpers/variable_backup.h"
#include "unit_tests/libult/create_command_stream.h"
#include "unit_tests/mocks/mock_async_event_handler.h"
#include "unit_tests/mocks/mock_builtins.h"
#include "unit_tests/mocks/mock_csr.h"
#include "unit_tests/mocks/mock_execution_environment.h"
#include "unit_tests/mocks/mock_source_level_debugger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace NEO;

struct PlatformTest : public ::testing::Test {
    void SetUp() override { pPlatform.reset(new Platform()); }
    cl_int retVal = CL_SUCCESS;
    std::unique_ptr<Platform> pPlatform;
};

struct MockPlatformWithMockExecutionEnvironment : public Platform {
    MockExecutionEnvironment *mockExecutionEnvironment = nullptr;

    MockPlatformWithMockExecutionEnvironment() {
        this->executionEnvironment->decRefInternal();
        mockExecutionEnvironment = new MockExecutionEnvironment(nullptr, false);
        executionEnvironment = mockExecutionEnvironment;
        MockAubCenterFixture::setMockAubCenter(executionEnvironment);
        executionEnvironment->incRefInternal();
    }
};

TEST_F(PlatformTest, getDevices) {
    size_t devNum = pPlatform->getNumDevices();
    EXPECT_EQ(0u, devNum);

    Device *device = pPlatform->getDevice(0);
    EXPECT_EQ(nullptr, device);

    bool ret = pPlatform->initialize();
    EXPECT_TRUE(ret);

    EXPECT_TRUE(pPlatform->isInitialized());

    devNum = pPlatform->getNumDevices();
    EXPECT_EQ(numPlatformDevices, devNum);

    device = pPlatform->getDevice(0);
    EXPECT_NE(nullptr, device);

    device = pPlatform->getDevice(numPlatformDevices);
    EXPECT_EQ(nullptr, device);

    auto allDevices = pPlatform->getDevices();
    EXPECT_NE(nullptr, allDevices);
}

TEST_F(PlatformTest, PlatformgetAsCompilerEnabledExtensionsString) {
    std::string compilerExtensions = pPlatform->peekCompilerExtensions();
    EXPECT_EQ(std::string(""), compilerExtensions);

    pPlatform->initialize();
    compilerExtensions = pPlatform->peekCompilerExtensions();

    EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string(" -cl-ext=-all,+cl")));
    if (std::string(pPlatform->getDevice(0)->getDeviceInfo().clVersion).find("OpenCL 2.1") != std::string::npos) {
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_khr_subgroups")));
    }
}

TEST_F(PlatformTest, hasAsyncEventsHandler) {
    EXPECT_NE(nullptr, pPlatform->getAsyncEventsHandler());
}

TEST_F(PlatformTest, givenMidThreadPreemptionWhenInitializingPlatformThenCallGetSipKernel) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::MidThread));

    auto builtIns = new MockBuiltins();
    pPlatform->peekExecutionEnvironment()->builtins.reset(builtIns);

    ASSERT_FALSE(builtIns->getSipKernelCalled);
    pPlatform->initialize();
    EXPECT_TRUE(builtIns->getSipKernelCalled);
    EXPECT_EQ(SipKernelType::Csr, builtIns->getSipKernelType);
}

TEST_F(PlatformTest, givenDisabledPreemptionAndNoSourceLevelDebuggerWhenInitializingPlatformThenDoNotCallGetSipKernel) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::Disabled));

    auto builtIns = new MockBuiltins();
    pPlatform->peekExecutionEnvironment()->builtins.reset(builtIns);

    ASSERT_FALSE(builtIns->getSipKernelCalled);
    pPlatform->initialize();
    EXPECT_FALSE(builtIns->getSipKernelCalled);
    EXPECT_EQ(SipKernelType::COUNT, builtIns->getSipKernelType);
}

TEST_F(PlatformTest, givenDisabledPreemptionInactiveSourceLevelDebuggerWhenInitializingPlatformThenDoNotCallGetSipKernel) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::Disabled));

    auto builtIns = new MockBuiltins();
    pPlatform->peekExecutionEnvironment()->builtins.reset(builtIns);
    auto sourceLevelDebugger = new MockSourceLevelDebugger();
    sourceLevelDebugger->setActive(false);
    pPlatform->peekExecutionEnvironment()->sourceLevelDebugger.reset(sourceLevelDebugger);

    ASSERT_FALSE(builtIns->getSipKernelCalled);
    pPlatform->initialize();
    EXPECT_FALSE(builtIns->getSipKernelCalled);
    EXPECT_EQ(SipKernelType::COUNT, builtIns->getSipKernelType);
}

TEST_F(PlatformTest, givenDisabledPreemptionActiveSourceLevelDebuggerWhenInitializingPlatformThenCallGetSipKernel) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::Disabled));

    auto builtIns = new MockBuiltins();
    pPlatform->peekExecutionEnvironment()->builtins.reset(builtIns);
    pPlatform->peekExecutionEnvironment()->sourceLevelDebugger.reset(new MockActiveSourceLevelDebugger());

    ASSERT_FALSE(builtIns->getSipKernelCalled);
    pPlatform->initialize();
    EXPECT_TRUE(builtIns->getSipKernelCalled);
    EXPECT_LE(SipKernelType::DbgCsr, builtIns->getSipKernelType);
    EXPECT_GE(SipKernelType::DbgCsrLocal, builtIns->getSipKernelType);
}

TEST(PlatformTestSimple, givenCsrHwTypeWhenPlatformIsInitializedThenInitAubCenterIsNotCalled) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.SetCommandStreamReceiver.set(0);
    MockPlatformWithMockExecutionEnvironment platform;
    bool ret = platform.initialize();
    EXPECT_TRUE(ret);
    EXPECT_FALSE(platform.mockExecutionEnvironment->initAubCenterCalled);
}

TEST(PlatformTestSimple, givenNotCsrHwTypeWhenPlatformIsInitializedThenInitAubCenterIsCalled) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.SetCommandStreamReceiver.set(1);
    VariableBackup<bool> backup(&overrideCommandStreamReceiverCreation, true);
    MockPlatformWithMockExecutionEnvironment platform;
    bool ret = platform.initialize();
    EXPECT_TRUE(ret);
    EXPECT_TRUE(platform.mockExecutionEnvironment->initAubCenterCalled);
}

TEST(PlatformTestSimple, shutdownClosesAsyncEventHandlerThread) {
    Platform *platform = new Platform;

    MockHandler *mockAsyncHandler = new MockHandler;

    auto oldHandler = platform->setAsyncEventsHandler(std::unique_ptr<AsyncEventsHandler>(mockAsyncHandler));
    EXPECT_EQ(mockAsyncHandler, platform->getAsyncEventsHandler());

    mockAsyncHandler->openThread();
    delete platform;
    EXPECT_TRUE(MockAsyncEventHandlerGlobals::destructorCalled);
}

namespace NEO {
extern CommandStreamReceiverCreateFunc commandStreamReceiverFactory[2 * IGFX_MAX_CORE];
}

CommandStreamReceiver *createMockCommandStreamReceiver(bool withAubDump, ExecutionEnvironment &executionEnvironment) {
    return nullptr;
};

class PlatformFailingTest : public PlatformTest {
  public:
    void SetUp() override {
        PlatformTest::SetUp();
        hwInfo = platformDevices[0];
        commandStreamReceiverCreateFunc = commandStreamReceiverFactory[hwInfo->platform.eRenderCoreFamily];
        commandStreamReceiverFactory[hwInfo->platform.eRenderCoreFamily] = createMockCommandStreamReceiver;
    }

    void TearDown() override {
        commandStreamReceiverFactory[hwInfo->platform.eRenderCoreFamily] = commandStreamReceiverCreateFunc;
        PlatformTest::TearDown();
    }

    VariableBackup<bool> backup{&overrideCommandStreamReceiverCreation, true};
    CommandStreamReceiverCreateFunc commandStreamReceiverCreateFunc;
    const HardwareInfo *hwInfo;
};

TEST_F(PlatformFailingTest, givenPlatformInitializationWhenIncorrectHwInfoThenInitializationFails) {
    Platform *platform = new Platform;
    bool ret = platform->initialize();
    EXPECT_FALSE(ret);
    EXPECT_FALSE(platform->isInitialized());
    delete platform;
}

TEST_F(PlatformTest, givenSupportingCl21WhenPlatformSupportsFp64ThenFillMatchingSubstringsAndMandatoryTrailingSpace) {
    const HardwareInfo *hwInfo;
    hwInfo = platformDevices[0];
    std::string extensionsList = getExtensionsList(*hwInfo);
    EXPECT_THAT(extensionsList, ::testing::HasSubstr(std::string("cl_khr_3d_image_writes")));

    std::string compilerExtensions = convertEnabledExtensionsToCompilerInternalOptions(extensionsList.c_str());
    EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string(" -cl-ext=-all,+cl")));

    if (hwInfo->capabilityTable.clVersionSupport > 20) {
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_khr_subgroups")));
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_khr_il_program")));
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_intel_spirv_device_side_avc_motion_estimation")));
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_intel_spirv_media_block_io")));
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_intel_spirv_subgroups")));
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_khr_spirv_no_integer_wrap_decoration")));
    }

    if (hwInfo->capabilityTable.ftrSupportsFP64) {
        EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("cl_khr_fp64")));
    }

    EXPECT_THAT(compilerExtensions, ::testing::EndsWith(std::string(" ")));
}

TEST_F(PlatformTest, givenNotSupportingCl21WhenPlatformNotSupportFp64ThenNotFillMatchingSubstringAndFillMandatoryTrailingSpace) {
    HardwareInfo TesthwInfo = *platformDevices[0];
    TesthwInfo.capabilityTable.ftrSupportsFP64 = false;
    TesthwInfo.capabilityTable.clVersionSupport = 10;

    std::string extensionsList = getExtensionsList(TesthwInfo);
    EXPECT_THAT(extensionsList, ::testing::HasSubstr(std::string("cl_khr_3d_image_writes")));

    std::string compilerExtensions = convertEnabledExtensionsToCompilerInternalOptions(extensionsList.c_str());
    EXPECT_THAT(compilerExtensions, ::testing::HasSubstr(std::string("-cl-ext=-all,+cl")));

    EXPECT_THAT(compilerExtensions, ::testing::Not(::testing::HasSubstr(std::string("cl_khr_fp64"))));
    EXPECT_THAT(compilerExtensions, ::testing::Not(::testing::HasSubstr(std::string("cl_khr_subgroups"))));

    EXPECT_THAT(compilerExtensions, ::testing::EndsWith(std::string(" ")));
}

TEST_F(PlatformTest, testRemoveLastSpace) {
    std::string emptyString = "";
    removeLastSpace(emptyString);
    EXPECT_EQ(std::string(""), emptyString);

    std::string xString = "x";
    removeLastSpace(xString);
    EXPECT_EQ(std::string("x"), xString);

    std::string xSpaceString = "x ";
    removeLastSpace(xSpaceString);
    EXPECT_EQ(std::string("x"), xSpaceString);
}
TEST(PlatformConstructionTest, givenPlatformConstructorWhenItIsCalledTwiceThenTheSamePlatformIsReturned) {
    platformImpl.reset();
    ASSERT_EQ(nullptr, platformImpl);
    auto platform = constructPlatform();
    EXPECT_EQ(platform, platformImpl.get());
    auto platform2 = constructPlatform();
    EXPECT_EQ(platform2, platform);
    EXPECT_NE(platform, nullptr);
}

TEST(PlatformConstructionTest, givenPlatformConstructorWhenItIsCalledAfterResetThenNewPlatformIsConstructed) {
    platformImpl.reset();
    ASSERT_EQ(nullptr, platformImpl);
    auto platform = constructPlatform();
    std::unique_ptr<Platform> temporaryOwnership(std::move(platformImpl));
    EXPECT_EQ(nullptr, platformImpl.get());
    auto platform2 = constructPlatform();
    EXPECT_NE(platform2, platform);
    EXPECT_NE(platform, nullptr);
    EXPECT_NE(platform2, nullptr);
    platformImpl.reset(nullptr);
}

TEST(PlatformConstructionTest, givenPlatformThatIsNotInitializedWhenGetDevicesIsCalledThenNullptrIsReturned) {
    Platform platform;
    auto devices = platform.getDevices();
    EXPECT_EQ(nullptr, devices);
}

TEST(PlatformInitLoopTests, givenPlatformWhenInitLoopHelperIsCalledThenItDoesNothing) {
    struct mockPlatform : public Platform {
        using Platform::initializationLoopHelper;
    };
    mockPlatform platform;
    platform.initializationLoopHelper();
}

TEST(PlatformInitLoopTests, givenPlatformWithDebugSettingWhenInitIsCalledThenItEntersEndlessLoop) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.LoopAtPlatformInitialize.set(true);
    bool called = false;
    struct mockPlatform : public Platform {
        mockPlatform(bool &called) : called(called){};
        void initializationLoopHelper() override {
            DebugManager.flags.LoopAtPlatformInitialize.set(false);
            called = true;
        }
        bool &called;
    };
    mockPlatform platform(called);
    platform.initialize();
    EXPECT_TRUE(called);
}
