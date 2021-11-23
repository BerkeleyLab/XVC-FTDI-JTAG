INTRODUCTION
============
Xilinx tools such as Vivado and Vitis communicate with FPGAs through a JTAG
interface and an FTDI USB/Serial chip driven by a Xilinx server (hw_server)
that communicates with the tools using a proprietary protocol.  The server
and tools recognize the FTDI chip as a valid target only if it is equipped
with a special EEPROM from Digilent.

The tools also support an older, documented*, protocol called Xilinx Virtual
Cable (XVC).  The code in this repository provides a server that communicates
with the Xilinx tools using XVC and works with FTDI chips whether or not they
are equipped with the Digilent EEPROM.

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
     LBNL Marble: -c 30M -g 11    (Applies to Marble Mini, too)
   Xilinx ZCU111: -c 30M -g 44    (-g 40:44 to pulse power-on reset line)

LICENSE
=======
XVC FTDI JTAG Copyright (c) 2021, The Regents of the University of 
California, through Lawrence Berkeley National Laboratory (subject to 
receipt of any required approvals from the U.S. Dept. of Energy). All 
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

(1) Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

(2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

(3) Neither the name of the University of California, Lawrence Berkeley
National Laboratory, U.S. Dept. of Energy nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

You are under no obligation whatsoever to provide any bug fixes, patches,
or upgrades to the features, functionality or performance of the source
code ("Enhancements") to anyone; however, if you choose to make your
Enhancements available either publicly, or directly to Lawrence Berkeley
National Laboratory, without imposing a separate written license agreement
for such Enhancements, then you hereby grant the following license: a
non-exclusive, royalty-free perpetual license to install, use, modify,
prepare derivative works, incorporate into other computer software,
distribute, and sublicense such enhancements or derivative works thereof,
in binary and source code form.
