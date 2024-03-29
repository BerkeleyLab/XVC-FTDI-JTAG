#!/usr/bin/env python

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

from __future__ import print_function
import socket
import time

xvcGetinfo = bytearray(b'getinfo:')
xvcResetTapAndGoToShiftDR = bytearray(b'shift:\x09\x00\x00\x00\x5F\x00\x00\x00')
xvcGetID = bytearray(b'shift:\x20\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00')

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 2542))

for cmd in (xvcGetinfo, xvcResetTapAndGoToShiftDR):
    sock.send(cmd)
    print(sock.recv(100))

sock.send(xvcGetID)
id = bytearray(sock.recv(100))
print("%02X%02X%02X%02X"%(id[3], id[2], id[1], id[0]))
