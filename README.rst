Thingy-91 optimized acceleration measurement final theses.
###########

Overview
********

This Thingy:91 application has been optimized for efficient acceleration measurement by utilizing ADXL362 FIFO feature, various compression methods and lightweight CoAP protocol with DTLS encryption.

Building and Running
********************

Firmware can be built with Nordic Connect toolchain version 2.6.1. Building this firmware for older or newer versions of Nordic Connect toolchains may require modifying various files.

ncsFolder contains files, which are to be replaced files in Nordic Connect toolchain. These files enables FIFO-support for Zephyr ADXL362 accelerometer driver, which is not normally available.


Link to nRF Connect.
https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop

With nRF connect, install nRF Connect SDK v2.6.1 with Toolchain manager tool.
Instructions for replacing driver files can be found in ncsFiles folder.
