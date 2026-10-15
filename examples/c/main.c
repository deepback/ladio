#include <ladio/ladio.h>
#include <ladio/version.h>
#include <stdio.h>
int main(void) {
    printf("LADIO %s / C ABI %u\n", ladio_version(), ladio_abi_version());
    return ladio_abi_version() == 1 ? 0 : 1;
}
