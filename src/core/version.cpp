#include "ladio/version.h"
extern "C" const char *ladio_version(void) { return LADIO_VERSION_STRING; }
extern "C" unsigned ladio_abi_version(void) { return 1; }
