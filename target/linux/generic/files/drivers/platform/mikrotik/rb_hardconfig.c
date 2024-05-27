// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MikroTik RouterBoot hard config.
 *
 * Copyright (C) 2020 Thibaut VARÈNE <hacks+kernel@slashdirt.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This driver exposes the data encoded in the "hard_config" flash segment of
 * MikroTik RouterBOARDs devices. It presents the data in a sysfs folder
 * named "hard_config". The WLAN calibration data is available on demand via
 * the 'wlan_data' sysfs file in that folder.
 *
 * This driver permanently allocates a chunk of RAM as large as the hard_config
 * MTD partition, although it is technically possible to operate entirely from
 * the MTD device without using a local buffer (except when requesting WLAN
 * calibration data), at the cost of a performance penalty.
 *
 * Note: PAGE_SIZE is assumed to be >= 4K, hence the device attribute show
 * routines need not check for output overflow.
 *
 * Some constant defines extracted from routerboot.{c,h} by Gabor Juhos
 * <juhosg@openwrt.org>
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/mtd/mtd.h>
#include <linux/sysfs.h>
#include <linux/lzo.h>

#include "rb_hardconfig.h"
#include "routerboot.h"
#include "mikrotik_wlan.h"

#define RB_HARDCONFIG_VER		"0.07"
#define RB_HC_PR_PFX			"[rb_hardconfig] "

/* Bit definitions for hardware options */
#define RB_HW_OPT_NO_UART		BIT(0)
#define RB_HW_OPT_HAS_VOLTAGE		BIT(1)
#define RB_HW_OPT_HAS_USB		BIT(2)
#define RB_HW_OPT_HAS_ATTINY		BIT(3)
#define RB_HW_OPT_PULSE_DUTY_CYCLE	BIT(9)
#define RB_HW_OPT_NO_NAND		BIT(14)
#define RB_HW_OPT_HAS_LCD		BIT(15)
#define RB_HW_OPT_HAS_POE_OUT		BIT(16)
#define RB_HW_OPT_HAS_uSD		BIT(17)
#define RB_HW_OPT_HAS_SIM		BIT(18)
#define RB_HW_OPT_HAS_SFP		BIT(20)
#define RB_HW_OPT_HAS_WIFI		BIT(21)
#define RB_HW_OPT_HAS_TS_FOR_ADC	BIT(22)
#define RB_HW_OPT_HAS_PLC		BIT(29)

/*
 * Tag ID values for ERD data.
 * Mikrotik used to pack all calibration data under a single tag id 0x1, but
 * recently switched to a new scheme where each radio calibration gets a
 * separate tag. The new scheme has tag id bit 15 always set and seems to be
 * mutually exclusive with the old scheme.
 */
#define RB_WLAN_ERD_ID_SOLO		0x0001
#define RB_WLAN_ERD_ID_MULTI_8001	0x8001
#define RB_WLAN_ERD_ID_MULTI_8201	0x8201

static struct kobject *hc_kobj;
static u8 *hc_buf;		// ro buffer after init(): no locking required
static size_t hc_buflen;

/* Array of known hw_options bits with human-friendly parsing */
static struct hc_hwopt {
	const u32 bit;
	const char *str;
} const hc_hwopts[] = {
	{
		.bit = RB_HW_OPT_NO_UART,
		.str = "no UART\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_VOLTAGE,
		.str = "has Vreg\t",
	}, {
		.bit = RB_HW_OPT_HAS_USB,
		.str = "has usb\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_ATTINY,
		.str = "has ATtiny\t",
	}, {
		.bit = RB_HW_OPT_NO_NAND,
		.str = "no NAND\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_LCD,
		.str = "has LCD\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_POE_OUT,
		.str = "has POE out\t",
	}, {
		.bit = RB_HW_OPT_HAS_uSD,
		.str = "has MicroSD\t",
	}, {
		.bit = RB_HW_OPT_HAS_SIM,
		.str = "has SIM\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_SFP,
		.str = "has SFP\t\t",
	}, {
		.bit = RB_HW_OPT_HAS_WIFI,
		.str = "has WiFi\t",
	}, {
		.bit = RB_HW_OPT_HAS_TS_FOR_ADC,
		.str = "has TS ADC\t",
	}, {
		.bit = RB_HW_OPT_HAS_PLC,
		.str = "has PLC\t\t",
	},
};

