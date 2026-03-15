/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_time_datetime_v0.h>

#include <glib.h>

#include <limits.h>
#include <stdint.h>

static obi_status _obi_time_glib_unix_ns_to_civil_ext(void* ctx,
                                                       int64_t unix_ns,
                                                       const obi_time_zone_spec_v0* zone,
                                                       obi_time_civil_v0* out_civil,
                                                       int32_t* out_offset_minutes);

static obi_status _obi_time_glib_civil_to_unix_ns_ext(void* ctx,
                                                       const obi_time_civil_v0* civil,
                                                       const obi_time_zone_spec_v0* zone,
                                                       uint32_t flags,
                                                       int64_t* out_unix_ns,
                                                       int32_t* out_offset_minutes);

#define OBI_TIME_PROVIDER_ID "obi.provider:time.glib"
#define OBI_TIME_PROVIDER_VERSION "0.1.0"
#define OBI_TIME_PROVIDER_SPDX "MPL-2.0"
#define OBI_TIME_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_TIME_PROVIDER_DEPS_JSON \
    "[{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}]"
#define OBI_TIME_PROVIDER_CAPS (OBI_TIME_DATETIME_CAP_TZ_IANA | OBI_TIME_DATETIME_CAP_TZ_LOCAL)
#define OBI_TIME_PROVIDER_UNIX_NS_TO_CIVIL_EXT _obi_time_glib_unix_ns_to_civil_ext
#define OBI_TIME_PROVIDER_CIVIL_TO_UNIX_NS_EXT _obi_time_glib_civil_to_unix_ns_ext

#include "../time_common/obi_provider_time_base.inc"

static GTimeZone* _obi_time_glib_create_zone(const obi_time_zone_spec_v0* zone) {
    if (!zone) {
        return NULL;
    }

    switch (zone->kind) {
        case OBI_TIME_ZONE_LOCAL:
            return g_time_zone_new_local();
        case OBI_TIME_ZONE_IANA_NAME:
            if (!zone->iana_name || zone->iana_name[0] == '\0') {
                return NULL;
            }
            return g_time_zone_new_identifier(zone->iana_name);
        default:
            return NULL;
    }
}

