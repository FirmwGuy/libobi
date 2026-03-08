/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

static obi_status _status_from_errno(int e) {
    switch (e) {
        case 0:
            return OBI_STATUS_OK;
        case EACCES:
        case EPERM:
            return OBI_STATUS_PERMISSION_DENIED;
        case ENOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case ENOENT:
            return OBI_STATUS_UNAVAILABLE;
        case EINVAL:
            return OBI_STATUS_BAD_ARG;
        default:
            return OBI_STATUS_IO_ERROR;
    }
}

static obi_status _write_utf8_out(const char* s,
                                  char* out_buf,
                                  size_t out_cap,
                                  size_t* out_size) {
    if (!s || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t need = strlen(s);
    *out_size = need;
    if (!out_buf || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (need > 0u) {
        memcpy(out_buf, s, need);
    }
    if (out_cap > need) {
        out_buf[need] = '\0';
    }
    return OBI_STATUS_OK;
}

static obi_status _known_dir_join_home(const char* home,
                                       const char* suffix,
                                       char* out_path,
                                       size_t out_cap,
                                       size_t* out_size,
                                       bool* out_found) {
    if (!suffix || !out_size || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!home || home[0] == '\0') {
        *out_found = false;
        *out_size = 0u;
        return OBI_STATUS_OK;
    }

    size_t home_len = strlen(home);
    size_t suffix_len = strlen(suffix);
    size_t total = home_len + suffix_len;

    *out_found = true;
    *out_size = total;
    if (!out_path || out_cap < total) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_path, home, home_len);
    memcpy(out_path + home_len, suffix, suffix_len);
    if (out_cap > total) {
        out_path[total] = '\0';
    }
    return OBI_STATUS_OK;
}