/*
 * The MAC is stored network-endian on all devices, in 2 32-bit segments:
 * <XX:XX:XX:XX> <XX:XX:00:00>. Kernel print has us covered.
 */
static ssize_t hc_tag_show_mac(const u8 *pld, u16 pld_len, char *buf)
{
	if (8 != pld_len)
		return -EINVAL;

	return sprintf(buf, "%pM\n", pld);
}

/*
 * Print HW options in a human readable way:
 * The raw number and in decoded form
 */
static ssize_t hc_tag_show_hwoptions(const u8 *pld, u16 pld_len, char *buf)
{
	char *out = buf;
	u32 data;	// cpu-endian
	int i;

	if (sizeof(data) != pld_len)
		return -EINVAL;

	data = *(u32 *)pld;
	out += sprintf(out, "raw\t\t: 0x%08x\n\n", data);

	for (i = 0; i < ARRAY_SIZE(hc_hwopts); i++)
		out += sprintf(out, "%s: %s\n", hc_hwopts[i].str,
			       (data & hc_hwopts[i].bit) ? "true" : "false");

	return out - buf;
}

static ssize_t hc_wlan_data_bin_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t off, size_t count);

static struct hc_wlan_attr {
	const u16 erd_tag_id;
	struct bin_attribute battr;
	u16 pld_ofs;
	u16 pld_len;
} hc_wd_multi_battrs[] = {
	{
		.erd_tag_id = RB_WLAN_ERD_ID_MULTI_8001,
		.battr = __BIN_ATTR(data_0, S_IRUSR, hc_wlan_data_bin_read, NULL, 0),
	}, {
		.erd_tag_id = RB_WLAN_ERD_ID_MULTI_8201,
		.battr = __BIN_ATTR(data_2, S_IRUSR, hc_wlan_data_bin_read, NULL, 0),
	}
};

static struct hc_wlan_attr hc_wd_solo_battr = {
	.erd_tag_id = RB_WLAN_ERD_ID_SOLO,
	.battr = __BIN_ATTR(wlan_data, S_IRUSR, hc_wlan_data_bin_read, NULL, 0),
};

static ssize_t hc_attr_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf);

