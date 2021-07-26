// SPDX-License-Identifier: GPL-2.0-only
/*
 * Parser for MikroTik RouterBoot config tag data partitions.
 *
 * Copyright (C) 2021 John Thomson <git@johnthomson.fastmail.com.au>
 *
 * Based on OpenWrt Mikrotik RouterBoot parser & platform cfgtag sysfs driver
 * by Thibaut VARÈNE, and OpenWrt 19.07 rbcfg.h by Gabor Juhos
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/libfdt_env.h>
#include <linux/string.h>

/*
 * Tag magic is stored CPU-endian on SPI-NOR, and starts at an offset % 4K = 0
 */
#define RB_MAGIC_HARD	(('H') | ('a' << 8) | ('r' << 16) | ('d' << 24))
#define RB_MAGIC_SOFT	(('S') | ('o' << 8) | ('f' << 16) | ('t' << 24))

#define RBTAG_ID_MAX	0x30
#define RBTAG_LEN_MAX	0x1000
#define RBTAG_LEN_MIN	0x4
#define RBTAG_MAX_COUNT	30

#define RBTAG_PR_PFX	"[rbcfgtag] "

#define RBTAG_MAX_TAG_NAME_LEN	12

struct routerboot_cfgtag {
	size_t offset;
	u16 tag_id;
	u16 tag_len;
	char tag_name[RBTAG_MAX_TAG_NAME_LEN];
	struct device_node *of_node;
};

/* scan through an MTD partition to find RouterBOOT CFG tags.
 * offset 0 must be CFG tags start (example: Hard or Soft).
 */
static int routerboot_find_cfgtags(struct mtd_info *master,
					struct routerboot_cfgtag *cfgtags)
{
	size_t bytes_read, offset;
	u32 buf, tag_count, i;
	u16 tag_id, tag_len;
	char cfg_type[5];
	int err;

	offset = 0;
	tag_count = 0;
	while (offset < master->size) {
		err = mtd_read(master, offset, sizeof(buf), &bytes_read, (u8 *)&buf);
		if (err) {
			pr_err(RBTAG_PR_PFX "%s: mtd_read error while parsing (offset: 0x%X): %d\n",
			       master->name, offset, err);
			return -EINVAL;
		}

		/* At start of cfgtags partition, check and skip known magic */
		if (offset == 0) {
			if (buf == RB_MAGIC_HARD) {
				snprintf(cfg_type, sizeof(cfg_type), "hard");
				offset += sizeof(buf);
				continue;
			} else if (buf == RB_MAGIC_SOFT) {
				snprintf(cfg_type, sizeof(cfg_type), "soft");
				/* skip Soft CRC32 in addition */
				offset += sizeof(buf) + sizeof(u32);
				continue;
			} else {
				pr_err(RBTAG_PR_PFX "%s does not start with known magic\n",
						master->name);
				return -EINVAL;
			}
		}

		/* check array is not overflow */
		if (tag_count > RBTAG_MAX_COUNT) {
			pr_warn(RBTAG_PR_PFX "more tags found than expected\n");
		}

		/* rb config tag is stored CPU endian */
		tag_id = buf & 0xFFFF;
		tag_len = buf >> 0x10;

		pr_debug(RBTAG_PR_PFX "tag at 0x%X: tag 0x%X len 0x%X\n",
					offset, tag_id, tag_len);

		if (tag_id > RBTAG_ID_MAX || \
				tag_len < RBTAG_LEN_MIN || \
				tag_len > RBTAG_LEN_MAX || \
				(tag_len % RBTAG_LEN_MIN) != 0) {
			pr_debug(RBTAG_PR_PFX "invalid tag found at 0x%X\n",
					offset);
			break;
		}

		if ((tag_len + offset) > master->size) {
			pr_warn(RBTAG_PR_PFX "tag overflows partition at 0x%X\n",
						offset);
			break;
		}

		for (i = 0; i < tag_count; i++) {
			if (cfgtags[i].tag_id == tag_id) {
				pr_warn(RBTAG_PR_PFX "repeated tag ID at 0x%X\n",
						offset);
			break;
			}
		}

		cfgtags[tag_count].offset = offset + sizeof(buf);
		cfgtags[tag_count].tag_id = tag_id;
		cfgtags[tag_count].tag_len = tag_len;
		snprintf(cfgtags[tag_count].tag_name, RBTAG_MAX_TAG_NAME_LEN, "%s_tag_%02d", cfg_type, tag_id);

		offset += sizeof(buf) + tag_len;
		tag_count += 1;
	}

