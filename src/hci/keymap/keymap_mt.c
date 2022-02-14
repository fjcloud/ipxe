/** @file
 *
 * "mt" keyboard mapping
 *
 * This file is automatically generated; do not edit
 *
 */

FILE_LICENCE ( PUBLIC_DOMAIN );

#include <ipxe/keymap.h>

/** "mt" basic remapping */
static struct keymap_key mt_basic[] = {
	{ 0x1c, 0x1e },	/* 0x1c => 0x1e */
	{ 0x22, 0x40 },	/* '"' => '@' */
	{ 0x40, 0x22 },	/* '@' => '"' */
	{ 0x5c, 0x23 },	/* '\\' => '#' */
	{ 0x7c, 0x7e },	/* '|' => '~' */
	{ 0, 0 }
};

/** "mt" keyboard map */
struct keymap mt_keymap __keymap = {
	.name = "mt",
	.basic = mt_basic,
};
