#https://mesa.freedesktop.org/archive/mesa-17.0.3.tar.xz

PKG_NAME := mesa
PKG_VER  := 17.0.3

PKG_ARCHIVE_NAME := $(PKG_NAME)-$(PKG_VER).tar.xz

PKG_SOURCES := https://mesa.freedesktop.org/archive/$(PKG_ARCHIVE_NAME)

PKG_MD5     := 003072c044b3fb5eca8be2037f15e4ec

PKG_PATCHES := patch.txt

include $(EXTBLD_LIB)


MESA_CPPFLAGS := -I$(SRC_DIR)/include -I$(SRC_DIR)/compat/cxx/include
MESA_CPPFLAGS += -I$(SRC_DIR)/compat/libc/include -I$(SRC_DIR)/compat/posix/include
MESA_CPPFLAGS += -I$(SRC_DIR)/arch/$(ARCH)/include -O2 -I$(SRC_DIR)/../build/extbld/third_party/freedesktop/mesa/libdrm/libdrm-2.4.79/
MESA_CPPFLAGS += -I$(SRC_DIR)/compat/linux/include

$(CONFIGURE) :
	export EMBOX_GCC_LINK=full; \
	cd $(PKG_SOURCE_DIR) && aclocal && autoconf &&( \
		./configure --host=$(AUTOCONF_TARGET_TRIPLET) \
			--target=$(AUTOCONF_TARGET_TRIPLET) \
			--enable-static  --disable-shared \
			--enable-osmesa  --with-gallium-drivers=swrast \
			--disable-driglx-direct --disable-dri \
			--disable-egl \
			--disable-largefile --disable-valgrind \
			--disable-gallium-llvm --disable-gallium-tests \
			--disable-va \
			--disable-gbm \
			--disable-vdpau \
			--disable-glx --disable-glx-tls \
			--disable-asm \
			--disable-gles1 --disable-gles2 \
			--disable-dependency-tracking \
			CC=$(EMBOX_GCC) \
			CXX=$(EMBOX_GXX) \
			CFLAGS='-O2' \
			CPPFLAGS='-O2 $(MESA_CPPFLAGS)' \
			LIBDRM_CFLAGS='-I$(EXTERNAL_BUILD_DIR)/third_party/freedesktop/mesa/libdrm/libdrm-2.4.79/include/drm/' \
	)
	touch $@

$(BUILD) :
	cd $(PKG_SOURCE_DIR) && ( \
		$(MAKE) MAKEFLAGS='$(EMBOX_IMPORTED_MAKEFLAGS)' LIBDRM_CFLAGS='' V=1; \
	)
	touch $@

$(INSTALL) :
	#	cp $(PKG_SOURCE_DIR)/src/mesa/.libs/libmesa.a $(PKG_INSTALL_DIR)/libmesa.a
	cp $(PKG_SOURCE_DIR)/src/mesa/.libs/libmesagallium.a $(PKG_INSTALL_DIR)/libmesagallium.a
	cp $(PKG_SOURCE_DIR)/src/mapi/shared-glapi/.libs/libglapi.a $(PKG_INSTALL_DIR)/libglapi.a
	cp $(PKG_SOURCE_DIR)/src/mesa/drivers/osmesa/.libs/libOSMesa.a $(PKG_INSTALL_DIR)/libOSMesa.a
	#cp $(PKG_SOURCE_DIR)/src/gallium/winsys/etnaviv/drm/.libs/libetnavivdrm.a $(PKG_INSTALL_DIR)/libetnavivdrm.a
	#cp $(PKG_SOURCE_DIR)/src/gallium/drivers/etnaviv/.libs/libetnaviv.a $(PKG_INSTALL_DIR)/libetnaviv.a
	cp $(PKG_SOURCE_DIR)/src/gallium/auxiliary/.libs/libgallium.a $(PKG_INSTALL_DIR)/libgallium.a
	touch $@
