#
# Copyright (C) OpenAppFilter
#
# This is free software, licensed under the Apache License, Version 2.0 .
#
include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI app for OAF
LUCI_PKGARCH:=all
LUCI_DEPENDS:= \
	+appfilter \
	+kmod-oaf \
	+luci-compat \
	+luci-lib-jsonc \
	+luci-base

PKG_NAME:=luci-app-oaf
PKG_VERSION:=7.0
PKG_RELEASE:=1

PKG_MAINTAINER:=OpenAppFilter
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
