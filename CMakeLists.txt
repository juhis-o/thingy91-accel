# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thingy_91_efficient_accel_measurement)

target_sources(app PRIVATE src/main.c src/msg_encode.c src/heatshrink_encoder.c src/modem.c src/networking.c src/shared.c src/sensor.c)
