BOARDNAME:=Generic
FEATURES += squashfs
KERNELNAME:=vmlinux vmlinuz vmlinuz.bin
IMAGES_DIR:=../../..

DEFAULT_PACKAGES += wpad-basic-wolfssl

define Target/Description
	Build firmware images for generic Atheros AR71xx/AR913x/AR934x based boards.
endef
