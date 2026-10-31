/* Validate correct data, missing/truncated data, a same-size corrupt copy,
 * and the invariant that a rejected import does not change game memory. */
#include "game_data.h"
#include "bios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint8_t ds_base[SEGMENT_SIZE];

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    char error[256], temp[] = "/tmp/alleycat-import-XXXXXX";
    memset(ds_base, 0xA5, sizeof ds_base);
    if (game_data_load(NULL, error, sizeof error) || ds_base[0] != 0xA5)
        return 1;
    int fd = mkstemp(temp);
    if (fd < 0)
        return 1;
    FILE *file = fdopen(fd, "wb");
    fwrite("MZ", 1, 2, file);
    fclose(file);
    int bad = game_data_load(temp, error, sizeof error);
    unlink(temp);
    if (bad || ds_base[0] != 0xA5)
        return 1;
    unsigned char image[55067];
    file = fopen(argv[1], "rb");
    if (!file)
        return 1;
    size_t size = fread(image, 1, sizeof image, file);
    fclose(file);
    if (size != sizeof image || !game_data_load(argv[1], error, sizeof error) ||
        memcmp(ds_base, image + 0x300, 0x7130) || ds_base[0xFFFF] != 0)
        return 1;
    char corrupt[] = "/tmp/alleycat-import-XXXXXX";
    fd = mkstemp(corrupt);
    if (fd < 0)
        return 1;
    image[0x300] ^= 1;
    file = fdopen(fd, "wb");
    fwrite(image, 1, sizeof image, file);
    fclose(file);
    memset(ds_base, 0xA5, sizeof ds_base);
    bad = game_data_load(corrupt, error, sizeof error);
    unlink(corrupt);
    for (size_t i = 0; i < sizeof ds_base; i++)
        if (ds_base[i] != 0xA5)
            return 1;
    if (bad)
        return 1;
    puts("import: valid, missing, truncated and corrupt CAT.EXE checks passed");
    return 0;
}
