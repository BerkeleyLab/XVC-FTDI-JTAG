#
# Copyright 2020, Lawrence Berkeley National Laboratory
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS
# AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
# BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

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
