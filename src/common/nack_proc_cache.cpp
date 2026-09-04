/*
 * Cache for GL entry-point lookups.
 *
 * eglGetProcAddress / wglGetProcAddress / dlsym are not free, and a loader
 * such as glad resolves several hundred names at startup - and does it again
 * for every context. Memoising the result keeps repeated loads cheap and makes
 * gl_get_proc_address safe to call from a hot path.
 *
 * Negative results are cached too: an unsupported extension otherwise costs a
 * full failed lookup every time it is queried.
 */
#include "../nack_internal.h"

#define NACK_PROC_CACHE_BITS 10
#define NACK_PROC_CACHE_SIZE (1u << NACK_PROC_CACHE_BITS)
#define NACK_PROC_CACHE_MASK (NACK_PROC_CACHE_SIZE - 1u)
#define NACK_PROC_NAME_MAX 63

struct nack_proc_entry {
    char name[NACK_PROC_NAME_MAX + 1];
    void *proc;
    bool used;
};

namespace nack { namespace detail {

static nack_proc_entry proc_cache[NACK_PROC_CACHE_SIZE];
static size_t proc_cache_used;

static uint32_t proc_hash(const char *s)
{
    /* FNV-1a */
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

void proc_cache_clear(void)
{
    memset(proc_cache, 0, sizeof proc_cache);
    proc_cache_used = 0;
}

void *proc_cache_get(const char *name, void *(*resolve)(const char *))
{
    if (!name || !*name || !resolve)
        return nullptr;

    size_t len = strlen(name);
    if (len > NACK_PROC_NAME_MAX)
        return resolve(name);   /* too long to cache; rare enough not to matter */

    uint32_t index = proc_hash(name) & NACK_PROC_CACHE_MASK;
    for (uint32_t probe = 0; probe < NACK_PROC_CACHE_SIZE; ++probe) {
        nack_proc_entry *entry = &proc_cache[(index + probe) & NACK_PROC_CACHE_MASK];
        if (!entry->used) {
            /* Keep a slot free so lookups always terminate on a miss. */
            if (proc_cache_used + 1 >= NACK_PROC_CACHE_SIZE)
                return resolve(name);
            memcpy(entry->name, name, len + 1);
            entry->proc = resolve(name);
            entry->used = true;
            proc_cache_used++;
            return entry->proc;
        }
        if (strcmp(entry->name, name) == 0)
            return entry->proc;
    }
    return resolve(name);
}

} }   /* namespace nack::detail */
