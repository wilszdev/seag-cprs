/*
seag-cprs
Copyright (C) 2024  wilszdev

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR_OK          0x00
#define ERR_USAGE       0x01
#define ERR_CPRS_FILE   0x02
#define ERR_UNCPRS      0x08
#define ERR_OUT_FILE    0x10

#define FILE_READ_INCREMENT 0x1000

static void* read_file(char* path, size_t* sizeOut);
static int write_file(char* path, void* data, size_t size);
static void* decompress(uint32_t* data, size_t sizeBytes, size_t* sizeOut);

#define CPRS_SIG ((uint32_t)0x53525043)
#define CPRS_TERM 0x10002

static const uint32_t CPRS_TABLE[192];

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s INPUTFILE [OUTPUTFILE]\n", argv[0]);
        return ERR_USAGE;
    }

    size_t compressedSize = 0;
    void* compressed = read_file(
            (argv[1][0] == '-' && argv[1][1] == 0) ? 0 : argv[1],
            &compressedSize);

    if (!compressed) {
        return ERR_CPRS_FILE;
    }

    size_t decompressedSize = 0;
    void* decompressed = decompress(compressed, compressedSize, &decompressedSize);

    free(compressed);
    compressed = 0;

    if (!decompressed) {
        return ERR_UNCPRS;
    }

    int success = write_file(
            argc == 3 ? argv[2] : 0,
            decompressed,
            decompressedSize);

    free(decompressed);

    return success ? ERR_OK : ERR_OUT_FILE;
}

static void* read_file(char* path, size_t* sizeOut) {
    FILE* file = path ? fopen(path, "rb") : stdin;
    if (!file) {
        fprintf(stderr, "Error: Unable to open file %s\n", path);
        return 0;
    }

    uint8_t* buffer = 0;
    size_t bufferSize = 0;
    size_t totalBytesRead = 0;

    size_t bytesRead = 0;
    do {
        totalBytesRead += bytesRead;

        if (totalBytesRead >= bufferSize) {
            buffer = realloc(buffer, bufferSize + FILE_READ_INCREMENT);
            bufferSize += FILE_READ_INCREMENT;
        }
    } while ((bytesRead = fread(buffer + totalBytesRead, 1, bufferSize - totalBytesRead, file)) != 0);

    if (path) {
        fclose(file);
    }

    *sizeOut = totalBytesRead;
    return buffer;
}

static int write_file(char* path, void* data, size_t size) {
    FILE* file = path ? fopen(path, "wb") : stdout;
    if (!file) {
        fprintf(stderr, "Error: Unable to open file %s for writing\n", path);
        return 0;
    }

    size_t written = fwrite(data, 1, size, file);
    if (written != size) {
        fprintf(stderr, "Error: Failed to write all data to file %s\n", path);
        fclose(file);
        return 0;
    }

    if (path) {
        fclose(file);
    }
    return 1;
}

static void* decompress(uint32_t* data, size_t sizeBytes, size_t* sizeOut) {
    if (sizeBytes % sizeof(data[0]) != 0) {
        fprintf(stderr, "Error: Source buffer not %lu-byte aligned\n", sizeof(data[0]));
        return 0;
    }

    size_t size = sizeBytes / sizeof(data[0]);

    if (size <= 5) {
        fprintf(stderr, "Error: Source buffer too small\n");
        return 0;
    }

    if (data[0] != CPRS_SIG || data[size - 1] != CPRS_SIG) {
        fprintf(stderr, "Error: CPRS signature check failed\n");
        return 0;
    }

    uint32_t compressedSize = data[1];
    uint32_t decompressedSize = data[2];

    void* buffer = malloc(decompressedSize);
    memset(buffer, 0, decompressedSize);

    uint32_t alpha = data[3];
    uint32_t beta = data[4];

    int8_t remainingShifts = 0x20;
    uint32_t currentValue = 0;

    uint32_t* readCursor = data + 5;
    uint32_t* writeCursor = buffer;

    uint32_t index = 0;

    /*  I don't fully understand this and don't really need to.

        It's just a tidied up decompilation of the decompression
        function found in the internal flash of a Seagate/LSI MCU.

        It's definitely not safe currently, there's absolutely no
        bounds checking.
     */

    uint8_t writeCarryByte;

    while (1) {
        uint32_t readCarryByte;

        if ((alpha & 1) == 0) {
            readCarryByte = alpha >> 9;
            remainingShifts -= 9;
            writeCarryByte = alpha >> 1 & 0xff;
            alpha = index & 3;
            index += 1;
            *(uint8_t*)((size_t)&currentValue + alpha) = writeCarryByte;
            if ((index & 3) == 0) {
                *writeCursor = currentValue;
                writeCursor += 1;
                currentValue = 0;
            }
        } else {
            uint32_t uVar9 = CPRS_TABLE[(alpha >> 1 & 3) * 0x04];
            uint32_t uVar1 = (alpha >> 3) >> (uVar9 & 0xff);
            uint32_t uVar5 = uVar1 >> 4;
            const uint32_t* puVar2 = &CPRS_TABLE[(uVar1 & 0xf) * 0x04 + 0x10];
            uVar1 = *puVar2;
            readCarryByte = uVar5 >> (uVar1 & 0xff);
            remainingShifts -= uVar9 + 7 + uVar1;
            int iVar7 = puVar2[2] + ((1 << (uVar1 & 0xff)) - 1U & uVar5);
            if (iVar7 >= CPRS_TERM) {
                break;
            }
            int iVar6 = CPRS_TABLE[(alpha >> 1 & 3) * 0x04 + 2]
                + ((1 << (uVar9 & 0xff)) - 1U & alpha >> 3)
                + ((int)(puVar2[1] + 0xb) >> 3);
            if (iVar7 == 0) {
                while (iVar6--) {
                    alpha = index & 3;
                    index += 1;
                    *(uint8_t*)((size_t)&currentValue + alpha) = writeCarryByte;
                    if ((index & 3) == 0) {
                        *writeCursor = currentValue;
                        currentValue = 0;
                        writeCursor = writeCursor + 1;
                    }
                }
            } else {
                uint8_t* start = (uint8_t*)buffer + index - iVar7 * 2;
                for (int i = 0; i < iVar6; ++i) {
                    if (((uint32_t)(iVar7 * 2) < 4) && ((index & 3) != 0)) {
                        *writeCursor = currentValue;
                    }
                    writeCarryByte = start[i];
                    alpha = index & 3;
                    index += 1;
                    *(uint8_t*)((size_t)&currentValue + alpha) = writeCarryByte;
                    if ((index & 3) == 0) {
                        *writeCursor = currentValue;
                        writeCursor += 1;
                        currentValue = 0;
                    }
                }
            }
        }

        if (remainingShifts < 0) {
            remainingShifts += 0x20;
            readCarryByte = beta >> (0x20 - remainingShifts);
            beta = *readCursor;
            readCursor += 1;
        }
        alpha = (beta << remainingShifts) | readCarryByte;
    }

    if ((index & 3) != 0) {
        *writeCursor = currentValue;
    }
    if (sizeOut) {
        *sizeOut = index;
    }
    return buffer;
}

static const uint32_t CPRS_TABLE[192] = {
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
};
