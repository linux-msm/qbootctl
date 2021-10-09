/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>

#include "boot_control.h"
#include "BootControl.h"

BootControl::BootControl(boot_control_module *module) : mModule(module) {
}

// Methods from ::android::hardware::boot::V1_0::IBootControl follow.
unsigned int BootControl::getNumberSlots()  {
    return mModule->getNumberSlots(mModule);
}

unsigned int BootControl::getCurrentSlot()  {
    return mModule->getCurrentSlot(mModule);
}

int BootControl::markBootSuccessful()  {
    int ret = mModule->markBootSuccessful(mModule);
    return ret;
}

int BootControl::setActiveBootSlot(unsigned int slot)  {
    int ret = mModule->setActiveBootSlot(mModule, slot);
    return ret;
}

int BootControl::setSlotAsUnbootable(unsigned int slot)  {
    int ret = mModule->setSlotAsUnbootable(mModule, slot);
    return ret;
}

BoolResult BootControl::isSlotBootable(unsigned int slot)  {
    int32_t ret = mModule->isSlotBootable(mModule, slot);
    if (ret < 0) {
        return BoolResult::INVALID_SLOT;
    }
    return ret ? BoolResult::TRUE : BoolResult::FALSE;
}

BoolResult BootControl::isSlotMarkedSuccessful(unsigned int slot)  {
    int32_t ret = mModule->isSlotMarkedSuccessful(mModule, slot);
    if (ret < 0) {
        return BoolResult::INVALID_SLOT;
    }
    return ret ? BoolResult::TRUE : BoolResult::FALSE;
}

std::string BootControl::getSuffix(unsigned int slot)  {
    std::string ans;
    const char *suffix = mModule->getSuffix(mModule, slot);
    if (suffix) {
        ans = std::string(suffix);
    }
    return ans;
}

BootControl* BootControlInit() {
    boot_control_module* module;

    // For devices that don't build a standalone libhardware bootctrl impl for recovery,
    // we simulate the hw_get_module() by accessing it from the current process directly.
    const boot_control_module* hw_module = &HAL_MODULE_INFO_SYM;
    module = reinterpret_cast<boot_control_module*>(const_cast<boot_control_module*>(hw_module));
    module->init(module);
    return new BootControl(module);
}

int main (int argc, char * argv []) {
    BootControl *bootctl = BootControlInit();
    printf("======= Current slot: %d\n", bootctl->getCurrentSlot());
    printf("======= isslotbootable: a = %d, b = %d\n", bootctl->isSlotBootable(0),
    bootctl->isSlotBootable(1));
    printf("======= isSlotMarkedSuccessful: a = %d, b = %d\n", bootctl->isSlotMarkedSuccessful(0),
    bootctl->isSlotMarkedSuccessful(1));
}
