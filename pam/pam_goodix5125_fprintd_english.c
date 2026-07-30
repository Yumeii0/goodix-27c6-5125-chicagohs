#define _GNU_SOURCE
#include <dlfcn.h>
#include <locale.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__has_include)
# if __has_include(<security/pam_appl.h>) && __has_include(<security/pam_modules.h>)
#  include <security/pam_appl.h>
#  include <security/pam_modules.h>
# else
   typedef struct pam_handle pam_handle_t;
   struct pam_message {
       int msg_style;
       const char *msg;
   };
   struct pam_response {
       char *resp;
       int resp_retcode;
   };
   struct pam_conv {
       int (*conv)(int,
                   const struct pam_message **,
                   struct pam_response **,
                   void *);
       void *appdata_ptr;
   };
#  define PAM_SUCCESS 0
#  define PAM_SYSTEM_ERR 4
#  define PAM_CONV 5
#  define PAM_CONV_ERR 19
# endif
#else
# include <security/pam_appl.h>
# include <security/pam_modules.h>
#endif

#ifndef PAM_EXTERN
#define PAM_EXTERN
#endif

#ifndef REAL_PAM_FPRINTD
#define REAL_PAM_FPRINTD "/usr/lib/security/pam_fprintd.so"
#endif
#define GENERIC_FINGER_PROMPT "Place your finger on the fingerprint reader"

typedef int (*pam_entry_fn)(pam_handle_t *, int, int, const char **);
typedef int (*pam_get_item_fn)(const pam_handle_t *, int, const void **);
typedef int (*pam_set_item_fn)(pam_handle_t *, int, const void *);

typedef struct {
    const struct pam_conv *original;
} PromptProxy;

static pthread_mutex_t locale_lock = PTHREAD_MUTEX_INITIALIZER;

static int
real_module_is_safe(void)
{
    static const char trusted_prefix[] = "/usr/lib/security/";
    struct stat st;
    char *resolved = NULL;
    int safe = 0;

    /* Arch/CachyOS may ship pam_fprintd.so as a root-owned symlink to a
     * versioned module. Resolve it first, then validate the real target. */
    resolved = realpath(REAL_PAM_FPRINTD, NULL);
    if (resolved == NULL)
        return 0;
    if (strncmp(resolved, trusted_prefix, sizeof(trusted_prefix) - 1U) != 0)
        goto out;
    if (stat(resolved, &st) != 0)
        goto out;
    if (!S_ISREG(st.st_mode) || st.st_uid != 0)
        goto out;
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        goto out;
    safe = 1;

out:
    free(resolved);
    return safe;
}

static int
is_fingerprint_placement_prompt(const char *text)
{
    static const char prefix[] = "Place your ";
    static const char required[] = "finger on the fingerprint reader";

    if (text == NULL)
        return 0;
    if (strncmp(text, prefix, sizeof(prefix) - 1U) != 0)
        return 0;
    return strstr(text, required) != NULL;
}

static int
proxy_conversation(int num_msg,
                   const struct pam_message **messages,
                   struct pam_response **responses,
                   void *appdata_ptr)
{
    PromptProxy *proxy = appdata_ptr;
    const struct pam_message **forward = NULL;
    struct pam_message *copies = NULL;
    int result = PAM_CONV_ERR;
    int index;

    if (proxy == NULL || proxy->original == NULL ||
        proxy->original->conv == NULL || num_msg <= 0 ||
        messages == NULL || responses == NULL)
        return PAM_CONV_ERR;

    forward = calloc((size_t) num_msg, sizeof(*forward));
    copies = calloc((size_t) num_msg, sizeof(*copies));
    if (forward == NULL || copies == NULL)
        goto out;

    for (index = 0; index < num_msg; index++) {
        if (messages[index] == NULL)
            goto out;
        copies[index] = *messages[index];
        if (is_fingerprint_placement_prompt(copies[index].msg))
            copies[index].msg = GENERIC_FINGER_PROMPT;
        forward[index] = &copies[index];
    }

    result = proxy->original->conv(num_msg,
                                   forward,
                                   responses,
                                   proxy->original->appdata_ptr);

out:
    free(copies);
    free(forward);
    return result;
}

static int
call_real(const char *symbol,
          pam_handle_t *pamh,
          int flags,
          int argc,
          const char **argv)
{
    void *pam_library = NULL;
    void *handle = NULL;
    pam_entry_fn entry = NULL;
    pam_get_item_fn get_item = NULL;
    pam_set_item_fn set_item = NULL;
    const struct pam_conv *original_conv = NULL;
    struct pam_conv original_conv_copy;
    struct pam_conv wrapped_conv;
    PromptProxy proxy;
    const char *current_locale = NULL;
    char *saved_locale = NULL;
    int conversation_replaced = 0;
    int result = PAM_SYSTEM_ERR;

    if (!real_module_is_safe())
        return PAM_SYSTEM_ERR;

    if (pthread_mutex_lock(&locale_lock) != 0)
        return PAM_SYSTEM_ERR;

    current_locale = setlocale(LC_ALL, NULL);
    if (current_locale != NULL)
        saved_locale = strdup(current_locale);

    /* Keep pam_fprintd messages in English, then replace the finger-specific
     * placement sentence with a generic prompt that works for any enrolled
     * finger. The matching and authentication logic remains upstream code. */
    if (setlocale(LC_ALL, "C") == NULL)
        goto out;

    /* A PAM client may load libpam with RTLD_LOCAL (for example through
     * Python ctypes). In that case RTLD_DEFAULT cannot see pam_get_item and
     * pam_set_item, and a delegated PAM module may also fail to resolve PAM
     * symbols. Reopen libpam globally before loading the real module, and
     * resolve the two APIs from that explicit handle. */
    pam_library = dlopen("libpam.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (pam_library == NULL)
        goto out;

    *(void **) (&get_item) = dlsym(pam_library, "pam_get_item");
    *(void **) (&set_item) = dlsym(pam_library, "pam_set_item");
    if (get_item == NULL || set_item == NULL)
        goto out;

    handle = dlopen(REAL_PAM_FPRINTD, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL)
        goto out;

    *(void **) (&entry) = dlsym(handle, symbol);
    if (entry == NULL)
        goto out;

    if (get_item(pamh, PAM_CONV, (const void **) &original_conv) != PAM_SUCCESS ||
        original_conv == NULL || original_conv->conv == NULL)
        goto out;

    /* pam_set_item(PAM_CONV) may replace storage backing original_conv.
     * Preserve the callback and appdata by value before changing the item. */
    original_conv_copy = *original_conv;
    proxy.original = &original_conv_copy;
    wrapped_conv.conv = proxy_conversation;
    wrapped_conv.appdata_ptr = &proxy;
    if (set_item(pamh, PAM_CONV, &wrapped_conv) != PAM_SUCCESS)
        goto out;
    conversation_replaced = 1;

    result = entry(pamh, flags, argc, argv);

out:
    if (conversation_replaced && set_item != NULL)
        (void) set_item(pamh, PAM_CONV, &original_conv_copy);
    if (handle != NULL)
        dlclose(handle);
    if (pam_library != NULL)
        dlclose(pam_library);
    if (saved_locale != NULL) {
        (void) setlocale(LC_ALL, saved_locale);
        free(saved_locale);
    }
    (void) pthread_mutex_unlock(&locale_lock);
    return result;
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return call_real("pam_sm_authenticate", pamh, flags, argc, argv);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return call_real("pam_sm_setcred", pamh, flags, argc, argv);
}

PAM_EXTERN int
pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    return call_real("pam_sm_chauthtok", pamh, flags, argc, argv);
}