static obi_status _obi_time_glib_unix_ns_to_civil_ext(void* ctx,
                                                       int64_t unix_ns,
                                                       const obi_time_zone_spec_v0* zone,
                                                       obi_time_civil_v0* out_civil,
                                                       int32_t* out_offset_minutes) {
    (void)ctx;
    if (!zone || !out_civil || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    if (zone->kind != OBI_TIME_ZONE_LOCAL && zone->kind != OBI_TIME_ZONE_IANA_NAME) {
        return OBI_STATUS_UNSUPPORTED;
    }

    GTimeZone* tz = _obi_time_glib_create_zone(zone);
    if (!tz) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t sec = unix_ns / OBI_NS_PER_SEC;
    int64_t nsec = unix_ns % OBI_NS_PER_SEC;
    if (nsec < 0) {
        sec -= 1;
        nsec += OBI_NS_PER_SEC;
    }

    GDateTime* dt_utc = g_date_time_new_from_unix_utc((gint64)sec);
    if (!dt_utc) {
        g_time_zone_unref(tz);
        return OBI_STATUS_ERROR;
    }

    GDateTime* dt_local = g_date_time_to_timezone(dt_utc, tz);
    g_date_time_unref(dt_utc);
    g_time_zone_unref(tz);
    if (!dt_local) {
        return OBI_STATUS_ERROR;
    }

    memset(out_civil, 0, sizeof(*out_civil));
    out_civil->year = (int32_t)g_date_time_get_year(dt_local);
    out_civil->month = (uint8_t)g_date_time_get_month(dt_local);
    out_civil->day = (uint8_t)g_date_time_get_day_of_month(dt_local);
    out_civil->hour = (uint8_t)g_date_time_get_hour(dt_local);
    out_civil->minute = (uint8_t)g_date_time_get_minute(dt_local);
    out_civil->second = (uint8_t)g_date_time_get_second(dt_local);
    out_civil->nanosecond = (uint32_t)nsec;
    if (!_civil_is_valid(out_civil)) {
        g_date_time_unref(dt_local);
        return OBI_STATUS_ERROR;
    }

    gint64 off_us = g_date_time_get_utc_offset(dt_local);
    if ((off_us % (60ll * G_TIME_SPAN_SECOND)) != 0ll) {
        g_date_time_unref(dt_local);
        return OBI_STATUS_ERROR;
    }

    int64_t off_minutes = off_us / (60ll * G_TIME_SPAN_SECOND);
    if (off_minutes < INT32_MIN || off_minutes > INT32_MAX) {
        g_date_time_unref(dt_local);
        return OBI_STATUS_ERROR;
    }
    int32_t offset_minutes = (int32_t)off_minutes;
    if (!_offset_minutes_is_valid(offset_minutes)) {
        g_date_time_unref(dt_local);
        return OBI_STATUS_ERROR;
    }
    *out_offset_minutes = offset_minutes;

    g_date_time_unref(dt_local);
    return OBI_STATUS_OK;
}

static obi_status _obi_time_glib_civil_to_unix_ns_ext(void* ctx,
                                                       const obi_time_civil_v0* civil,
                                                       const obi_time_zone_spec_v0* zone,
                                                       uint32_t flags,
                                                       int64_t* out_unix_ns,
                                                       int32_t* out_offset_minutes) {
    (void)ctx;
    if (!civil || !zone || !out_unix_ns || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!_civil_is_valid(civil)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (zone->kind != OBI_TIME_ZONE_LOCAL && zone->kind != OBI_TIME_ZONE_IANA_NAME) {
        return OBI_STATUS_UNSUPPORTED;
    }

    GTimeZone* tz = _obi_time_glib_create_zone(zone);
    if (!tz) {
        return OBI_STATUS_BAD_ARG;
    }

    GDateTime* dt = g_date_time_new(tz,
                                    (gint)civil->year,
                                    (gint)civil->month,
                                    (gint)civil->day,
                                    (gint)civil->hour,
                                    (gint)civil->minute,
                                    (gdouble)civil->second);
    g_time_zone_unref(tz);
    if (!dt) {
        return OBI_STATUS_ERROR;
    }

    if ((flags & OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID) != 0u) {
        if ((int32_t)g_date_time_get_year(dt) != civil->year ||
            (uint8_t)g_date_time_get_month(dt) != civil->month ||
            (uint8_t)g_date_time_get_day_of_month(dt) != civil->day ||
            (uint8_t)g_date_time_get_hour(dt) != civil->hour ||
            (uint8_t)g_date_time_get_minute(dt) != civil->minute ||
            (uint8_t)g_date_time_get_second(dt) != civil->second) {
            g_date_time_unref(dt);
            return OBI_STATUS_ERROR;
        }
    }

    const int64_t unix_sec = (int64_t)g_date_time_to_unix(dt);
    int64_t base_ns = 0;
    if (unix_sec < 0 && civil->nanosecond != 0u) {
        int64_t adjusted_sec = 0;
        const int64_t negative_tail = (int64_t)civil->nanosecond - OBI_NS_PER_SEC;
        if (_add_overflow_i64(unix_sec, 1ll, &adjusted_sec) ||
            _mul_overflow_i64(adjusted_sec, OBI_NS_PER_SEC, &base_ns) ||
            _add_overflow_i64(base_ns, negative_tail, out_unix_ns)) {
            g_date_time_unref(dt);
            return OBI_STATUS_ERROR;
        }
    } else if (_mul_overflow_i64(unix_sec, OBI_NS_PER_SEC, &base_ns) ||
               _add_overflow_i64(base_ns, (int64_t)civil->nanosecond, out_unix_ns)) {
        g_date_time_unref(dt);
        return OBI_STATUS_ERROR;
    }

    gint64 off_us = g_date_time_get_utc_offset(dt);
    if ((off_us % (60ll * G_TIME_SPAN_SECOND)) != 0ll) {
        g_date_time_unref(dt);
        return OBI_STATUS_ERROR;
    }
    int64_t off_minutes = off_us / (60ll * G_TIME_SPAN_SECOND);
    if (off_minutes < INT32_MIN || off_minutes > INT32_MAX) {
        g_date_time_unref(dt);
        return OBI_STATUS_ERROR;
    }
    int32_t offset_minutes = (int32_t)off_minutes;
    if (!_offset_minutes_is_valid(offset_minutes)) {
        g_date_time_unref(dt);
        return OBI_STATUS_ERROR;
    }
    *out_offset_minutes = offset_minutes;
    g_date_time_unref(dt);

    int64_t earliest_unix_ns = *out_unix_ns;
    int32_t earliest_offset_minutes = *out_offset_minutes;
    int64_t latest_unix_ns = *out_unix_ns;
    int32_t latest_offset_minutes = *out_offset_minutes;
    bool ambiguous = false;
    obi_status st = _civil_mapping_window_with_ext(ctx,
                                                   civil,
                                                   zone,
                                                   *out_unix_ns,
                                                   *out_offset_minutes,
                                                   _obi_time_glib_unix_ns_to_civil_ext,
                                                   &earliest_unix_ns,
                                                   &earliest_offset_minutes,
                                                   &latest_unix_ns,
                                                   &latest_offset_minutes,
                                                   &ambiguous);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (ambiguous && (flags & OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID) != 0u) {
        return OBI_STATUS_ERROR;
    }

    if ((flags & OBI_TIME_CIVIL_TO_UNIX_PREFER_LATER) != 0u) {
        *out_unix_ns = latest_unix_ns;
        *out_offset_minutes = latest_offset_minutes;
    } else {
        *out_unix_ns = earliest_unix_ns;
        *out_offset_minutes = earliest_offset_minutes;
    }

    return OBI_STATUS_OK;
}
