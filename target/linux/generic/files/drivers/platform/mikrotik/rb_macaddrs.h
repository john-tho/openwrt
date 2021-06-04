// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 John Thomson <git@johnthomson.fastmail.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */


#if defined(CONFIG_MIKROTIK_RB_MACADDRS)
extern int set_macaddrs(u8 *base_macaddr, u32 macaddr_count,
			 const struct device_node *hc_of_node);
#else /* CONFIG_MIKROTIK_RB_MACADDRS */
static inline int set_macaddrs(u8 *base_macaddr, u32 macaddr_count,
			 const struct device_node *hc_of_node)
{
	return -EINVAL;
}
#endif
