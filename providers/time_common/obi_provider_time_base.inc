/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_time_datetime_v0.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This shared implementation handles RFC3339 parsing/formatting, fixed-offset
 * civil/unix conversions, and simple timestamp arithmetic for multiple time
 * providers. Zone-specific backends (GLib/ICU) may extend the fixed-offset
 * behavior through the optional extension hooks below.
 */

#ifndef OBI_TIME_PROVIDER_ID
#  define OBI_TIME_PROVIDER_ID "obi.provider:time.inhouse"
#endif

#ifndef OBI_TIME_PROVIDER_VERSION
#  define OBI_TIME_PROVIDER_VERSION "0.1.0"
#endif

#ifndef OBI_TIME_PROVIDER_SPDX
#  define OBI_TIME_PROVIDER_SPDX "MPL-2.0"
#endif

#ifndef OBI_TIME_PROVIDER_LICENSE_CLASS
#  define OBI_TIME_PROVIDER_LICENSE_CLASS "weak_copyleft"
#endif

#ifndef OBI_TIME_PROVIDER_DEPS_JSON
#  define OBI_TIME_PROVIDER_DEPS_JSON "[]"
#endif

#ifndef OBI_TIME_PROVIDER_CAPS
#  define OBI_TIME_PROVIDER_CAPS 0u
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_NS_PER_SEC 1000000000ll
#define OBI_SEC_PER_DAY 86400ll

typedef struct obi_time_provider_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_time_provider_ctx_v0;

static uint32_t _legal_copyleft_from_legacy_class(const char* klass) {
    if (!klass || klass[0] == '\0') {
        return OBI_LEGAL_COPYLEFT_UNKNOWN;
    }
    if (strcmp(klass, "permissive") == 0) {
        return OBI_LEGAL_COPYLEFT_PERMISSIVE;
    }
    if (strcmp(klass, "weak_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_WEAK;
    }
    if (strcmp(klass, "strong_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_STRONG;
    }
    return OBI_LEGAL_COPYLEFT_UNKNOWN;
}

static uint32_t _legal_patent_from_legacy_class(const char* klass) {
    if (!klass || klass[0] == '\0') {
        return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    }
    if (strcmp(klass, "patent_friendly") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT;
    }
    if (strcmp(klass, "patent_sensitive") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_SENSITIVE;
    }
    if (strcmp(klass, "patent_restricted") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_RESTRICTED;
    }
    if (strcmp(klass, "permissive") == 0 ||
        strcmp(klass, "weak_copyleft") == 0 ||
        strcmp(klass, "strong_copyleft") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    }
    return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
}

static bool _is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int _parse_u2(const char* s, int* out_v) {
    if (!s || !out_v || !_is_digit(s[0]) || !_is_digit(s[1])) {
        return 0;
    }
    *out_v = (int)(s[0] - '0') * 10 + (int)(s[1] - '0');
    return 1;
}

static int _parse_u4(const char* s, int* out_v) {
    if (!s || !out_v ||
        !_is_digit(s[0]) || !_is_digit(s[1]) || !_is_digit(s[2]) || !_is_digit(s[3])) {
        return 0;
    }
    *out_v = (int)(s[0] - '0') * 1000 +
             (int)(s[1] - '0') * 100 +
             (int)(s[2] - '0') * 10 +
             (int)(s[3] - '0');
    return 1;
}

static bool _is_leap_year(int32_t year) {
    if ((year % 4) != 0) {
        return false;
    }
    if ((year % 100) != 0) {
        return true;
    }
    return (year % 400) == 0;
}

static uint8_t _days_in_month(int32_t year, uint8_t month) {
    static const uint8_t k_days[12] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };
    if (month < 1u || month > 12u) {
        return 0u;
    }
    if (month == 2u && _is_leap_year(year)) {
        return 29u;
    }
    return k_days[month - 1u];
}

static bool _civil_is_valid(const obi_time_civil_v0* civil) {
    if (!civil) {
        return false;
    }
    if (civil->month < 1u || civil->month > 12u) {
        return false;
    }
    if (civil->day < 1u || civil->day > _days_in_month(civil->year, civil->month)) {
        return false;
    }
    if (civil->hour > 23u || civil->minute > 59u || civil->second > 59u) {
        return false;
    }
    if (civil->nanosecond > 999999999u) {
        return false;
    }
    return true;
}

static bool _add_overflow_i64(int64_t a, int64_t b, int64_t* out) {
    if (!out) {
        return true;
    }
    if (b > 0 && a > (INT64_MAX - b)) {
        return true;
    }
    if (b < 0 && a < (INT64_MIN - b)) {
        return true;
    }
    *out = a + b;
    return false;
}

static bool _sub_overflow_i64(int64_t a, int64_t b, int64_t* out) {
    if (!out) {
        return true;
    }
    if (b == INT64_MIN) {
        if (a >= 0) {
            return true;
        }
        *out = a - b;
        return false;
    }
    return _add_overflow_i64(a, -b, out);
}

static bool _mul_overflow_i64(int64_t a, int64_t b, int64_t* out) {
    if (!out) {
        return true;
    }
    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }

    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return true;
            }
        } else {
            if (b < INT64_MIN / a) {
                return true;
            }
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return true;
            }
        } else {
            if (a != 0 && b < INT64_MAX / a) {
                return true;
            }
        }
    }

    *out = a * b;
    return false;
}

