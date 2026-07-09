#ifndef KEYSCLICKS_PRIVACY_PROVIDER_H
#define KEYSCLICKS_PRIVACY_PROVIDER_H
/* Public plugin ABI for external privacy providers. Part of the base app
 * (Apache-2.0). ABI v1 is FROZEN: additive-only; bump the version macro on any
 * incompatible change. Field order and types below must not change. */
#include <stdbool.h>
#include <stdint.h>

#define KC_PRIVACY_ABI_VERSION 1u

typedef struct KcPrivacyProvider {
    uint32_t     abi_version;                 /* must equal KC_PRIVACY_ABI_VERSION */
    const char  *name;                        /* provider name, for logs */
    void        *(*init)(void);               /* create ctx; return NULL on failure */
    void         (*shutdown)(void *ctx);      /* free ctx; may be NULL */
    bool         (*should_mask_keys)(void *ctx); /* true => hide keystrokes right now */
} KcPrivacyProvider;

/* The single symbol every plugin .so must export. */
const KcPrivacyProvider *kc_privacy_provider_get(void);

#endif /* KEYSCLICKS_PRIVACY_PROVIDER_H */
