#
# Makefile for the fingerprint device.
#

# Each configuration option enables a list of files.
#include $(srctree)/drivers/misc/mediatek/Makefile.custom

ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)

subdir-ccflags-y += -I$(srctree)/drivers/tee/teei/400/tz_driver/include/
subdir-ccflags-y += -I$(srctree)/drivers/tee/teei/400/common/include/

# obj-$(CONFIG_GOODIX_FINGERPRINT) := gf_spi_tee.o gf_spi_factory.o
obj-m := gf_spi_tee.o

$(warning "start compling fp")
$(warning $(CONFIG_BUILD_PRODUCT_NAME))
ifneq ($(filter "daumier", $(CONFIG_BUILD_PRODUCT_NAME)),)
CDEFINES += -DCONFIG_PRODUCT_DAUMIER_FP
KBUILD_CPPFLAGS += $(CDEFINES)
else ifneq ($(filter "plato", $(CONFIG_BUILD_PRODUCT_NAME)),)
CDEFINES += -DCONFIG_PRODUCT_PLATO_FP
KBUILD_CPPFLAGS += $(CDEFINES)
endif
