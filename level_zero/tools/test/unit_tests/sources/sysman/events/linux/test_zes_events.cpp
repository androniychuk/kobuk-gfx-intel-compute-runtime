/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/tools/source/sysman/global_operations/global_operations_imp.h"
#include "level_zero/tools/test/unit_tests/sources/sysman/linux/mock_sysman_fixture.h"

#include "mock_events.h"

using ::testing::Matcher;

namespace L0 {
namespace ult {

class SysmanEventsFixture : public SysmanDeviceFixture {
  protected:
    std::unique_ptr<Mock<EventsFsAccess>> pFsAccess;
    FsAccess *pFsAccessOriginal = nullptr;
    OsEvents *pOsEventsPrev = nullptr;
    L0::EventsImp *pEventsImp;
    GlobalOperations *pGlobalOperationsOriginal = nullptr;
    std::unique_ptr<GlobalOperationsImp> pGlobalOperations;
    std::unique_ptr<Mock<EventsSysfsAccess>> pSysfsAccess;
    SysfsAccess *pSysfsAccessOriginal = nullptr;

    void SetUp() override {
        SysmanDeviceFixture::SetUp();
        pFsAccessOriginal = pLinuxSysmanImp->pFsAccess;
        pFsAccess = std::make_unique<NiceMock<Mock<EventsFsAccess>>>();
        pLinuxSysmanImp->pFsAccess = pFsAccess.get();

        pEventsImp = static_cast<L0::EventsImp *>(pSysmanDeviceImp->pEvents);
        pOsEventsPrev = pEventsImp->pOsEvents;
        pEventsImp->pOsEvents = nullptr;
        pGlobalOperations = std::make_unique<GlobalOperationsImp>(pLinuxSysmanImp);
        pGlobalOperationsOriginal = pSysmanDeviceImp->pGlobalOperations;
        pSysmanDeviceImp->pGlobalOperations = pGlobalOperations.get();
        pSysmanDeviceImp->pGlobalOperations->init();

        pSysfsAccessOriginal = pLinuxSysmanImp->pSysfsAccess;
        pSysfsAccess = std::make_unique<NiceMock<Mock<EventsSysfsAccess>>>();
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccess.get();
        ON_CALL(*pSysfsAccess.get(), readSymLink(_, _))
            .WillByDefault(::testing::Invoke(pSysfsAccess.get(), &Mock<EventsSysfsAccess>::getValStringSymLinkSuccess));

        pEventsImp->init();
    }

    void TearDown() override {
        if (nullptr != pEventsImp->pOsEvents) {
            delete pEventsImp->pOsEvents;
        }
        pEventsImp->pOsEvents = pOsEventsPrev;
        pEventsImp = nullptr;
        pLinuxSysmanImp->pSysfsAccess = pSysfsAccessOriginal;
        pLinuxSysmanImp->pFsAccess = pFsAccessOriginal;
        pSysmanDeviceImp->pGlobalOperations = pGlobalOperationsOriginal;
        SysmanDeviceFixture::TearDown();
    }
};

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForResetRequiredEventsThenEventListenAPIReturnsAfterReceivingEventWithinTimeout) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_RESET_REQUIRED));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsOne));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(1u, numDeviceEvents);
    EXPECT_EQ(ZES_EVENT_TYPE_FLAG_DEVICE_RESET_REQUIRED, pDeviceEvents[0]);
    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForResetRequiredEventsThenEventListenAPIWaitForTimeoutIfEventNotReceived) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_RESET_REQUIRED));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsZero));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValFileNotFound));
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForCurrentlyUnsupportedEventsThenEventListenAPIWaitForTimeoutIfEventNotReceived) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_TEMP_THRESHOLD2));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsOne));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);
    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenReadSymLinkCallFailsWhenGettingPCIBDFThenEmptyPciIdPathTagReceived) {
    ON_CALL(*pSysfsAccess.get(), readSymLink(_, _))
        .WillByDefault(::testing::Invoke(pSysfsAccess.get(), &Mock<EventsSysfsAccess>::getValStringSymLinkFailure));
    PublicLinuxEventsImp linuxEventImp(pOsSysman);
    EXPECT_TRUE(linuxEventImp.pciIdPathTag.empty());
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForDeviceDetachEventsThenEventListenAPIReturnsAfterReceivingEventWithinTimeout) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_DETACH));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsOne));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(1u, numDeviceEvents);
    EXPECT_EQ(ZES_EVENT_TYPE_FLAG_DEVICE_DETACH, pDeviceEvents[0]);
    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForDeviceDetachEventsThenEventListenAPIWaitForTimeoutIfEventNotReceived) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_DETACH));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsZero));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValFileNotFound));
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForDeviceAttachEventsThenEventListenAPIReturnsAfterReceivingEventWithinTimeout) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_ATTACH));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsOne));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(1u, numDeviceEvents);
    EXPECT_EQ(ZES_EVENT_TYPE_FLAG_DEVICE_ATTACH, pDeviceEvents[0]);
    delete[] phDevices;
    delete[] pDeviceEvents;
}

TEST_F(SysmanEventsFixture, GivenValidDeviceHandleWhenListeningForDeviceAttachEventsThenEventListenAPIWaitForTimeoutIfEventNotReceived) {
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDeviceEventRegister(device->toHandle(), ZES_EVENT_TYPE_FLAG_DEVICE_ATTACH));
    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValReturnValAsZero));
    zes_device_handle_t *phDevices = new zes_device_handle_t[1];
    phDevices[0] = device->toHandle();
    uint32_t numDeviceEvents = 0;
    zes_event_type_flags_t *pDeviceEvents = new zes_event_type_flags_t[1];
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    ON_CALL(*pFsAccess.get(), read(_, Matcher<uint32_t &>(_)))
        .WillByDefault(::testing::Invoke(pFsAccess.get(), &Mock<EventsFsAccess>::getValFileNotFound));
    EXPECT_EQ(ZE_RESULT_SUCCESS, zesDriverEventListen(driverHandle->toHandle(), 100u, 1u, phDevices, &numDeviceEvents, pDeviceEvents));
    EXPECT_EQ(0u, numDeviceEvents);

    delete[] phDevices;
    delete[] pDeviceEvents;
}

} // namespace ult
} // namespace L0
