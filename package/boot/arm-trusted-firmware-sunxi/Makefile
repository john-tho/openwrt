#
# Copyright (C) 2017 Hauke Mehrtens
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_VERSION:=2.4
PKG_RELEASE:=$(AUTORELEASE)
PKG_HASH:=bf3eb3617a74cddd7fb0e0eacbfe38c3258ee07d4c8ed730deef7a175cc3d55b

PKG_MAINTAINER:=Hauke Mehrtens <hauke@hauke-m.de>

include $(INCLUDE_DIR)/trusted-firmware-a.mk
include $(INCLUDE_DIR)/package.mk


define Package/arm-trusted-firmware-sunxi/Default
    SECTION:=boot
    CATEGORY:=Boot Loaders
    TITLE:=ARM Trusted Firmware for Allwinner
    DEPENDS:=@TARGET_sunxi_cortexa53
endef

define Package/arm-trusted-firmware-sunxi-a64
    $(call Package/arm-trusted-firmware-sunxi/Default)
    VARIANT:=sun50i_a64
endef

define Package/arm-trusted-firmware-sunxi-h6
    $(call Package/arm-trusted-firmware-sunxi/Default)
    VARIANT:=sun50i_h6
endef

export GCC_HONOUR_COPTS=s

MAKE_VARS = \
	CROSS_COMPILE="$(TARGET_CROSS)"

MAKE_FLAGS += \
	PLAT=$(BUILD_VARIANT) \
	bl31

define Build/InstallDev
	$(INSTALL_DIR) $(STAGING_DIR_IMAGE)
	$(CP) $(PKG_BUILD_DIR)/build/$(BUILD_VARIANT)/release/bl31.bin $(STAGING_DIR_IMAGE)/bl31_$(BUILD_VARIANT).bin
endef

define Package/arm-trusted-firmware-sunxi/install
endef

$(eval $(call BuildPackage,arm-trusted-firmware-sunxi-a64))
$(eval $(call BuildPackage,arm-trusted-firmware-sunxi-h6))
