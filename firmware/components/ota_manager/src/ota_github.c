#include "ota_manager_internal.h"
#include "version.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
    int fields = sscanf(str, "%d.%d.%d%c", major, minor, patch, &trailing);

    /* Accept exactly 3 fields, or 3 fields followed by a pre-release
     * delimiter ('-' or '+') per semver, e.g. "1.2.3-rc1" */
    if (fields == 3) {
        /* Clean version string like "1.2.3" */
    } else if (fields == 4 && (trailing == '-' || trailing == '+')) {
        /* Pre-release or build metadata suffix — ignore it for comparison */
    } else {
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
 *  GitHub release check
 * -------------------------------------------------------------------------*/

/** Maximum response buffer size for GitHub API (from Kconfig, in bytes) */
#define GITHUB_RESPONSE_MAX_SIZE  (CONFIG_OTA_MANAGER_HTTP_BUF_SIZE_KB * 1024)

/**
 * @brief Extract owner/repo from a GitHub URL.
 *
 * Given "https://github.com/SirPangolin/boorker-watchdog", writes
 * "SirPangolin/boorker-watchdog" into @p out.
 *
 * @return true on success
 */
static bool extract_owner_repo(const char *url, char *out, size_t out_size)
{
    const char *prefix = "https://github.com/";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        return false;
    }
    const char *slug = url + strlen(prefix);
    if (strlen(slug) == 0 || strchr(slug, '/') == NULL) {
        return false;
    }
    /* Strip any trailing slash */
    size_t len = strlen(slug);
    while (len > 0 && slug[len - 1] == '/') {
        len--;
    }
    if (len == 0 || len >= out_size) {
        return false;
    }
    memcpy(out, slug, len);
    out[len] = '\0';
    return true;
}

/**
 * @brief Search the release body text for a SHA-256 hash associated with an asset.
 *
 * Looks for 64 consecutive hex characters near the asset filename in the body.
 * Common formats:
 *   boorker-watchdog.bin abc123def456...
 *   SHA256: abc123def456...
 */
static bool find_sha256_in_body(const char *body, const char *asset_name,
                                char *sha256_out, size_t sha256_out_size)
{
    if (body == NULL || asset_name == NULL || sha256_out_size < 65) {
        return false;
    }

    /* Strategy: scan the body for any sequence of 64 hex characters.
     * Prefer a match on the same line as the asset name. */
    const char *search_start = body;

    /* If asset name appears in the body, start searching from there */
    const char *asset_line = strstr(body, asset_name);
    if (asset_line != NULL) {
        search_start = asset_line;
    }

    /* Try from preferred start, then fall back to full body */
    for (int pass = 0; pass < 2; pass++) {
        const char *p = (pass == 0) ? search_start : body;
        /* On second pass, skip if we already searched from the start */
        if (pass == 1 && search_start == body) {
            break;
        }

        while (*p != '\0') {
            if (!isxdigit((unsigned char)*p)) {
                p++;
                continue;
            }
            const char *start = p;
            int count = 0;
            while (isxdigit((unsigned char)*p) && count < 65) {
                count++;
                p++;
            }
            if (count == 64) {
                memcpy(sha256_out, start, 64);
                sha256_out[64] = '\0';
                return true;
            }
        }
    }

    return false;
}

