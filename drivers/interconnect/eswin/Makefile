#
# Makefile for Arteris FlexNOC on-chip interconnect
#

obj-$(CONFIG_INTERCONNECT_WIN2030_WDT)	+= win2030_noc_wdt.o
ccflags-$(CONFIG_INTERCONNECT_ESWIN_DEBUG) += -DDEBUG

obj-$(CONFIG_INTERCONNECT_ESWIN) += eswin_noc_drv.o
eswin_noc_drv-objs:= noc.o noc_sysfs.o noc_stat_sysfs.o noc_stat.o noc_profiler.o noc_profiler_sysfs.o of_noc.o win2030_sideband_manager.o

