/* The side under test: libnack's hand-rolled declarations. */
#define NACK_WIN32_ABI_CHECK 1   /* we know windows.h is not in scope here */
#include "win32/nack_win32_api.h"

#define NACK_ABI_TABLE nack_abi_table_nack
#define NACK_ABI_COUNT nack_abi_count_nack
#include "win32_abi_table.h"