/*
 * Days since 1970-01-01 (civil calendar), re-expressed from Howard Hinnant's
 * published civil-date formulas. This file keeps a local implementation of
 * the formulas and does not embed upstream source text.
 */
static int64_t _days_from_civil(int64_t year, unsigned month, unsigned day) {
    year -= (month <= 2u) ? 1 : 0;
    int64_t era = (year >= 0) ? (year / 400) : ((year - 399) / 400);
    unsigned yoe = (unsigned)(year - era * 400);                                 /* [0, 399] */
    int mp = (int)month + ((month > 2u) ? -3 : 9);
    unsigned doy = (unsigned)((153 * mp + 2) / 5) + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;                     /* [0, 146096] */
    return era * 146097 + (int64_t)doe - 719468;
}

static void _civil_from_days(int64_t z, int32_t* out_year, uint8_t* out_month, uint8_t* out_day) {
    z += 719468;
    int64_t era = (z >= 0) ? (z / 146097) : ((z - 146096) / 146097);
    unsigned doe = (unsigned)(z - era * 146097);                                  /* [0, 146096] */
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;    /* [0, 399] */
    int64_t year = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);                    /* [0, 365] */
    int mp = (int)((5u * doy + 2u) / 153u);                                        /* [0, 11] */
    unsigned day = doy - (unsigned)((153 * mp + 2) / 5) + 1u;                      /* [1, 31] */
    unsigned month = (unsigned)(mp + (mp < 10 ? 3 : -9));                          /* [1, 12] */
    year += (month <= 2u) ? 1 : 0;

    if (out_year) {
        *out_year = (int32_t)year;
    }
    if (out_month) {
        *out_month = (uint8_t)month;
    }
    if (out_day) {
        *out_day = (uint8_t)day;
    }
}

static int64_t _floor_div_i64(int64_t n, int64_t d) {
    int64_t q = n / d;
    int64_t r = n % d;
    if (r != 0 && ((r > 0) != (d > 0))) {
        q -= 1;
    }
    return q;
}

static bool _offset_minutes_is_valid(int32_t offset_minutes) {
    return offset_minutes >= -1439 && offset_minutes <= 1439;
}

