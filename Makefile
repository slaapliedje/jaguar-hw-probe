# Jaguar hardware probe — builds probe.cof with the jaguar-sdk
# (https://github.com/cubanismo/jaguar-sdk). Point JAGSDK at your checkout.

JAGSDK ?= $(HOME)/jaguar-sdk
export PATH := $(JAGSDK)/tools/bin:$(PATH)
export RMACPATH := $(JAGSDK)/jaguar/include;$(JAGSDK)/jaguar/skunk/include

include $(JAGSDK)/tools/build/jagdefs.mk

# startup.o first (entry point), bssend.o last (BSS end marker).
OBJS = startup.o probe.o probe_asm.o probe_dsp.o text.o blit.o \
       ee93c46.o eeprom.o bssend.o

PROGS = probe.cof
LIBGCC := $(shell PATH="$(PATH)" m68k-aout-gcc -print-libgcc-file-name)

$(OBJS): Makefile $(wildcard *.h)

# Atari's cartridge EEPROM driver, assembled straight from the SDK
ee93c46.o: $(JAGSDK)/jaguar/source/eeprom/eeprom.s
	$(ASM) $(ASMFLAGS) -o $@ $<

probe.cof: $(OBJS)
	$(LINK) $(LINKFLAGS) -o $@ $(OBJS) $(LIBGCC)

include $(JAGSDK)/tools/build/jagrules.mk
