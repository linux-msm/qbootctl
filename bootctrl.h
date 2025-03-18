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
/*
 * Copyright (C) 2023 Caleb Connolly <caleb@connolly.tech>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BOOTCTRL_H__
#define __BOOTCTRL_H__

#include <stdbool.h>

struct slot_info {
	bool active;
	bool bootable;
	bool successful;
};

struct boot_control_module {
	/*
	* (*getCurrentSlot)() returns the value letting the system know
	* whether the current slot is A or B. The meaning of A and B is
	* left up to the implementer. It is assumed that if the current slot
	* is A, then the block devices underlying B can be accessed directly
	* without any risk of corruption.
	* The returned value is always guaranteed to be strictly less than the
	* value returned by getNumberSlots. Slots start at 0 and
	* finish at getNumberSlots() - 1
	* Returns -ENOENT on devices with no slots.
	*/
	int (*getCurrentSlot)();

	/*
	* (*markBootSuccessful)() marks the specified slot
	* as boot successful
	*
	* Returns 0 on success, -errno on error.
	*/
	int (*markBootSuccessful)(unsigned slot);

	/*
	* (*setActiveBootSlot)() marks the slot passed in parameter as
	* the active boot slot (see getCurrentSlot for an explanation
	* of the "slot" parameter). This overrides any previous call to
	* setSlotAsUnbootable.
	* Returns 0 on success, -errno on error.
	*/
	int (*setActiveBootSlot)(unsigned slot, bool ignore_missing_bsg);

	/*
	* (*setSlotAsUnbootable)() marks the slot passed in parameter as
	* an unbootable. This can be used while updating the contents of the slot's
	* partitions, so that the system will not attempt to boot a known bad set up.
	* Returns 0 on success, -errno on error.
	*/
	int (*setSlotAsUnbootable)(unsigned slot);

	/*
	* (*isSlotBootable)() returns if the slot passed in parameter is
	* bootable. Note that slots can be made unbootable by both the
	* bootloader and by the OS using setSlotAsUnbootable.
	* Returns 1 if the slot is bootable, 0 if it's not, and -errno on
	* error.
	*/
	int (*isSlotBootable)(unsigned slot);

	/*
	* (*getSuffix)() returns the string suffix used by partitions that
	* correspond to the slot number passed in parameter. The returned string
	* is expected to be statically allocated and not need to be freed.
	* Returns NULL if slot does not match an existing slot.
	*/
	const char *(*getSuffix)(unsigned slot);

	/*
	* (*isSlotMarkedSucessful)() returns if the slot passed in parameter has
	* been marked as successful using markBootSuccessful.
	* Returns 1 if the slot has been marked as successful, 0 if it's
	* not the case, and -errno on error.
	*/
	int (*isSlotMarkedSuccessful)(unsigned slot);

	/**
	* Returns the active slot to boot into on the next boot. If
	* setActiveBootSlot() has been called, the getter function should return
	* the same slot as the one provided in the last setActiveBootSlot() call.
	*/
	unsigned (*getActiveBootSlot)();
};

extern const struct boot_control_module bootctl;

#endif // __BOOTCTRL_H__
