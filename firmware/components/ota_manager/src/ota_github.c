#include "ota_manager_internal.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "ota_github";

/* ---------------------------------------------------------------------------
 *  Semver helpers
 * -------------------------------------------------------------------------*/

/**
 * @brief Parse a "MAJOR.MINOR.PATCH" string, optionally prefixed with 'v'/'V'.
 *
 * @return true if exactly three numeric fields were parsed.
 */
static bool parse_version(const char *str, int *major, int *minor, int *patch)
{
    if (str == NULL) {
        return false;
    }

    /* Skip optional leading 'v' or 'V' */
    if (*str == 'v' || *str == 'V') {
        str++;
    }

    char trailing = '\0';
    if (sscanf(str, "%d.%d.%d%c", major, minor, patch, &trailing) != 3) {
        return false;
    }

    if (*major < 0 || *minor < 0 || *patch < 0) {
        return false;
    }

    return true;
}

bool ota_github_version_newer(const char *current, const char *candidate)
{
    int cur_major, cur_minor, cur_patch;
    int cand_major, cand_minor, cand_patch;

    if (!parse_version(current, &cur_major, &cur_minor, &cur_patch) ||
        !parse_version(candidate, &cand_major, &cand_minor, &cand_patch)) {
        ESP_LOGW(TAG, "Failed to parse version strings (current='%s', candidate='%s')",
                 current ? current : "(null)",
                 candidate ? candidate : "(null)");
        return false;
    }

    if (cand_major != cur_major) {
        return cand_major > cur_major;
    }
    if (cand_minor != cur_minor) {
        return cand_minor > cur_minor;
    }
    return cand_patch > cur_patch;
}

/* ---------------------------------------------------------------------------
 *  GitHub release check (Task 3.2 – stub)
 * -------------------------------------------------------------------------*/

esp_err_t ota_github_check_releases(void)
{
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}
