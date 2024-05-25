#!/usr/bin/env python3

# seag-cprs
# Copyright (C) 2024  wilszdev
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


import struct
import sys


ERR_OK        = 0x00
ERR_USAGE     = 0x01
ERR_CPRS_FILE = 0x02
ERR_UNCPRS    = 0x04
ERR_OUT_FILE  = 0x08


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(f'Usage: {sys.argv[0]} INPUTFILE OUTPUTFILE\n')
        return ERR_USAGE

    try:
        inputFile = open(sys.argv[1], 'rb')
    except OSError:
        sys.stderr.write(f'Error: Unable to open file {sys.argv[1]}\n')
        return ERR_CPRS_FILE

    with inputFile:
        inputData = inputFile.read()

    if not (decompressedData := decompress(inputData)):
        return ERR_UNCPRS

    try:
        outputFile = open(sys.argv[2], 'wb')
    except OSError:
        sys.stderr.write(f'Error: Unable to open file {sys.argv[2]} for writing\n')
        return ERR_OUT_FILE

    with outputFile:
        writeCount = outputFile.write(decompressedData)

    if writeCount != len(decompressedData):
        sys.stderr.write(f'Error: Failed to write all data to file {sys.argv[2]}\n')
        return ERR_OUT_FILE

    return ERR_OK


def decompress(data):
    if len(data) % 4 != 0:
        sys.stderr.write('Error: source buffer not 4-byte aligned\n')
        return

    if len(data) < 20:
        sys.stderr.write('Error: Source buffer too small\n')
        return

    if data[:4] != b'CPRS' or data[-4:] != b'CPRS':
        sys.stderr.write('Error: CPRS signature check failed\n')
        return

    compressedSize, decompressedSize, alpha, beta = \
            struct.unpack_from('<IIII', data, 4)

    decompressed = bytearray(decompressedSize)

    readIndex = 20
    writeIndex = 0

    remainingShifts = 0x20
    currentValue = 0
    extractedBytes = 0
    writeCarryByte = 0

    while 1:
        readCarry = 0

        if alpha & 1 == 0:
            readCarry = alpha >> 9
            remainingShifts -= 9
            writeCarryByte = alpha >> 1 & 0xff
            alpha = extractedBytes & 3
            extractedBytes += 1
            currentValue = write_carry(currentValue, alpha, writeCarryByte)
            if extractedBytes & 3 == 0:
                struct.pack_into('<I', decompressed, writeIndex, currentValue)
                writeIndex += 4
                currentValue = 0
        else:
            uVar9 = CPRS_TABLE[(alpha >> 1 & 3) * 0x04]
            uVar1 = (alpha >> 3) >> (uVar9 & 0xff)
            uVar5 = uVar1 >> 4
            tableIndex = (uVar1 & 0xf) * 0x04 + 0x10
            uVar1 = CPRS_TABLE[tableIndex]
            readCarry = uVar5 >> (uVar1 & 0xff)
            remainingShifts -= uVar9 + 7 + uVar1

            iVar7 = CPRS_TABLE[tableIndex + 2] + ((1 << (uVar1 & 0xff)) - 1 & uVar5)
            if iVar7 >= CPRS_TERM:
                break

            iVar6 = (
                    CPRS_TABLE[(alpha >> 1 & 3) * 0x04 + 2]
                    + ((1 << (uVar9 & 0xff)) - 1 & alpha >> 3)
                    + ((CPRS_TABLE[tableIndex + 1] + 0xb) >> 3)
            )

            if iVar7 == 0:
                for _ in range(iVar6):
                    alpha = extractedBytes & 3
                    extractedBytes += 1
                    currentValue = write_carry(currentValue, alpha, writeCarryByte)
                    if extractedBytes & 3 == 0:
                        struct.pack_into('<I', decompressed, writeIndex, currentValue)
                        writeIndex += 4
                        currentValue = 0
            else:
                start = extractedBytes - iVar7 * 2
                for i in range(start, start + iVar6):
                    if iVar7 * 2 < 4 and (extractedBytes & 3) != 0:
                        struct.pack_into('<I', decompressed, writeIndex, currentValue)

                    writeCarryByte = decompressed[i]
                    alpha = extractedBytes & 3
                    extractedBytes += 1
                    currentValue = write_carry(currentValue, alpha, writeCarryByte)
                    if extractedBytes & 3 == 0:
                        struct.pack_into('<I', decompressed, writeIndex, currentValue)
                        writeIndex += 4
                        currentValue = 0

        if remainingShifts < 0:
            remainingShifts += 0x20
            readCarry = beta >> (0x20 - remainingShifts)
            (beta,) = struct.unpack_from('<I', data, readIndex)
            readIndex += 4

        alpha = (beta << remainingShifts) | readCarry
        alpha &= 0xffffffff

    if extractedBytes & 3 != 0:
        struct.pack_into('<I', decompressed, writeIndex, currentValue)

    return decompressed


