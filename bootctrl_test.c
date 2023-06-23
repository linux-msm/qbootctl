/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright (C) 2022 Caleb Connolly <caleb@connolly.tech>
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

#include <stdbool.h>
#include "bootctrl.h"

struct test_state {
	struct slot_info slots[2];
};

static struct test_state state = {
	.slots = {
	[0] = {
		.active = true,
		.bootable = true,
		.successful = false,
	},
	[1] = {
		.active = false,
		.bootable = true,
		.successful = false,
	},
	}
};

unsigned test_get_current_slot()
{
	return (unsigned int)state.slots[1].active;
}

int test_mark_boot_successful(unsigned slot)
{
	return 0;
}

int test_set_active_boot_slot(unsigned slot)
{
	return 0;
}

int test_set_slot_as_unbootable(unsigned slot)
{
	return 0;
}

int test_is_slot_bootable(unsigned slot)
{
	return 1;
}

const char *test_get_suffix(unsigned slot)
{
	switch (slot) {
	case 0:
		return "_x";
	case 1:
		return "_z";
	default:
		return "??";
	}
}

int test_is_slot_marked_successful(unsigned slot)
{
	return 1;
}

unsigned test_get_active_boot_slot()
{
	return 0;
}

const struct boot_control_module bootctl_test = {
	.getCurrentSlot = test_get_current_slot,
	.markBootSuccessful = test_mark_boot_successful,
	.setActiveBootSlot = test_set_active_boot_slot,
	.setSlotAsUnbootable = test_set_slot_as_unbootable,
	.isSlotBootable = test_is_slot_bootable,
	.getSuffix = test_get_suffix,
	.isSlotMarkedSuccessful = test_is_slot_marked_successful,
	.getActiveBootSlot = test_get_active_boot_slot,
};