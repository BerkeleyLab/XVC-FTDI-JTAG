INTRODUCTION
============
The Xilinx tools such as Vivado and Vitis communicate with FPGAs through
a JTAG interface.  Many boards include an FTDI USB/Serial chip that is
driven through a program (hw_server) that communicates with the Xilinx
tools using a proprietary protocol.  The tools recognize the FTDI chip
as a valid target only if it is equipped with a special EEPROM from
Digilent.

The tools also support an older, documented*, protocol called Xilinx Virtual
Cable (XVC). Xilinx servers for this protocol also recognize FTDI chips only
if they are equipped with the Digilent EEPROM.

This server communicates with the Xilinx tools using XVC and works with FTDI
chips whether or not they are equipped with the Digilent EEPROM.

* Thanks to tmbinc's 2012 reverse-engineering (https://github.com/tmbinc/xvcd)
  and Xilinx's XAPP1251, XVC can now be considered a properly documented and
  usable protocol.

AUTHOR
======
Eric Norum (wenorum@lbl.gov)

PREREQUISITES
=============
C development environment
libusb  --  https://github.com/libusb/libusb

BUILDING
========
In many cases all that's needed is
    make
    make install

You will have to edit the Makefile if you want to install the executable
and manual page in non-default locations, or if your C compiler doesn't
find the libusb header or library.

EXAMPLES
========
LBNL Marble Mini: -c 30M -g 1:1
   Xilinx ZCU111: -c 30M -g 4:4    (-g 4:0:4 to generate power-on reset)

LICENSE
=======
Copyright 2020, Lawrence Berkeley National Laboratory

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS
AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
