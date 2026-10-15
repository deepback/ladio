#include <ladio/ladio.h>
#include <ladio/version.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    if (strcmp(ladio_version(), "1.0.0") || ladio_abi_version() != 1) return 1;
    if (ladio_player_create(NULL, 4096) != NULL) return 2;
    if (!ladio_last_error() || !strlen(ladio_last_error())) return 3;
    ladio_player_destroy(NULL);
    printf("LADIO %s C ABI %u: valid version and error boundary\n", ladio_version(), ladio_abi_version());
    return 0;
}
