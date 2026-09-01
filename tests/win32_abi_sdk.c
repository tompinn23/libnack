/* The reference side: the real Windows SDK headers. */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#  define UNICODE 1
#endif
#include <windows.h>
#include <windowsx.h>

#define NACK_ABI_TABLE nack_abi_table_sdk
#define NACK_ABI_COUNT nack_abi_count_sdk
#include "win32_abi_table.h"