esp_err_t ota_github_check_releases(void)
{
    esp_err_t ret = ESP_FAIL;
    esp_http_client_handle_t client = NULL;
    char *response_buf = NULL;
    cJSON *root = NULL;

    /* ----- Build API URL ------------------------------------------------- */
    char owner_repo[128];
    if (!extract_owner_repo(g_ota.repo_url, owner_repo, sizeof(owner_repo))) {
        ESP_LOGW(TAG, "Failed to extract owner/repo from '%s'", g_ota.repo_url);
        return ESP_FAIL;
    }

    char api_url[256];
    int url_len = snprintf(api_url, sizeof(api_url),
                           "https://api.github.com/repos/%s/releases/latest",
                           owner_repo);
    if (url_len < 0 || (size_t)url_len >= sizeof(api_url)) {
        ESP_LOGW(TAG, "API URL too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Fetching %s", api_url);

    /* ----- HTTP request -------------------------------------------------- */
    esp_http_client_config_t config = {
        .url = api_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "User-Agent", "boorker-watchdog-ota");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status == 403 || status == 429) {
        ESP_LOGW(TAG, "GitHub API rate limited (HTTP %d)", status);
        goto cleanup;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "GitHub API returned HTTP %d", status);
        goto cleanup;
    }

    /* Determine buffer size: use content-length if available, else max */
    int buf_size = (content_length > 0 && content_length < GITHUB_RESPONSE_MAX_SIZE)
                   ? (content_length + 1)
                   : GITHUB_RESPONSE_MAX_SIZE;

    response_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (response_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes from PSRAM", buf_size);
        goto cleanup;
    }

    /* Read response body */
    int total_read = 0;
    int read_len;
    while (total_read < buf_size - 1) {
        read_len = esp_http_client_read(client, response_buf + total_read,
                                        buf_size - 1 - total_read);
        if (read_len <= 0) {
            break;
        }
        total_read += read_len;
    }
    response_buf[total_read] = '\0';

    esp_http_client_close(client);

    if (total_read == 0) {
        ESP_LOGW(TAG, "Empty response body");
        goto cleanup;
    }

    ESP_LOGD(TAG, "Response: %d bytes", total_read);

    /* ----- Parse JSON ---------------------------------------------------- */
    root = cJSON_Parse(response_buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON parse error");
        goto cleanup;
    }

    cJSON *tag_name_json = cJSON_GetObjectItem(root, "tag_name");
    cJSON *prerelease_json = cJSON_GetObjectItem(root, "prerelease");
    cJSON *body_json = cJSON_GetObjectItem(root, "body");
    cJSON *assets_json = cJSON_GetObjectItem(root, "assets");

    if (!cJSON_IsString(tag_name_json)) {
        ESP_LOGW(TAG, "Missing or invalid tag_name");
        goto cleanup;
    }

    const char *tag_name = tag_name_json->valuestring;
    bool is_prerelease = cJSON_IsTrue(prerelease_json);
    const char *body_text = cJSON_IsString(body_json) ? body_json->valuestring : "";

    /* Extract version from tag (strip leading 'v') */
    const char *version = tag_name;
    if (*version == 'v' || *version == 'V') {
        version++;
    }

    ESP_LOGI(TAG, "Latest release: %s (prerelease=%d)", tag_name, is_prerelease);

    /* Channel check: if stable channel and it's a prerelease, skip.
     * Note: /releases/latest already excludes prereleases by definition,
     * so this check is defensive.  When prerelease channel support is
     * needed, switch to the /releases endpoint and iterate the array. */
    if (g_ota.channel == 0 && is_prerelease) {
        ESP_LOGI(TAG, "Skipping prerelease on stable channel");
        g_ota.update_available = false;
        ret = ESP_OK;
        goto cleanup;
    }

    /* Version check */
    if (!ota_github_version_newer(BOORKER_VERSION_STRING, version)) {
        ESP_LOGI(TAG, "No newer version (current=%s, latest=%s)",
                 BOORKER_VERSION_STRING, version);
        g_ota.update_available = false;
        ret = ESP_OK;
        goto cleanup;
    }

    /* ----- Find .bin asset ----------------------------------------------- */
    if (!cJSON_IsArray(assets_json)) {
        ESP_LOGI(TAG, "No assets array in release");
        g_ota.update_available = false;
        ret = ESP_OK;
        goto cleanup;
    }

    const char *bin_url = NULL;
    const char *bin_name = NULL;
    uint32_t bin_size = 0;

    int asset_count = cJSON_GetArraySize(assets_json);
    for (int i = 0; i < asset_count; i++) {
        cJSON *asset = cJSON_GetArrayItem(assets_json, i);
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (!cJSON_IsString(name)) {
            continue;
        }
        const char *aname = name->valuestring;
        size_t alen = strlen(aname);
        if (alen > 4 && strcmp(aname + alen - 4, ".bin") == 0) {
            cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
            cJSON *size = cJSON_GetObjectItem(asset, "size");
            if (cJSON_IsString(url)) {
                bin_url = url->valuestring;
                bin_name = aname;
                bin_size = cJSON_IsNumber(size) ? (uint32_t)size->valuedouble : 0;
                break;
            }
        }
    }

    if (bin_url == NULL) {
        ESP_LOGI(TAG, "No .bin asset found in release");
        g_ota.update_available = false;
        ret = ESP_OK;
        goto cleanup;
    }

    ESP_LOGI(TAG, "Found asset: %s (%lu bytes)", bin_name, (unsigned long)bin_size);

    /* ----- Populate update_info ------------------------------------------ */
    memset(&g_ota.update_info, 0, sizeof(g_ota.update_info));

    strncpy(g_ota.update_info.version, version,
            sizeof(g_ota.update_info.version) - 1);
    strncpy(g_ota.update_info.tag_name, tag_name,
            sizeof(g_ota.update_info.tag_name) - 1);
    strncpy(g_ota.update_info.download_url, bin_url,
            sizeof(g_ota.update_info.download_url) - 1);
    strncpy(g_ota.update_info.release_notes, body_text,
            sizeof(g_ota.update_info.release_notes) - 1);

    g_ota.update_info.is_prerelease = is_prerelease;
    g_ota.update_info.size_bytes = bin_size;

    /* Try to find SHA-256 hash in release body */
    if (!find_sha256_in_body(body_text, bin_name,
                             g_ota.update_info.sha256,
                             sizeof(g_ota.update_info.sha256))) {
        ESP_LOGI(TAG, "No SHA-256 hash found in release notes");
        g_ota.update_info.sha256[0] = '\0';
    } else {
        ESP_LOGI(TAG, "SHA-256: %.16s...", g_ota.update_info.sha256);
    }

    g_ota.update_available = true;
    ret = ESP_OK;

    ESP_LOGI(TAG, "Update available: %s -> %s", BOORKER_VERSION_STRING, version);

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (response_buf != NULL) {
        heap_caps_free(response_buf);
    }
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    return ret;
}
