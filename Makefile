# XVC FTDI JTAG Copyright (c) 2021, The Regents of the University of 
# California, through Lawrence Berkeley National Laboratory (subject to 
# receipt of any required approvals from the U.S. Dept. of Energy). All 
# rights reserved.
# 
# If you have questions about your rights to use or distribute this software,
# please contact Berkeley Lab's Intellectual Property Office at
# IPO@lbl.gov.
# 
# NOTICE.  This Software was developed under funding from the U.S. Department
# of Energy and the U.S. Government consequently retains certain rights.  As
# such, the U.S. Government has been granted for itself and others acting on
# its behalf a paid-up, nonexclusive, irrevocable, worldwide license in the
# Software to reproduce, distribute copies to the public, prepare derivative 
# works, and perform publicly and display publicly, and to permit others to
# do so.

# Install locations
INSTALL_BIN = /usr/local/bin
INSTALL_MAN = /usr/local/share/man/man1

# Uncomment and modify if libusb stuff is not on compiler search paths
#USBLIB_INCLUDE = -I/usr/local/include
#USBLIB_LIB = -L/usr/local/lib

CFLAGS = -g -O2 $(USBLIB_INCLUDE) $(USBLIB_LIB)
CFLAGS += --std=c99 -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE
CFLAGS += -Wall -Wextra -pedantic -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wshadow
CFLAGS += -Wpointer-arith -Wcast-align -Wcast-qual -Wredundant-decls
LDLIBS = -lusb-1.0

all: ftdiJTAG

clean:
	rm -rf ftdiJTAG ftdiJTAG.dSYM

install: $(INSTALL_BIN)/ftdiJTAG $(INSTALL_MAN)/ftdiJTAG.1
$(INSTALL_BIN)/ftdiJTAG: ftdiJTAG
	cp ftdiJTAG $(INSTALL_BIN)
$(INSTALL_MAN)/ftdiJTAG.1: ftdiJTAG.1
	cp ftdiJTAG.1 $(INSTALL_MAN)

uninstall:
	rm -f $(INSTALL_BIN)/ftdiJTAG $(INSTALL_MAN)/ftdiJTAG.1
