/*
 * Copyright (c) 2025 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

GROUP_START(SHARED_MEMORY_33)

	SECTION_PROLOGUE(.cy_sharedmem,(NOLOAD),)
	{
		. = ALIGN(4);
		__public_ram_start__ = .;
		KEEP(*(.cy_sharedmem))
		. = ALIGN(4);
		__public_ram_end__ = .;
	} GROUP_LINK_IN(SHARED_MEMORY_33)

GROUP_END(SHARED_MEMORY_33)
