# hefile.irx, da compilare con ps2sdk: scripts/build-ps2-iop.sh.
IOP_BIN = hefile.irx

IOP_IMPORT_INCS += \
	system/ioman \
	system/loadcore \
	system/modload \
	system/stdio \
	system/sysclib \
	system/sysmem \
	system/threadman

IOP_OBJS = hefile.o imports.o

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.bin.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release
