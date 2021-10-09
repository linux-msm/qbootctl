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
#ifndef ANDROID_HARDWARE_BOOT_V1_0_BOOTCONTROL_H
#define ANDROID_HARDWARE_BOOT_V1_0_BOOTCONTROL_H

/**
 * A result encapsulating whether a function returned true, false or
 * failed due to an invalid slot number
 */
enum class BoolResult : int {
    FALSE = 0,
    TRUE = 1,
    INVALID_SLOT = -1 /* -1 */,
};

struct BootControl {
    BootControl(boot_control_module* module);
    // Methods from ::android::hardware::boot::V1_0::IBootControl follow.
    unsigned int getNumberSlots();
    unsigned int getCurrentSlot();
    int markBootSuccessful();
    int setActiveBootSlot(unsigned int slot);
    int setSlotAsUnbootable(unsigned int slot);
    BoolResult isSlotBootable(unsigned int slot);
    BoolResult isSlotMarkedSuccessful(unsigned int slot);
    std::string getSuffix(unsigned int slot);
private:
    boot_control_module* mModule;
};


#endif  // ANDROID_HARDWARE_BOOT_V1_0_BOOTCONTROL_H
