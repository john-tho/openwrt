DEVICE_VARS += UBI_KERNEL_MIB UBI_MAX_MIB

# RouterBOOT NAND OEM firmware boots:
# NAND mtd part 0, UBI vol number 0 "Kernel", file "kernel" (ELF)
# based on imx6 boot-overlay from 5d9a4c210dd6f601a0dcf19886a95959df29d40f
define Build/mikrotik-kernel-ubifs
	rm -rf $@.kernelubifs $@.ubifs
	mkdir -p $@.kernelubifs
	$(CP) $@ $@.kernelubifs/kernel
	$(STAGING_DIR_HOST)/bin/mkfs.ubifs \
		$(filter-out --max-leb-cnt%,$(UBIFS_OPTS)) \
		--max-leb-cnt=$(UBI_UBI_MAX_LEB_CNT_KERNEL) \
		--compr=none --squash-uids \
		--root=$@.kernelubifs --output=$@.ubifs
	$(CP) $@.ubifs $@
endef

# build a ubinized partition to ubiformat to NAND mtd part 0
define Build/mikrotik-kernel-ubi
	rm -f ubi_kernel.ini $@.ubi
	echo "[Kernel]" > ubi_kernel.ini
	echo "mode=ubi" >> ubi_kernel.ini
	echo "image=$@.ubifs" >> ubi_kernel.ini
	echo "vol_id=0" >> ubi_kernel.ini
	echo "vol_name=Kernel" >> ubi_kernel.ini
	echo "vol_type=static" >> ubi_kernel.ini
	echo "vol_alignment=1" >> ubi_kernel.ini
	cat ubi_kernel.ini
	ubinize --output=$@.ubi --peb-size=$(BLOCKSIZE:%k=%KiB) \
		--min-io-size=$(PAGESIZE) ubi_kernel.ini
	$(CP) $@.ubi $@
endef

define Device/mikrotik_nand
	DEVICE_VENDOR := MikroTik
	BLOCKSIZE := 128k
	PAGESIZE := 2048
	UBIFS_LEB = $$(shell echo "$$$$(( $$(BLOCKSIZE:k=) / 32 * 31 ))KiB")
	# kernel MTD partition size for UBI in MiB
	UBI_KERNEL_MIB := 8
	# maximum UBI MTD partition size in MiB
	UBI_MAX_MIB := 512
	# max-leb-cnt=max vol size/blocksize Ex. 8MiB/128KiB = 64
	UBI_MAX_LEB_CNT = $$(shell echo \
		$$$$(( $$(UBI_MAX_MIB) * 2**10 / $$(BLOCKSIZE:k=) )))
	UBI_UBI_MAX_LEB_CNT_KERNEL = $$(shell echo \
		$$$$(( $$(UBI_KERNEL_MIB) * 2**10 / $$(BLOCKSIZE:k=) )))
	KERNEL_NAME := vmlinux
	KERNEL_INITRAMFS := kernel-bin | append-dtb-elf
	UBIFS_OPTS = --min-io-size=$$(PAGESIZE) \
		--leb-size=$$(UBIFS_LEB) --max-leb-cnt=$$(UBI_MAX_LEB_CNT)
	IMAGES = sysupgrade.bin

	## match OEM, 2 UBI mtd partitions on NAND
	# requires custom upgrade script
	KERNEL := kernel-bin | append-dtb-elf | mikrotik-kernel-ubifs | \
		mikrotik-kernel-ubi
	IMAGE/sysupgrade.bin := append-kernel | sysupgrade-tar | \
		append-metadata

	## single ubi partition on NAND not working
	# when additional ubinize packed rootfs UBI volume present
	# loading kernel... OK
	# setting up elf image... not an elf header
	# kernel loading failed
	#KERNEL := kernel-bin | append-dtb-elf | mikrotik-kernel-ubifs
	#IMAGE/sysupgrade.bin := append-ubi
	#KERNEL_IN_UBI := 1
endef

define Device/mikrotik_nor
	DEVICE_VENDOR := MikroTik
	BLOCKSIZE := 64k
	IMAGE_SIZE := 16128k
	KERNEL_NAME := vmlinux
	KERNEL := kernel-bin | append-dtb-elf
	IMAGES = sysupgrade.bin
	IMAGE/sysupgrade.bin := append-kernel | kernel2minor -s 1024 | \
		pad-to $$$$(BLOCKSIZE) | append-rootfs | pad-rootfs | \
		check-size | append-metadata
endef

define Device/mikrotik_hap-ac2
	$(call Device/mikrotik_nor)
	DEVICE_MODEL := hAP ac2
	SOC := qcom-ipq4018
	DEVICE_PACKAGES := ipq-wifi-mikrotik_hap-ac2 -kmod-ath10k-ct \
		kmod-ath10k-ct-smallbuffers
endef
TARGET_DEVICES += mikrotik_hap-ac2

define Device/mikrotik_rb450gx4
	$(call Device/mikrotik_nand)
	DEVICE_MODEL := RouterBOARD RB450Gx4
	SOC := qcom-ipq4019
	DEVICE_DTS := qcom-ipq4019-rb450gx4
	DEVICE_PACKAGES := -kmod-ath10k-ct -ath10k-firmware-qca4019-ct \
		-wpad-basic-wolfssl
endef

define Device/mikrotik_rb450gx4-128k
	$(call Device/mikrotik_rb450gx4)
	DEVICE_VARIANT := 128k blocksize NAND
	BLOCKSIZE := 128k
	PAGESIZE := 2048
	SUPPORTED_DEVICES += mikrotik,rb450gx4
endef
TARGET_DEVICES += mikrotik_rb450gx4-128k

define Device/mikrotik_rb450gx4-256k
	$(call Device/mikrotik_rb450gx4)
	DEVICE_VARIANT := 256k blocksize NAND
	BLOCKSIZE := 256k
	PAGESIZE := 4096
	SUPPORTED_DEVICES += mikrotik,rb450gx4
endef
TARGET_DEVICES += mikrotik_rb450gx4-256k

define Device/mikrotik_sxtsq-5-ac
	$(call Device/mikrotik_nor)
	DEVICE_MODEL := SXTsq 5 ac (RBSXTsqG-5acD)
	SOC := qcom-ipq4018
	DEVICE_PACKAGES := ipq-wifi-mikrotik_sxtsq-5-ac rssileds
endef
TARGET_DEVICES += mikrotik_sxtsq-5-ac
