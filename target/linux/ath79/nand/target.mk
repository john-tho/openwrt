BOARDNAME := Generic devices with NAND flash

KERNELNAME:=vmlinux vmlinuz vmlinuz.bin
IMAGES_DIR:=../../..

FEATURES += squashfs nand

DEFAULT_PACKAGES += wpad-basic-wolfssl

define Target/Description
	Firmware for boards using Qualcomm Atheros, MIPS-based SoCs
	in the ar72xx and subsequent series, with support for NAND flash
endef
