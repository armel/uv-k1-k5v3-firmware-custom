#!/usr/bin/env python3

# Copyright (c) 2025 muzkr
#
#   https://github.com/muzkr
#
# Licensed under the MIT License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at the root of this repository.
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#


import argparse
import serial
import signal
from time import sleep
import os

import _prog as pp
import _dump as dd
import _restore as rr


def load_image(file: str) -> bytes:

    a = bytearray()
    with open(file, "rb") as fd:
        buf = bytearray(512)
        while True:
            len1 = fd.readinto(buf)
            if len1 > 0:
                a.extend(memoryview(buf)[:len1])
            if len1 < len(buf):
                break

    return a


def main_dump(args, ser: serial.Serial):

    dump_file: str = args.file

    print("Dump file: {}".format(dump_file))
    if os.path.exists(dump_file):
        print("Dump file exists. Will be overwritten")

    if args.config:
        dump_what = dd.DUMP_CONFIG
        print("Dump configuration..")
    elif args.calib:
        dump_what = dd.DUMP_CALIB
        print("Dump calibration data..")
    else:
        dump_what = dd.DUMP_ALL
        print("Dump all..")

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    dump = dd.EepromDump(ser, dump_what, dump_file)
    while (not quit_flag) and dump.loop():
        sleep(0)


def main_restore(args, ser: serial.Serial):

    dump_file: str = args.file

    print("Dump file: {}".format(dump_file))
    if not os.path.exists(dump_file):
        print("Dump file not exist")
        return

    if args.config:
        dump_what = dd.DUMP_CONFIG
        print("Restore configuration..")
    elif args.calib:
        dump_what = dd.DUMP_CALIB
        print("Restore calibration data..")
    else:
        dump_what = dd.DUMP_ALL
        print("Restore all..")

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    dump = rr.EepromDump(ser, dump_what, dump_file)
    while (not quit_flag) and dump.loop():
        sleep(0)

def main_ports(args):
    from serial.tools import list_ports
    port = list(list_ports.comports())
    print("USB Device avalaible:")
    for p in port:
        print(p.device)

def main_flash(args, ser: serial.Serial):

    bl_ver: str = args.bl_ver
    fw_file: str = args.file

    try:
        fw_image = load_image(fw_file)
        if 0 == len(fw_image):
            print("Invalid firmware image: {}: empty file".format(fw_file))
            return
    except Exception as e:
        print("Cannot load firmware image '{}': {}".format(fw_file, e))
        return

    if len(bl_ver) > 4:
        print("Invalid bootloader version '{}': more than 4 characters".format(bl_ver))
        return

    print("Firmware image loaded: {}, size = {}".format(fw_file, len(fw_image)))

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    prog = pp.Programmer(ser, fw_image, bl_ver)

    while (not quit_flag) and prog.loop():
        sleep(0)


def main():

    # Usage:
    # serialtool.py --port <port> subcmd ..
    # serialtool.py .. flash [--bl-ver <ver>] <file>
    # serialtool.py .. dump {--config | --calib [| --all]} file
    # serialtool.py .. restore {--config | --calib [| --all]} file
    ap = argparse.ArgumentParser(description="UV-K5 V2 serial tool")

    # TODO: have to add option to each of subcommands ??
    # ap.add_argument(
    #     "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=False
    # )
    sp = ap.add_subparsers(required=True, dest="subcommand")

    ap_ports = sp.add_parser("ports", help="list usb ports")

    ap_flash = sp.add_parser("flash", help="flash firmware")
    ap_flash.add_argument(
        "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=True
    )
    ap_flash.add_argument(
        "--bl-ver",
        help="bootloader version, eg. '1.01'. Max 4 characters. Default '?'",
        required=False,
        default="?",
    )
    ap_flash.add_argument("file", help="firmware image file")

    ap_dump = sp.add_parser("dump", help="dump configuration or calibration data")
    ap_dump.add_argument(
        "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=True
    )
    ag = ap_dump.add_mutually_exclusive_group()
    ag.add_argument("--config", action="store_true", help="dump configuration")
    ag.add_argument("--calib", action="store_true", help="dump calibration data")
    ag.add_argument(
        "--all",
        "-a",
        action="store_true",
        help="dump both configuration and calibration data. This is default",
    )
    ap_dump.add_argument("file", help="output dump file")

    ap_restore = sp.add_parser(
        "restore", help="restore configuration or calibration data from previous dump"
    )
    ap_restore.add_argument(
        "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=True
    )
    ag = ap_restore.add_mutually_exclusive_group()
    ag.add_argument("--config", action="store_true", help="restore configuration")
    ag.add_argument("--calib", action="store_true", help="restore calibration data")
    ag.add_argument(
        "--all",
        "-a",
        action="store_true",
        help="restore both configuration and calibration data. This is default",
    )
    ap_restore.add_argument("file", help="input dump file")

    args = ap.parse_args()
    sub_name: str = args.subcommand

    if sub_name =='ports':
        main_ports(args)
        exit(0)

    port: str = args.port

    print(ap.description)
    # print("Press Ctrl-C to quit")

    try:
        ser = serial.Serial(port, baudrate=38400, timeout=0.0001, write_timeout=None)
    except Exception as e:
        print("Cannot open port '{}': {}".format(port, e))
        return

    match sub_name:
        case "flash":
            main_flash(args, ser)
        case "dump":
            main_dump(args, ser)
        case "restore":
            main_restore(args, ser)

    ser.close()
    print("Quit")


if __name__ == "__main__":
    main()
