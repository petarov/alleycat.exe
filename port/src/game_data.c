/* The public ARM64 binary contains no original DOS data segment. */
#include "game_data.h"
#include "bios.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <string.h>

extern uint8_t ds_base[SEGMENT_SIZE];

int game_data_load(const char *path, char *error, size_t error_size)
{
    static const unsigned char expected[32] = {
        0x49,0x79,0xc8,0x86,0x78,0x26,0xe0,0x88,0x81,0xd1,0xd4,0xbe,0xc9,0x5c,0x95,0xd6,
        0xbc,0x1a,0xc7,0x0f,0xd2,0x10,0x50,0xcc,0xdb,0x4f,0x73,0x3a,0x50,0xd7,0xbf,0xdd
    };
    unsigned char image[55068], digest[CC_SHA256_DIGEST_LENGTH];
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) {
        snprintf(error, error_size, "Cannot read CAT.EXE.");
        return 0;
    }
    size_t size = fread(image, 1, sizeof image, file);
    int failed = ferror(file);
    fclose(file);
    if (failed || size != 55067) {
        snprintf(error, error_size, "Please select the supported original DOS CAT.EXE (55,067 bytes).");
        return 0;
    }
    CC_SHA256(image, (CC_LONG)size, digest);
    if (memcmp(digest, expected, sizeof expected)) {
        snprintf(error, error_size, "This CAT.EXE is not the supported DOS version.");
        return 0;
    }
    memset(ds_base, 0, SEGMENT_SIZE);
    memcpy(ds_base, image + 0x300, 0x7130);
    if (error_size)
        error[0] = '\0';
    return 1;
}