	return tag_count;
}

static int routerboot_partitions_parse(struct mtd_info *master,
				       const struct mtd_partition **pparts,
				       struct mtd_part_parser_data *data)
{
	struct device_node *mtd_node;
	struct device_node *pp;
	struct routerboot_cfgtag *cfgtags;
	struct mtd_partition *parts;
	char *tag_name;
	int nr_parts, nr_tags, i, i_part;
	bool all_tags;

	/* Pull mtd_node from the master device node */
	mtd_node = mtd_get_of_node(master);
	if (!mtd_node)
		return 0;

	cfgtags = kcalloc(RBTAG_MAX_COUNT, sizeof(*cfgtags), GFP_KERNEL);
	if (!cfgtags)
		return -ENOMEM;

	nr_tags = routerboot_find_cfgtags(master, cfgtags);

	if (nr_tags < 1)
		return nr_tags;

	nr_parts = 0;
	for_each_child_of_node(mtd_node,  pp) {
		nr_parts++;
	}
	pr_debug(RBTAG_PR_PFX "%d tag nodes\n", nr_parts);
	all_tags = of_property_read_bool(mtd_node, "mikrotik,rbcfg-all-tags");
	if (all_tags)
		nr_parts = nr_tags;

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	i_part = 0;
	for (i = 0; i < nr_tags; i++) {
		for_each_child_of_node(mtd_node, pp) {
			const __be32 *reg;
			int len;
			int a_cells, s_cells;
			size_t addr, size;

			reg = of_get_property(pp, "reg", &len);
			if (!reg)
				continue;

			a_cells = of_n_addr_cells(pp);
			s_cells = of_n_size_cells(pp);

			addr = of_read_number(reg, a_cells);
			size = of_read_number(reg + a_cells, s_cells);

			if (!addr)
				continue;

			if (addr == cfgtags[i].tag_id) {
				cfgtags[i].of_node = pp;
				pr_debug(RBTAG_PR_PFX "tag %02d has OF node\n", cfgtags[i].tag_id);
				if (size && size != cfgtags[i].tag_len) {
					cfgtags[i].tag_len = size;
					pr_debug(RBTAG_PR_PFX "tag %02d size forced to 0x%X\n", cfgtags[i].tag_id, cfgtags[i].tag_len);
				}
				of_node_put(pp);
				break;
			}
			of_node_put(pp);
		}

		if (all_tags || cfgtags[i].of_node) {
			parts[i_part].offset = cfgtags[i].offset;
			parts[i_part].size = cfgtags[i].tag_len;
			tag_name = kstrdup(cfgtags[i].tag_name, GFP_KERNEL);
			parts[i_part].name = tag_name ? tag_name : "";
			if (cfgtags[i].of_node)
				parts[i_part].of_node = cfgtags[i].of_node;
			i_part++;
		}
	}

	kfree(cfgtags);

	if (!nr_parts) {
		kfree(parts);
	} else {
		*pparts = parts;
	}
	return nr_parts;
}

static const struct of_device_id parse_routerboot_cfgtag_match_table[] = {
	{ .compatible = "mikrotik,routerboot-cfgtag-partitions" },
	{},
};
MODULE_DEVICE_TABLE(of, parse_routerboot_cfgtag_match_table);

static struct mtd_part_parser routerboot_cfgtag_parser = {
	.parse_fn = routerboot_partitions_parse,
	.name = "routerboot_cfgtag",
	.of_match_table = parse_routerboot_cfgtag_match_table,
};
module_mtd_part_parser(routerboot_cfgtag_parser);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MTD partitioning for RouterBoot config tags");
MODULE_AUTHOR("John Thomson");