/* Array of known tags to publish in sysfs */
static struct hc_attr {
	const u16 tag_id;
	ssize_t (* const tshow)(const u8 *pld, u16 pld_len, char *buf);
	struct kobj_attribute kattr;
	u16 pld_ofs;
	u16 pld_len;
} hc_attrs[] = {
	{
		.tag_id = RB_ID_FLASH_INFO,
		.tshow = routerboot_tag_show_u32s,
		.kattr = __ATTR(flash_info, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_MAC_ADDRESS_PACK,
		.tshow = hc_tag_show_mac,
		.kattr = __ATTR(mac_base, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_BOARD_PRODUCT_CODE,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(board_product_code, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_BIOS_VERSION,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(booter_version, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_SERIAL_NUMBER,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(board_serial, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_MEMORY_SIZE,
		.tshow = routerboot_tag_show_u32s,
		.kattr = __ATTR(mem_size, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_MAC_ADDRESS_COUNT,
		.tshow = routerboot_tag_show_u32s,
		.kattr = __ATTR(mac_count, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_HW_OPTIONS,
		.tshow = hc_tag_show_hwoptions,
		.kattr = __ATTR(hw_options, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_WLAN_DATA,
		.tshow = NULL,
	}, {
		.tag_id = RB_ID_BOARD_IDENTIFIER,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(board_identifier, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_PRODUCT_NAME,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(product_name, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_DEFCONF,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(defconf, S_IRUSR, hc_attr_show, NULL),
	}, {
		.tag_id = RB_ID_BOARD_REVISION,
		.tshow = routerboot_tag_show_string,
		.kattr = __ATTR(board_revision, S_IRUSR, hc_attr_show, NULL),
	}
};

/*
 * If the RB_ID_WLAN_DATA payload starts with RB_MAGIC_ERD, then past
 * that magic number the payload itself contains a routerboot tag node
 * locating the LZO-compressed calibration data. So far this scheme is only
 * known to use a single tag at id 0x1.
 */
static int hc_wlan_data_unpack_erd(const u16 tag_id, const u8 *inbuf, size_t inlen,
				   void *outbuf, size_t *outlen)
{
	u16 lzo_ofs, lzo_len;
	int ret;

	/* Find embedded tag */
	ret = routerboot_tag_find(inbuf, inlen, tag_id, &lzo_ofs, &lzo_len);
	if (ret) {
		pr_debug(RB_HC_PR_PFX "no ERD data for id 0x%04x\n", tag_id);
		goto fail;
	}

	if (lzo_len > inlen) {
		pr_debug(RB_HC_PR_PFX "Invalid ERD data length\n");
		ret = -EINVAL;
		goto fail;
	}

	ret = lzo1x_decompress_safe(inbuf+lzo_ofs, lzo_len, outbuf, outlen);
	if (ret)
		pr_debug(RB_HC_PR_PFX "LZO decompression error (%d)\n", ret);

fail:
	return ret;
}

/*
 * If the RB_ID_WLAN_DATA payload starts with RB_MAGIC_LZOR, then past
 * that magic number is a payload that must be appended to the hc_lzor_prefix,
 * the resulting blob is LZO-compressed. In the LZO decompression result,
 * the RB_MAGIC_ERD magic number (aligned) must be located. Following that
 * magic, there is one or more routerboot tag node(s) locating the RLE-encoded
 * calibration data payload.
 */
static int hc_wlan_data_unpack_lzor(const u16 tag_id, const u8 *inbuf, size_t inlen,
				    void *outbuf, size_t *outlen)
{
	u16 rle_ofs, rle_len;
	const u32 *needle;
	u8 *tempbuf;
	size_t templen;
	int ret;

	/* Temporary buffer same size as the outbuf */
	templen = *outlen;
	tempbuf = kmalloc(templen, GFP_KERNEL);
	if (!tempbuf)
		return -ENOMEM;

	/* LZO-decompress lzo_len bytes of outbuf into the tempbuf */
	ret = mikrotik_wlan_lzor_prefixed_decompress(inbuf, inlen, tempbuf, &templen);
	if (ret) {
		if (LZO_E_INPUT_NOT_CONSUMED == ret) {
			/*
			 * The tag length is always aligned thus the LZO payload may be padded,
			 * which can trigger a spurious error which we ignore here.
			 */
			pr_debug(RB_HC_PR_PFX "LZOR: LZO EOF before buffer end - this may be harmless\n");
		} else {
			pr_debug(RB_HC_PR_PFX "LZOR: LZO decompression error (%d)\n", ret);
			goto fail;
		}
	}

	/*
	 * Post decompression we have a blob (possibly byproduct of the lzo
	 * dictionary). We need to find RB_MAGIC_ERD. The magic number seems to
	 * be 32bit-aligned in the decompression output.
	 */
	needle = (const u32 *)tempbuf;
	while (RB_MAGIC_ERD != *needle++) {
		if ((u8 *)needle >= tempbuf+templen) {
			pr_debug(RB_HC_PR_PFX "LZOR: ERD magic not found\n");
			ret = -ENODATA;
			goto fail;
		}
	};
	templen -= (u8 *)needle - tempbuf;

	/* Past magic. Look for tag node */
	ret = routerboot_tag_find((u8 *)needle, templen, tag_id, &rle_ofs, &rle_len);
	if (ret) {
		pr_debug(RB_HC_PR_PFX "LZOR: no RLE data for id 0x%04x\n", tag_id);
		goto fail;
	}

	if (rle_len > templen) {
		pr_debug(RB_HC_PR_PFX "LZOR: Invalid RLE data length\n");
		ret = -EINVAL;
		goto fail;
	}

	/* RLE-decode tempbuf from needle back into the outbuf */
	ret = mikrotik_wlan_rle_decompress((u8 *)needle+rle_ofs, rle_len, outbuf, outlen);
	if (ret)
		pr_debug(RB_HC_PR_PFX "LZOR: RLE decoding error (%d)\n", ret);

fail:
	kfree(tempbuf);
	return ret;
}

static int hc_wlan_data_unpack(const u16 tag_id, const size_t tofs, size_t tlen,
			       void *outbuf, size_t *outlen)
{
	const u8 *lbuf;
	u32 magic;
	int ret;

	/* Caller ensure tlen > 0. tofs is aligned */
	if ((tofs + tlen) > hc_buflen)
		return -EIO;

	lbuf = hc_buf + tofs;
	magic = *(u32 *)lbuf;

	ret = -ENODATA;
	switch (magic) {
	case RB_MAGIC_LZOR:
		/* Skip magic */
		lbuf += sizeof(magic);
		tlen -= sizeof(magic);
		ret = hc_wlan_data_unpack_lzor(tag_id, lbuf, tlen, outbuf, outlen);
		break;
	case RB_MAGIC_ERD:
		/* Skip magic */
		lbuf += sizeof(magic);
		tlen -= sizeof(magic);
		ret = hc_wlan_data_unpack_erd(tag_id, lbuf, tlen, outbuf, outlen);
		break;
	default:
		/*
		 * If the RB_ID_WLAN_DATA payload doesn't start with a
		 * magic number, the payload itself is the raw RLE-encoded
		 * calibration data. Only RB_WLAN_ERD_ID_SOLO makes sense here.
		 */
		if (RB_WLAN_ERD_ID_SOLO == tag_id) {
			ret = mikrotik_wlan_rle_decompress(lbuf, tlen, outbuf, outlen);
			if (ret)
				pr_debug(RB_HC_PR_PFX "RLE decoding error (%d)\n", ret);
		}
		break;
	}

	return ret;
}

static ssize_t hc_attr_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	const struct hc_attr *hc_attr;
	const u8 *pld;
	u16 pld_len;

	hc_attr = container_of(attr, typeof(*hc_attr), kattr);

	if (!hc_attr->pld_len)
		return -ENOENT;

	pld = hc_buf + hc_attr->pld_ofs;
	pld_len = hc_attr->pld_len;

	return hc_attr->tshow(pld, pld_len, buf);
}

/*
 * This function will allocate and free memory every time it is called. This
 * is not the fastest way to do this, but since the data is rarely read (mainly
 * at boot time to load wlan caldata), this makes it possible to save memory for
 * the system.
 */
static ssize_t hc_wlan_data_bin_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t off, size_t count)
{
	struct hc_wlan_attr *hc_wattr;
	size_t outlen;
	void *outbuf;
	int ret;

	hc_wattr = container_of(attr, typeof(*hc_wattr), battr);

	if (!hc_wattr->pld_len)
		return -ENOENT;

	outlen = RB_ART_SIZE;

	/* Don't bother unpacking if the source is already too large */
	if (hc_wattr->pld_len > outlen)
		return -EFBIG;

	outbuf = kmalloc(outlen, GFP_KERNEL);
	if (!outbuf)
		return -ENOMEM;

	ret = hc_wlan_data_unpack(hc_wattr->erd_tag_id, hc_wattr->pld_ofs, hc_wattr->pld_len, outbuf, &outlen);
	if (ret) {
		kfree(outbuf);
		return ret;
	}

	if (off >= outlen) {
		kfree(outbuf);
		return 0;
	}

	if (off + count > outlen)
		count = outlen - off;

	memcpy(buf, outbuf + off, count);

	kfree(outbuf);
	return count;
}

int rb_hardconfig_init(struct kobject *rb_kobj, struct mtd_info *mtd)
{
	struct kobject *hc_wlan_kobj;
	size_t bytes_read, buflen, outlen;
	const u8 *buf;
	void *outbuf;
	int i, j, ret;
	u32 magic;

	hc_buf = NULL;
	hc_kobj = NULL;
	hc_wlan_kobj = NULL;

	ret = __get_mtd_device(mtd);
	if (ret)
		return -ENODEV;

	hc_buflen = mtd->size;
	hc_buf = kmalloc(hc_buflen, GFP_KERNEL);
	if (!hc_buf) {
		__put_mtd_device(mtd);
		return -ENOMEM;
	}

	ret = mtd_read(mtd, 0, hc_buflen, &bytes_read, hc_buf);
	__put_mtd_device(mtd);

	if (ret)
		goto fail;

	if (bytes_read != hc_buflen) {
		ret = -EIO;
		goto fail;
	}

	/* Check we have what we expect */
	magic = *(const u32 *)hc_buf;
	if (RB_MAGIC_HARD != magic) {
		ret = -EINVAL;
		goto fail;
	}

	/* Skip magic */
	buf = hc_buf + sizeof(magic);
	buflen = hc_buflen - sizeof(magic);

	/* Populate sysfs */
	ret = -ENOMEM;
	hc_kobj = kobject_create_and_add(RB_MTD_HARD_CONFIG, rb_kobj);
	if (!hc_kobj)
		goto fail;

	/* Locate and publish all known tags */
	for (i = 0; i < ARRAY_SIZE(hc_attrs); i++) {
		ret = routerboot_tag_find(buf, buflen, hc_attrs[i].tag_id,
					  &hc_attrs[i].pld_ofs, &hc_attrs[i].pld_len);
		if (ret) {
			hc_attrs[i].pld_ofs = hc_attrs[i].pld_len = 0;
			continue;
		}

		/* Account for skipped magic */
		hc_attrs[i].pld_ofs += sizeof(magic);

		/*
		 * Special case RB_ID_WLAN_DATA to prep and create the binary attribute.
		 * We first check if the data is "old style" within a single tag (or no tag at all):
		 * If it is we publish this single blob as a binary attribute child of hc_kobj to
		 * preserve backward compatibility.
		 * If it isn't and instead uses multiple ERD tags, we create a subfolder and
		 * publish the known ones there.
		 */
		if ((RB_ID_WLAN_DATA == hc_attrs[i].tag_id) && hc_attrs[i].pld_len) {
			if (! IS_ENABLED(CONFIG_MIKROTIK_WLAN_DECOMPRESS)) {

				pr_err(RB_HC_PR_PFX "WLAN tag found, but decode library not available\n");
				continue;
			}
			outlen = RB_ART_SIZE;
			outbuf = kmalloc(outlen, GFP_KERNEL);
			if (!outbuf) {
				pr_warn(RB_HC_PR_PFX "Out of memory parsing WLAN tag\n");
				continue;
			}

			/* Test ID_SOLO first, if found: done */
			ret = hc_wlan_data_unpack(RB_WLAN_ERD_ID_SOLO, hc_attrs[i].pld_ofs, hc_attrs[i].pld_len, outbuf, &outlen);
			if (!ret) {
				hc_wd_solo_battr.pld_ofs = hc_attrs[i].pld_ofs;
				hc_wd_solo_battr.pld_len = hc_attrs[i].pld_len;

				ret = sysfs_create_bin_file(hc_kobj, &hc_wd_solo_battr.battr);
				if (ret)
					pr_warn(RB_HC_PR_PFX "Could not create %s sysfs entry (%d)\n",
						hc_wd_solo_battr.battr.attr.name, ret);
			}
			/* Otherwise, create "wlan_data" subtree and publish known data */
			else {
				hc_wlan_kobj = kobject_create_and_add("wlan_data", hc_kobj);
				if (!hc_wlan_kobj) {
					kfree(outbuf);
					pr_warn(RB_HC_PR_PFX "Could not create wlan_data sysfs folder\n");
					continue;
				}

				for (j = 0; j < ARRAY_SIZE(hc_wd_multi_battrs); j++) {
					outlen = RB_ART_SIZE;
					ret = hc_wlan_data_unpack(hc_wd_multi_battrs[j].erd_tag_id,
								  hc_attrs[i].pld_ofs, hc_attrs[i].pld_len, outbuf, &outlen);
					if (ret) {
						hc_wd_multi_battrs[j].pld_ofs = hc_wd_multi_battrs[j].pld_len = 0;
						continue;
					}

					hc_wd_multi_battrs[j].pld_ofs = hc_attrs[i].pld_ofs;
					hc_wd_multi_battrs[j].pld_len = hc_attrs[i].pld_len;

					ret = sysfs_create_bin_file(hc_wlan_kobj, &hc_wd_multi_battrs[j].battr);
					if (ret)
						pr_warn(RB_HC_PR_PFX "Could not create wlan_data/%s sysfs entry (%d)\n",
							hc_wd_multi_battrs[j].battr.attr.name, ret);
				}
			}

			kfree(outbuf);
		}
		/* All other tags are published via standard attributes */
		else {
			ret = sysfs_create_file(hc_kobj, &hc_attrs[i].kattr.attr);
			if (ret)
				pr_warn(RB_HC_PR_PFX "Could not create %s sysfs entry (%d)\n",
				       hc_attrs[i].kattr.attr.name, ret);
		}
	}

	pr_info("MikroTik RouterBOARD hardware configuration sysfs driver v" RB_HARDCONFIG_VER "\n");

	return 0;

fail:
	kfree(hc_buf);
	hc_buf = NULL;
	return ret;
}

void rb_hardconfig_exit(void)
{
	kobject_put(hc_kobj);
	hc_kobj = NULL;
	kfree(hc_buf);
	hc_buf = NULL;
}
