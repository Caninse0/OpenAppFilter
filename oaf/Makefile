include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=oaf
PKG_VERSION:=6.0.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

PKG_AUTOLOAD:=oaf
RSTRIP:=:

define KernelPackage/oaf
  SUBMENU:=Netfilter Extensions
  TITLE:=OAF netfilter module
  FILES:=$(PKG_BUILD_DIR)/oaf.ko
  DEPENDS:=+kmod-ipt-conntrack
  KCONFIG:=
  AUTOLOAD:=$(call AutoLoad,09,$(PKG_AUTOLOAD))
endef

define KernelPackage/oaf/description
  oaf netfilter module
endef

KCFLAGS := -Wno-error=missing-prototypes \
           -Wno-error=missing-declarations \
           -Wno-error=unused-variable \
           -Wno-error=declaration-after-statement \
           -Wno-error=implicit-fallthrough \
           -Wno-error=missing-braces \
           -Wno-error=parentheses \
           -Wno-error=format \
           -Wno-frame-larger-than


MAKE_OPTS:= \
	$(KERNEL_MAKE_FLAGS) \
	M="$(PKG_BUILD_DIR)" \
	KCFLAGS="$(KCFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -rf ./src/* $(PKG_BUILD_DIR)
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,oaf))
