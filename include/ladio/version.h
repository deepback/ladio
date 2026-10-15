#ifndef LADIO_VERSION_H
#define LADIO_VERSION_H
#define LADIO_VERSION_MAJOR 1
#define LADIO_VERSION_MINOR 0
#define LADIO_VERSION_PATCH 0
#define LADIO_VERSION_STRING "1.0.0"
#ifdef __cplusplus
extern "C" {
#endif
const char *ladio_version(void);
unsigned ladio_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