def write_carry(value, byteIndex, carry):
    value &= ~(0xff << (byteIndex * 8))
    value |= (carry & 0xff) << (byteIndex * 8)
    return value


CPRS_TERM = 0x10002


CPRS_TABLE = [
    0x00000001, 0x00000003, 0x00000000, 0x00000001,
    0x00000001, 0x00000003, 0x00000002, 0x00000003,
    0x00000003, 0x00000005, 0x00000004, 0x0000000b,
    0x00000008, 0x0000000a, 0x0000000c, 0x0000010a,
    0x00000002, 0x00000006, 0x00000000, 0x00000003,
    0x00000002, 0x00000006, 0x00000004, 0x00000007,
    0x00000002, 0x00000006, 0x00000008, 0x0000000b,
    0x00000003, 0x00000007, 0x0000000c, 0x00000013,
    0x00000004, 0x00000008, 0x00000014, 0x00000023,
    0x00000005, 0x00000009, 0x00000024, 0x00000043,
    0x00000006, 0x0000000a, 0x00000044, 0x00000083,
    0x00000007, 0x0000000b, 0x00000084, 0x00000103,
    0x00000008, 0x0000000c, 0x00000104, 0x00000203,
    0x00000009, 0x0000000d, 0x00000204, 0x00000403,
    0x0000000a, 0x0000000e, 0x00000404, 0x00000803,
    0x0000000b, 0x0000000f, 0x00000804, 0x00001003,
    0x0000000c, 0x00000010, 0x00001004, 0x00002003,
    0x0000000d, 0x00000011, 0x00002004, 0x00004003,
    0x0000000e, 0x00000012, 0x00004004, 0x00008003,
    0x0000000f, 0x00000013, 0x00008004, 0x00010002,
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00000009, 0x00000012, 0x00000024, 0x00000048,
    0x00000090, 0x00000120, 0x00000240, 0x00000480,
    0x00000900, 0x00001200, 0x00002400, 0x00004800,
    0x00000001, 0x00009000, 0x00002490, 0x00010900,
    0x00004349, 0x00000091, 0x00019024, 0x00002599,
    0x00051941, 0x0005d34b, 0x00004101, 0x00008001,
    0x0000b080, 0x00082c94, 0x00014308, 0x0004d183,
    0x000880b5, 0x0003f8ad, 0x000cadd5, 0x0004f7db,
    0x00010901, 0x0000d349, 0x00002401, 0x00009924,
    0x000066d0, 0x000519d0, 0x0004436f, 0x00006498,
    0x00059940, 0x000563cb, 0x00086d95, 0x0001c309,
    0x00000000, 0x00c602b5, 0x018c016b, 0x01ce037b,
    0x02940021, 0x01290042, 0x01ad0231, 0x02520084,
    0x031802d6, 0x0210014a, 0x039c02f7, 0x027303de,
    0x03bd00e7, 0x035a0063, 0x01ef0339, 0x00a50108,
    0x015502aa, 0x0372025b, 0x00b702e5, 0x00100200,
    0x01f602cf, 0x019f03ec, 0x039602dc, 0x03d9033e,
    0x01cb016e, 0x00fb0367, 0x00010020, 0x00040080,
    0x00080100, 0x01b9032d, 0x00020040, 0x027d03b3,
    0x021d0160, 0x03b0000b, 0x00240111, 0x00810228,
    0x0335019b, 0x02b9036c, 0x02d500b6, 0x02b602c5,
    0x01e30185, 0x006f00ac, 0x03b502a2, 0x02bd0055,
    0x02690192, 0x0133024c, 0x02f501f4, 0x02b7028f
]


if __name__ == '__main__':
    exit(main())
