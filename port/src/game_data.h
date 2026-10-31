#ifndef GAME_DATA_H
#define GAME_DATA_H
#include <stddef.h>

/* Returns 1 only after validating the supported original executable. */
int game_data_load(const char *path, char *error, size_t error_size);
/* Native first-run importer; 0 also covers the user cancelling selection. */
int game_data_prepare(void);
/* Check the current folder, then reuse/migrate a validated legacy copy. */
int game_data_try_saved(const char *folder, const char *legacy_folder);
#endif
