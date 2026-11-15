/* Test-only PAM module: exercises the real exec helper without account edits. */
#include <security/pam_modules.h>
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *handle, int flags, int argc, const char **argv) {
    (void)flags;
    const char *mode = argc ? argv[0] : "success";
    if (!strcmp(mode, "crash")) _exit(7);
    if (!strcmp(mode, "hang")) for (;;) pause();
    if (!strcmp(mode, "max-tries")) return PAM_MAXTRIES;
    if (!strcmp(mode, "error")) return PAM_SYSTEM_ERR;
    const struct pam_conv *conv;
    if (pam_get_item(handle, PAM_CONV, (const void **)&conv) != PAM_SUCCESS) return PAM_SYSTEM_ERR;
    struct pam_message messages[] = {
        {PAM_TEXT_INFO, "Touch sensor\nsecond line"},
        {PAM_ERROR_MSG, "Try again"},
        {PAM_PROMPT_ECHO_OFF, "Password:"},
        {PAM_PROMPT_ECHO_ON, "Visible challenge:"},
        {PAM_PROMPT_ECHO_OFF, "Last challenge:"}
    };
    const struct pam_message *ptrs[5];
    for (int i = 0; i < 5; ++i) ptrs[i] = &messages[i];
    struct pam_response *responses = NULL;
    int result = conv->conv(5, ptrs, &responses, conv->appdata_ptr);
    if (result != PAM_SUCCESS) return result;
    const char *expected[] = {NULL, NULL, "fixture-answer", "visible\nanswer", ""};
    int ok = 1;
    for (int i = 0; i < 5; ++i) {
        if (expected[i] && (!responses[i].resp || strcmp(responses[i].resp, expected[i]))) ok = 0;
        if (responses[i].resp) {
            explicit_bzero(responses[i].resp, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    free(responses);
    return ok ? PAM_SUCCESS : PAM_AUTH_ERR;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *handle, int flags, int argc, const char **argv) {
    (void)handle; (void)flags; (void)argc; (void)argv;
    return PAM_SUCCESS;
}
