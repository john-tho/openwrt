BOARDNAME:=MikroTik
FEATURES += minor
KERNEL_IMAGES:=vmlinux
IMAGES_DIR:=compressed

DEFAULT_PACKAGES += \
	kmod-ath10k-ct-smallbuffers wpad-basic-wolfssl \
	ath10k-firmware-qca4019-ct
