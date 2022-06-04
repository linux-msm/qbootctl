/*
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

#include <map>
#include <list>
#include <string>
#include <vector>
#include <errno.h>
#include <regex>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "bootctrl.h"

const struct boot_control_module *impl = &bootctl;

bool isslot(const char* str)
{
	return strspn(str, "01abAB") == strlen(str);
}

bool isslotnum(const char* str)
{
	return strspn(str, "01") == strlen(str);
}

unsigned parseSlot(const char* arg)
{
	char *end;
	int slot;
	if (!isslot(arg)) {
		goto fail;
	}
	if (isslotnum(arg)) {
		slot = (int)strtol(arg, &end, 10);
		if (end == arg)
			goto fail;
	} else {
		switch (arg[0]) {
		case 'a':
		case 'A':
			slot = 0;
			break;
		case 'b':
		case 'B':
			slot = 1;
			break;
		default:
			goto fail;
		}
	}

	return (unsigned)slot;

fail:
	fprintf(stderr,
		"Expected slot not '%s'\n",
		arg);
	exit(1);
}

int usage()
{
	fprintf(stderr, "qbootctl: qcom bootctrl HAL port for Linux\n");
	fprintf(stderr, "-------------------------------------------\n");
	fprintf(stderr, "qbootctl [-c|-m|-s|-u|-b|-n|-x] [SLOT]\n\n");
	fprintf(stderr, "    <no args>        dump slot info (default)\n");
	fprintf(stderr, "    -h               this help text\n");
	fprintf(stderr, "    -c               get the current slot\n");
	fprintf(stderr, "    -a               get the active slot\n");
	fprintf(stderr,
		"    -b SLOT          check if SLOT is marked as bootable\n");
	fprintf(stderr,
		"    -n SLOT          check if SLOT is marked as successful\n");
	fprintf(stderr,
		"    -x [SLOT]        get the slot suffix for SLOT (default: current)\n");
	fprintf(stderr, "    -s SLOT          set to active slot to SLOT\n");
	fprintf(stderr,
		"    -m [SLOT]        mark a boot as successful (default: current)\n");
	fprintf(stderr,
		"    -u [SLOT]        mark SLOT as unbootable (default: current)\n");

	return 1;
}

int get_slot_info(struct slot_info *slots)
{
	int rc;
	uint32_t active_slot = impl->getActiveBootSlot();

	slots[active_slot].active = true;

	for (size_t i = 0; i < 2; i++) {
		rc = impl->isSlotMarkedSuccessful(i);
		if (rc < 0)
			return rc;
		slots[i].successful = rc;
		rc = impl->isSlotBootable(i);
		if (rc < 0)
			return rc;
		slots[i].bootable = rc;
	}

	return 0;
}

void dump_info()
{
	struct slot_info slots[2] = { { 0 } };
	int current_slot = impl->getCurrentSlot();

	get_slot_info(slots);

	printf("Current slot: %s\n",
	       current_slot >= 0 ? impl->getSuffix(current_slot) : "N/A");
	for (size_t i = 0; i < 2; i++) {
		printf("SLOT %s:\n", impl->getSuffix(i));
		printf("\tActive      : %d\n", slots[i].active);
		printf("\tSuccessful  : %d\n", slots[i].successful);
		printf("\tBootable    : %d\n", slots[i].bootable);
	}
}

int main(int argc, char **argv)
{
	int optflag;
	int slot = -1;
	int rc;

	const char* IS_TEST = getenv("QBOOTCTL_TEST");
	if (IS_TEST) {
		impl = &bootctl_test;
	}

	if(geteuid() != 0) {
		fprintf(stderr, "This program must be run as root!\n");
		return 1;
	}

	switch (argc) {
	case 1:
		dump_info();
		return 0;
	case 2:
		break;
	case 3:
		slot = parseSlot(argv[2]);
		break;
	default:
		return usage();
	}

	if (slot < 0)
		slot = impl->getCurrentSlot();

	optflag = getopt(argc, argv, "hcmas:ub:n:x");

	switch (optflag) {
	case 'c':
		slot = impl->getCurrentSlot();
		printf("Current slot: %s\n", impl->getSuffix(slot));
		return 0;
	case 'a':
		slot = impl->getActiveBootSlot();
		printf("Active slot: %s\n", impl->getSuffix(slot));
		return 0;
	case 'b':
		printf("SLOT %s: is %smarked bootable\n", impl->getSuffix(slot),
		       impl->isSlotBootable(slot) == 1 ? "" : "not ");
		return 0;
	case 'n':
		printf("SLOT %s: is %smarked successful\n",
		       impl->getSuffix(slot),
		       impl->isSlotMarkedSuccessful(slot) == 1 ? "" : "not ");
		return 0;
	case 'x':
		printf("%s\n", impl->getSuffix(slot));
		return 0;
	case 's':
		rc = impl->setActiveBootSlot(slot);
		if (rc < 0) {
			fprintf(stderr, "SLOT %s: Failed to set active\n",
				impl->getSuffix(slot));
			return 1;
		}
		printf("SLOT %d: Set as active slot\n", slot);
		return 0;
	case 'm':
		rc = impl->markBootSuccessful(slot);
		if (rc < 0)
			return 1;
		printf("SLOT %s: Marked boot successful\n",
		       impl->getSuffix(slot));
		return 0;
	case 'u':
		rc = impl->setSlotAsUnbootable(slot);
		if (rc < 0) {
			fprintf(stderr,
				"SLOT %s: Failed to set as unbootable\n",
				impl->getSuffix(slot));
			return 1;
		}
		printf("SLOT %s: Set as unbootable\n", impl->getSuffix(slot));
		return 0;
	case 'h':
	default:
		usage();
		return 0;
	}

	return 0;
}
