// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MikroTik RouterBoot hard config tag based
 * setting of DT mac-address nodes.
 *
 * Copyright (C) 2021 John Thomson <git@johnthomson.fastmail.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>

int set_macaddrs(u8 *base_macaddr, u32 macaddr_count,
			 const struct device_node *hc_of_node)
{
	struct device_node *assignments_node, *assignment_node, *ether_node;
	struct property *prop;
	const char *node_name;
	u8 *next_macaddr;
	u64 tmp_macaddr;
	int err, i;
	u32 reg;

	pr_info("rb_macaddrs: base_macaddr: %pM\n", base_macaddr);
	if (! is_valid_ether_addr(base_macaddr)) {
		pr_err("rb_macaddrs: hardconfig base_macaddr is invalid\n");
		return -EINVAL;
	}

	assignments_node = of_get_child_by_name(hc_of_node, "mac-address-assignments");
	if (!assignments_node) {
		pr_warn("rb_macaddrs: could not find mac-address-assignments hard_config DT node\n");
		return -EINVAL;
	}

	if (!macaddr_count) {
		pr_err("rb_macaddrs: no hardconfig macaddr_count\n");
		return -EINVAL;
	}

	for_each_available_child_of_node(assignments_node, assignment_node) {
		err = of_property_read_u32(assignment_node, "reg", &reg);
			if (err) {
				of_node_put(assignment_node);
				of_node_put(assignments_node);
				pr_err("rb_macaddrs: mac-address-assignments child node missing reg)\n");
				return -EINVAL;
			} else
				pr_debug("rb_macaddrs: next_macaddr increment: 0x%x\n", reg);

		if (reg > macaddr_count) {
			pr_err("rb_macaddrs: mac address incremented higher than allocated in hard_cfg\n");
			of_node_put(assignment_node);
			continue;
		}

		tmp_macaddr = ether_addr_to_u64(base_macaddr);
		tmp_macaddr = tmp_macaddr + reg;

		next_macaddr = kmalloc(ETH_ALEN, GFP_KERNEL);
		if (!next_macaddr) {
			pr_err("rb_macaddrs: kmalloc MAC address failed\n");
			return -ENOMEM;
		}
		u64_to_ether_addr(tmp_macaddr, next_macaddr);

		pr_debug("rb_macaddrs: next_macaddr %pM\n", next_macaddr);
		if (! is_valid_ether_addr(next_macaddr)) {
			pr_err("rb_macaddrs: next_macaddr is invalid\n");
			of_node_put(assignment_node);
			kfree(next_macaddr);
			continue;
		}

		for (i = 0; ; i++) {
			ether_node = of_parse_phandle(assignment_node, "assign-to", i);
			if (!ether_node)
				break;

			prop = kzalloc(sizeof(struct property), GFP_KERNEL);
			if (!prop) {
				pr_err("rb_macaddrs: kzalloc OF properties failed\n");
				return -ENOMEM;
			}

			prop->name = "mac-address";
			prop->value = next_macaddr;
			prop->length = ETH_ALEN;

			if (of_add_property(ether_node,prop) != 0) {
				pr_warn("rb_macaddrs: of_add_property mac-address failed\n");
				continue;
			}
			node_name = kbasename(ether_node->full_name);
			pr_info("rb_macaddrs: ethernet node: %s, set mac-address %pM\n",
				node_name, prop->value);
			of_node_put(ether_node);
		}
		if (i == 0) {
			pr_err("rb_macaddrs: missing assign-to array of phandles\n");
			continue;
		}
		of_node_put(assignment_node);
	of_node_put(assignments_node);
	}
	return 0;
}