static obi_status _resolve_zone_offset(const obi_time_zone_spec_v0* zone, int32_t* out_offset_minutes) {
    if (!zone || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    switch (zone->kind) {
        case OBI_TIME_ZONE_UTC:
            *out_offset_minutes = 0;
            return OBI_STATUS_OK;
        case OBI_TIME_ZONE_FIXED_OFFSET_MINUTES:
            if (!_offset_minutes_is_valid(zone->offset_minutes)) {
                return OBI_STATUS_BAD_ARG;
            }
            *out_offset_minutes = zone->offset_minutes;
            return OBI_STATUS_OK;
        default:
            return OBI_STATUS_UNSUPPORTED;
    }
}

static obi_status _unix_ns_to_civil_fixed(int64_t unix_ns,
                                          int32_t offset_minutes,
                                          obi_time_civil_v0* out_civil) {
    if (!out_civil || !_offset_minutes_is_valid(offset_minutes)) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t offset_sec = 0;
    int64_t offset_ns = 0;
    int64_t local_ns = 0;
    if (_mul_overflow_i64((int64_t)offset_minutes, 60ll, &offset_sec) ||
        _mul_overflow_i64(offset_sec, OBI_NS_PER_SEC, &offset_ns) ||
        _add_overflow_i64(unix_ns, offset_ns, &local_ns)) {
        return OBI_STATUS_ERROR;
    }

    int64_t sec = _floor_div_i64(local_ns, OBI_NS_PER_SEC);
    int64_t nsec = local_ns - sec * OBI_NS_PER_SEC;
    if (nsec < 0) {
        sec -= 1;
        nsec += OBI_NS_PER_SEC;
    }

    int64_t days = _floor_div_i64(sec, OBI_SEC_PER_DAY);
    int64_t sod = sec - days * OBI_SEC_PER_DAY;
    if (sod < 0) {
        days -= 1;
        sod += OBI_SEC_PER_DAY;
    }

    int32_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    _civil_from_days(days, &year, &month, &day);

    memset(out_civil, 0, sizeof(*out_civil));
    out_civil->year = year;
    out_civil->month = month;
    out_civil->day = day;
    out_civil->hour = (uint8_t)(sod / 3600ll);
    out_civil->minute = (uint8_t)((sod % 3600ll) / 60ll);
    out_civil->second = (uint8_t)(sod % 60ll);
    out_civil->nanosecond = (uint32_t)nsec;
    return OBI_STATUS_OK;
}

static obi_status _civil_to_unix_ns_fixed(const obi_time_civil_v0* civil,
                                          int32_t offset_minutes,
                                          int64_t* out_unix_ns) {
    if (!civil || !out_unix_ns || !_civil_is_valid(civil) || !_offset_minutes_is_valid(offset_minutes)) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t days = _days_from_civil((int64_t)civil->year, civil->month, civil->day);
    int64_t day_sec = 0;
    int64_t sec_local = 0;
    int64_t sec_utc = 0;
    int64_t offset_sec = 0;
    int64_t unix_ns = 0;

    int64_t hms_sec = (int64_t)civil->hour * 3600ll +
                      (int64_t)civil->minute * 60ll +
                      (int64_t)civil->second;

    if (_mul_overflow_i64(days, OBI_SEC_PER_DAY, &day_sec) ||
        _add_overflow_i64(day_sec, hms_sec, &sec_local) ||
        _mul_overflow_i64((int64_t)offset_minutes, 60ll, &offset_sec) ||
        _sub_overflow_i64(sec_local, offset_sec, &sec_utc) ||
        _mul_overflow_i64(sec_utc, OBI_NS_PER_SEC, &unix_ns) ||
        _add_overflow_i64(unix_ns, (int64_t)civil->nanosecond, &unix_ns)) {
        return OBI_STATUS_ERROR;
    }

    *out_unix_ns = unix_ns;
    return OBI_STATUS_OK;
}

static obi_status _parse_rfc3339(void* ctx,
                                 const char* s,
                                 int64_t* out_unix_ns,
                                 int32_t* out_offset_minutes) {
    (void)ctx;
    if (!s || !out_unix_ns || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t n = strlen(s);
    if (n < 20u) {
        return OBI_STATUS_BAD_ARG;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    size_t pos = 0u;

    if (!_parse_u4(s + pos, &year)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 4u;
    if (s[pos++] != '-') {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_parse_u2(s + pos, &month)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 2u;
    if (s[pos++] != '-') {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_parse_u2(s + pos, &day)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 2u;
    if (s[pos] != 'T' && s[pos] != 't') {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 1u;
    if (!_parse_u2(s + pos, &hour)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 2u;
    if (s[pos++] != ':') {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_parse_u2(s + pos, &minute)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 2u;
    if (s[pos++] != ':') {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_parse_u2(s + pos, &second)) {
        return OBI_STATUS_BAD_ARG;
    }
    pos += 2u;

    if (month < 1 || month > 12) {
        return OBI_STATUS_BAD_ARG;
    }
    if (day < 1 || day > (int)_days_in_month((int32_t)year, (uint8_t)month)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return OBI_STATUS_BAD_ARG;
    }

    int32_t frac_ns = 0;
    if (pos < n && s[pos] == '.') {
        pos += 1u;
        int digits = 0;
        while (pos < n && _is_digit(s[pos])) {
            if (digits >= 9) {
                return OBI_STATUS_BAD_ARG;
            }
            frac_ns = frac_ns * 10 + (int32_t)(s[pos] - '0');
            digits++;
            pos++;
        }
        if (digits == 0) {
            return OBI_STATUS_BAD_ARG;
        }
        while (digits < 9) {
            frac_ns *= 10;
            digits++;
        }
    }

    int32_t offset_minutes = 0;
    if (pos < n && (s[pos] == 'Z' || s[pos] == 'z')) {
        offset_minutes = 0;
        pos += 1u;
    } else {
        if (pos + 6u > n) {
            return OBI_STATUS_BAD_ARG;
        }
        int sign = 0;
        if (s[pos] == '+') {
            sign = 1;
        } else if (s[pos] == '-') {
            sign = -1;
        } else {
            return OBI_STATUS_BAD_ARG;
        }
        pos += 1u;

        int off_h = 0;
        int off_m = 0;
        if (!_parse_u2(s + pos, &off_h)) {
            return OBI_STATUS_BAD_ARG;
        }
        pos += 2u;
        if (s[pos++] != ':') {
            return OBI_STATUS_BAD_ARG;
        }
        if (!_parse_u2(s + pos, &off_m)) {
            return OBI_STATUS_BAD_ARG;
        }
        pos += 2u;

        if (off_h > 23 || off_m > 59) {
            return OBI_STATUS_BAD_ARG;
        }

        offset_minutes = sign * (off_h * 60 + off_m);
    }

    if (pos != n) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_time_civil_v0 civil;
    memset(&civil, 0, sizeof(civil));
    civil.year = (int32_t)year;
    civil.month = (uint8_t)month;
    civil.day = (uint8_t)day;
    civil.hour = (uint8_t)hour;
    civil.minute = (uint8_t)minute;
    civil.second = (uint8_t)second;
    civil.nanosecond = (uint32_t)frac_ns;

    int64_t unix_ns = 0;
    obi_status st = _civil_to_unix_ns_fixed(&civil, offset_minutes, &unix_ns);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_unix_ns = unix_ns;
    *out_offset_minutes = offset_minutes;
    return OBI_STATUS_OK;
}

static obi_status _format_rfc3339(void* ctx,
                                  int64_t unix_ns,
                                  int32_t offset_minutes,
                                  char* out,
                                  size_t out_cap,
                                  size_t* out_size) {
    (void)ctx;
    if (!out_size || !_offset_minutes_is_valid(offset_minutes)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_time_civil_v0 civil;
    obi_status st = _unix_ns_to_civil_fixed(unix_ns, offset_minutes, &civil);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (civil.year < 0 || civil.year > 9999) {
        return OBI_STATUS_UNSUPPORTED;
    }

    char tz_suffix[16];
    if (offset_minutes == 0) {
        strcpy(tz_suffix, "Z");
    } else {
        int32_t abs_off = (offset_minutes < 0) ? -offset_minutes : offset_minutes;
        int off_h = abs_off / 60;
        int off_m = abs_off % 60;
        (void)snprintf(tz_suffix,
                       sizeof(tz_suffix),
                       "%c%02d:%02d",
                       (offset_minutes < 0) ? '-' : '+',
                       off_h,
                       off_m);
    }

    char frac_suffix[16];
    frac_suffix[0] = '\0';
    if (civil.nanosecond != 0u) {
        (void)snprintf(frac_suffix, sizeof(frac_suffix), ".%09u", (unsigned)civil.nanosecond);
    }

    char tmp[64];
    int wrote = snprintf(tmp,
                         sizeof(tmp),
                         "%04d-%02u-%02uT%02u:%02u:%02u%s%s",
                         civil.year,
                         (unsigned)civil.month,
                         (unsigned)civil.day,
                         (unsigned)civil.hour,
                         (unsigned)civil.minute,
                         (unsigned)civil.second,
                         frac_suffix,
                         tz_suffix);
    if (wrote <= 0 || (size_t)wrote >= sizeof(tmp)) {
        return OBI_STATUS_ERROR;
    }

    *out_size = (size_t)wrote + 1u;
    if (!out || out_cap < *out_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out, tmp, *out_size);
    return OBI_STATUS_OK;
}

static obi_status _unix_ns_to_civil(void* ctx,
                                    int64_t unix_ns,
                                    const obi_time_zone_spec_v0* zone,
                                    obi_time_civil_v0* out_civil,
                                    int32_t* out_offset_minutes) {
    (void)ctx;
    if (!zone || !out_civil || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    int32_t offset_minutes = 0;
    obi_status st = _resolve_zone_offset(zone, &offset_minutes);
    if (st != OBI_STATUS_OK) {
#if defined(OBI_TIME_PROVIDER_UNIX_NS_TO_CIVIL_EXT)
        st = OBI_TIME_PROVIDER_UNIX_NS_TO_CIVIL_EXT(ctx, unix_ns, zone, out_civil, out_offset_minutes);
        if (st != OBI_STATUS_UNSUPPORTED) {
            return st;
        }
#endif
        return st;
    }

    st = _unix_ns_to_civil_fixed(unix_ns, offset_minutes, out_civil);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_offset_minutes = offset_minutes;
    return OBI_STATUS_OK;
}

static obi_status _civil_to_unix_ns(void* ctx,
                                    const obi_time_civil_v0* civil,
                                    const obi_time_zone_spec_v0* zone,
                                    uint32_t flags,
                                    int64_t* out_unix_ns,
                                    int32_t* out_offset_minutes) {
    (void)ctx;
    if (!civil || !zone || !out_unix_ns || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    if ((flags & ~(OBI_TIME_CIVIL_TO_UNIX_PREFER_LATER | OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    int32_t offset_minutes = 0;
    obi_status st = _resolve_zone_offset(zone, &offset_minutes);
    if (st != OBI_STATUS_OK) {
#if defined(OBI_TIME_PROVIDER_CIVIL_TO_UNIX_NS_EXT)
        st = OBI_TIME_PROVIDER_CIVIL_TO_UNIX_NS_EXT(ctx,
                                                    civil,
                                                    zone,
                                                    flags,
                                                    out_unix_ns,
                                                    out_offset_minutes);
        if (st != OBI_STATUS_UNSUPPORTED) {
            return st;
        }
#endif
        return st;
    }

    st = _civil_to_unix_ns_fixed(civil, offset_minutes, out_unix_ns);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_offset_minutes = offset_minutes;
    return OBI_STATUS_OK;
}

static obi_status _add_ns(void* ctx, int64_t unix_ns, int64_t delta_ns, int64_t* out_unix_ns) {
    (void)ctx;
    if (!out_unix_ns) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_add_overflow_i64(unix_ns, delta_ns, out_unix_ns)) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
}

static obi_status _diff_ns(void* ctx, int64_t a_unix_ns, int64_t b_unix_ns, int64_t* out_delta_ns) {
    (void)ctx;
    if (!out_delta_ns) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_sub_overflow_i64(a_unix_ns, b_unix_ns, out_delta_ns)) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
}

static obi_status _cmp(void* ctx, int64_t a_unix_ns, int64_t b_unix_ns, int32_t* out_cmp) {
    (void)ctx;
    if (!out_cmp) {
        return OBI_STATUS_BAD_ARG;
    }
    if (a_unix_ns < b_unix_ns) {
        *out_cmp = -1;
    } else if (a_unix_ns > b_unix_ns) {
        *out_cmp = 1;
    } else {
        *out_cmp = 0;
    }
    return OBI_STATUS_OK;
}

static const obi_time_datetime_api_v0 OBI_TIME_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_time_datetime_api_v0),
    .reserved = 0u,
    .caps = OBI_TIME_PROVIDER_CAPS,
    .parse_rfc3339 = _parse_rfc3339,
    .format_rfc3339 = _format_rfc3339,
    .unix_ns_to_civil = _unix_ns_to_civil,
    .civil_to_unix_ns = _civil_to_unix_ns,
    .add_ns = _add_ns,
    .diff_ns = _diff_ns,
    .cmp = _cmp,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_TIME_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_TIME_PROVIDER_VERSION;
}

static obi_status _get_profile(void* ctx,
                               const char* profile_id,
                               uint32_t profile_abi_major,
                               void* out_profile,
                               size_t out_profile_size) {
    if (!ctx || !profile_id || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (profile_abi_major != OBI_CORE_ABI_MAJOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (strcmp(profile_id, OBI_PROFILE_TIME_DATETIME_V0) == 0) {
        if (out_profile_size < sizeof(obi_time_datetime_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_time_datetime_v0* p = (obi_time_datetime_v0*)out_profile;
        p->api = &OBI_TIME_PROVIDER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_TIME_PROVIDER_ID "\",\"provider_version\":\"" OBI_TIME_PROVIDER_VERSION "\","
           "\"profiles\":[\"obi.profile:time.datetime-0\"],"
           "\"license\":{\"spdx_expression\":\"" OBI_TIME_PROVIDER_SPDX "\",\"class\":\"" OBI_TIME_PROVIDER_LICENSE_CLASS "\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":" OBI_TIME_PROVIDER_DEPS_JSON "}";
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    static const obi_legal_dependency_v0 glib_deps[] = {
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "glib-2.0",
            .name = "glib-2.0",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_WEAK,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "LGPL-2.1-or-later",
            },
        },
    };

    static const obi_legal_dependency_v0 icu_deps[] = {
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "icu-i18n",
            .name = "icu-i18n",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "Unicode-3.0",
            },
        },
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "icu-uc",
            .name = "icu-uc",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "Unicode-3.0",
            },
        },
    };

    const obi_legal_dependency_v0* deps = NULL;
    size_t dep_count = 0u;
    const char* effective_spdx = OBI_TIME_PROVIDER_SPDX;
    const char* effective_summary = "Effective posture equals provider module posture";

    if (strcmp(OBI_TIME_PROVIDER_ID, "obi.provider:time.glib") == 0) {
        deps = glib_deps;
        dep_count = sizeof(glib_deps) / sizeof(glib_deps[0]);
        effective_spdx = "MPL-2.0 AND LGPL-2.1-or-later";
        effective_summary = "Effective posture reflects module plus required GLib dependency";
    } else if (strcmp(OBI_TIME_PROVIDER_ID, "obi.provider:time.icu") == 0) {
        deps = icu_deps;
        dep_count = sizeof(icu_deps) / sizeof(icu_deps[0]);
        effective_spdx = "MPL-2.0 AND Unicode-3.0";
        effective_summary = "Effective posture reflects module plus required ICU dependencies";
    }

    const uint32_t module_copyleft = _legal_copyleft_from_legacy_class(OBI_TIME_PROVIDER_LICENSE_CLASS);
    const uint32_t module_patent = _legal_patent_from_legacy_class(OBI_TIME_PROVIDER_LICENSE_CLASS);

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = module_copyleft;
    out_meta->module_license.patent_posture = module_patent;
    out_meta->module_license.spdx_expression = OBI_TIME_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = module_copyleft;
    out_meta->effective_license.patent_posture = module_patent;
    out_meta->effective_license.spdx_expression = effective_spdx;
    out_meta->effective_license.summary_utf8 = effective_summary;

    out_meta->dependencies = deps;
    out_meta->dependency_count = dep_count;
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_time_provider_ctx_v0* p = (obi_time_provider_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TIME_PROVIDER_ENTRY_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,

    .provider_id = _provider_id,
    .provider_version = _provider_version,
    .get_profile = _get_profile,
    .describe_json = _describe_json,
    .describe_legal_metadata = _describe_legal_metadata,
    .destroy = _destroy,
};

static obi_status _create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_time_provider_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_time_provider_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_time_provider_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_TIME_PROVIDER_ENTRY_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_TIME_PROVIDER_ID,
    .provider_version = OBI_TIME_PROVIDER_VERSION,
    .create = _create,
};
