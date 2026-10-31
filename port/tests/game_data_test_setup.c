/* Only linked into headless tests, never into the distributable app. */
#include "game_data.h"
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor)) static void load_test_data(void)
{
    char error[256];
    if (!game_data_load(getenv("ALLEYCAT_TEST_EXE"), error, sizeof error)) {
        fprintf(stderr, "test data: %s\n", error);
        exit(2);
    }
}
