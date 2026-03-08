/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_time_datetime_v0.h>

#include <unicode/ucal.h>
#include <unicode/ustring.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static obi_status _obi_time_icu_unix_ns_to_civil_ext(void* ctx,
                                                      int64_t unix_ns,
                                                      const obi_time_zone_spec_v0* zone,
                                                      obi_time_civil_v0* out_civil,
                                                      int32_t* out_offset_minutes);

static obi_status _obi_time_icu_civil_to_unix_ns_ext(void* ctx,
                                                      const obi_time_civil_v0* civil,
                                                      const obi_time_zone_spec_v0* zone,
                                                      uint32_t flags,
                                                      int64_t* out_unix_ns,
                                                      int32_t* out_offset_minutes);

#define OBI_TIME_PROVIDER_ID "obi.provider:time.icu"
#define OBI_TIME_PROVIDER_VERSION "0.1.0"
#define OBI_TIME_PROVIDER_SPDX "MPL-2.0"
#define OBI_TIME_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_TIME_PROVIDER_DEPS_JSON \
    "[{\"name\":\"icu-i18n\",\"version\":\"dynamic\",\"spdx_expression\":\"Unicode-3.0\",\"class\":\"permissive\"}," \
    "{\"name\":\"icu-uc\",\"version\":\"dynamic\",\"spdx_expression\":\"Unicode-3.0\",\"class\":\"permissive\"}]"
#define OBI_TIME_PROVIDER_CAPS (OBI_TIME_DATETIME_CAP_TZ_IANA | OBI_TIME_DATETIME_CAP_TZ_LOCAL)
#define OBI_TIME_PROVIDER_UNIX_NS_TO_CIVIL_EXT _obi_time_icu_unix_ns_to_civil_ext
#define OBI_TIME_PROVIDER_CIVIL_TO_UNIX_NS_EXT _obi_time_icu_civil_to_unix_ns_ext
#include "../time_common/obi_provider_time_base.inc"

static int _obi_time_icu_civil_equal(const obi_time_civil_v0* a, const obi_time_civil_v0* b) {
    if (!a || !b) {
        return 0;
    }
    return a->year == b->year &&
           a->month == b->month &&
           a->day == b->day &&
           a->hour == b->hour &&
           a->minute == b->minute &&
           a->second == b->second &&
           a->nanosecond == b->nanosecond;
}

static obi_status _obi_time_icu_zone_id_from_spec(const obi_time_zone_spec_v0* zone,
                                                   UChar** out_zone_id,
                                                   int32_t* out_zone_len) {
    if (!zone || !out_zone_id || !out_zone_len) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_zone_id = NULL;
    *out_zone_len = 0;

    if (zone->kind == OBI_TIME_ZONE_LOCAL) {
        UErrorCode err = U_ZERO_ERROR;
        int32_t need = ucal_getDefaultTimeZone(NULL, 0, &err);
        if (err != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(err)) {
            return OBI_STATUS_UNAVAILABLE;
        }

        UChar* zone_id = (UChar*)malloc((size_t)(need + 1) * sizeof(UChar));
        if (!zone_id) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        err = U_ZERO_ERROR;
        int32_t written = ucal_getDefaultTimeZone(zone_id, need + 1, &err);
        if (U_FAILURE(err) || written <= 0) {
            free(zone_id);
            return OBI_STATUS_UNAVAILABLE;
        }

        zone_id[written] = 0;
        *out_zone_id = zone_id;
        *out_zone_len = written;
        return OBI_STATUS_OK;
    }

    if (zone->kind == OBI_TIME_ZONE_IANA_NAME) {
        if (!zone->iana_name || zone->iana_name[0] == '\0') {
            return OBI_STATUS_BAD_ARG;
        }

        UErrorCode err = U_ZERO_ERROR;
        int32_t need = 0;
        u_strFromUTF8(NULL, 0, &need, zone->iana_name, -1, &err);
        if (err != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(err)) {
            return OBI_STATUS_BAD_ARG;
        }

        UChar* zone_id = (UChar*)malloc((size_t)(need + 1) * sizeof(UChar));
        if (!zone_id) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        err = U_ZERO_ERROR;
        int32_t written = 0;
        u_strFromUTF8(zone_id, need + 1, &written, zone->iana_name, -1, &err);
        if (U_FAILURE(err) || written <= 0) {
            free(zone_id);
            return OBI_STATUS_BAD_ARG;
        }

        zone_id[written] = 0;
        *out_zone_id = zone_id;
        *out_zone_len = written;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static obi_status _obi_time_icu_open_calendar(const obi_time_zone_spec_v0* zone,
                                               UCalendar** out_cal) {
    if (!zone || !out_cal) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* zone_id = NULL;
    int32_t zone_len = 0;
    obi_status st = _obi_time_icu_zone_id_from_spec(zone, &zone_id, &zone_len);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    UCalendar* cal = ucal_open(zone_id, zone_len, NULL, UCAL_GREGORIAN, &err);
    free(zone_id);
    if (!cal || U_FAILURE(err)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    ucal_setAttribute(cal, UCAL_LENIENT, 1);
    *out_cal = cal;
    return OBI_STATUS_OK;
}

static obi_status _obi_time_icu_offset_minutes(UCalendar* cal, int32_t* out_offset_minutes) {
    if (!cal || !out_offset_minutes) {
        return OBI_STATUS_BAD_ARG;
    }

    UErrorCode err = U_ZERO_ERROR;
    int32_t zone_ms = ucal_get(cal, UCAL_ZONE_OFFSET, &err);
    int32_t dst_ms = ucal_get(cal, UCAL_DST_OFFSET, &err);
    if (U_FAILURE(err)) {
        return OBI_STATUS_ERROR;
    }

    int64_t total_ms = (int64_t)zone_ms + (int64_t)dst_ms;
    if ((total_ms % 60000ll) != 0ll) {
        return OBI_STATUS_ERROR;
    }

    int64_t minutes = total_ms / 60000ll;
    if (minutes < INT32_MIN || minutes > INT32_MAX) {
        return OBI_STATUS_ERROR;
    }

    int32_t offset_minutes = (int32_t)minutes;
    if (!_offset_minutes_is_valid(offset_minutes)) {
        return OBI_STATUS_ERROR;
    }

    *out_offset_minutes = offset_minutes;
    return OBI_STATUS_OK;
}

static obi_status _obi_time_icu_set_calendar_unix_ns(UCalendar* cal,
                                                      int64_t unix_ns,
                                                      uint32_t* out_nanosecond) {
    if (!cal) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t sec = _floor_div_i64(unix_ns, OBI_NS_PER_SEC);
    int64_t nsec = unix_ns - sec * OBI_NS_PER_SEC;
    if (nsec < 0) {
        sec -= 1;
        nsec += OBI_NS_PER_SEC;
    }

    int64_t ms = 0;
    if (_mul_overflow_i64(sec, 1000ll, &ms) || _add_overflow_i64(ms, nsec / 1000000ll, &ms)) {
        return OBI_STATUS_ERROR;
    }

    UErrorCode err = U_ZERO_ERROR;
    ucal_setMillis(cal, (UDate)ms, &err);
    if (U_FAILURE(err)) {
        return OBI_STATUS_ERROR;
    }

    if (out_nanosecond) {
        *out_nanosecond = (uint32_t)nsec;
    }
    return OBI_STATUS_OK;
}

static obi_status _obi_time_icu_calendar_to_unix_ns(UCalendar* cal,
                                                     uint32_t nanosecond,
                                                     int64_t* out_unix_ns) {
    if (!cal || !out_unix_ns) {
        return OBI_STATUS_BAD_ARG;
    }

    UErrorCode err = U_ZERO_ERROR;
    UDate millis = ucal_getMillis(cal, &err);
    if (U_FAILURE(err)) {
        return OBI_STATUS_ERROR;
    }

    if (millis > (UDate)INT64_MAX || millis < (UDate)INT64_MIN) {
        return OBI_STATUS_ERROR;
    }

    int64_t ms_i64 = (int64_t)millis;
    int64_t unix_ns = 0;
    if (_mul_overflow_i64(ms_i64, 1000000ll, &unix_ns) ||
        _add_overflow_i64(unix_ns, (int64_t)(nanosecond % 1000000u), &unix_ns)) {
        return OBI_STATUS_ERROR;
    }

    *out_unix_ns = unix_ns;
    return OBI_STATUS_OK;
}

static obi_status _obi_time_icu_unix_ns_to_civil_ext(void* ctx,
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

    UCalendar* cal = NULL;
    obi_status st = _obi_time_icu_open_calendar(zone, &cal);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    uint32_t nanosecond = 0u;
    st = _obi_time_icu_set_calendar_unix_ns(cal, unix_ns, &nanosecond);
    if (st != OBI_STATUS_OK) {
        ucal_close(cal);
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    int32_t year = ucal_get(cal, UCAL_YEAR, &err);
    int32_t month0 = ucal_get(cal, UCAL_MONTH, &err);
    int32_t day = ucal_get(cal, UCAL_DATE, &err);
    int32_t hour = ucal_get(cal, UCAL_HOUR_OF_DAY, &err);
    int32_t minute = ucal_get(cal, UCAL_MINUTE, &err);
    int32_t second = ucal_get(cal, UCAL_SECOND, &err);
    if (U_FAILURE(err)) {
        ucal_close(cal);
        return OBI_STATUS_ERROR;
    }

    memset(out_civil, 0, sizeof(*out_civil));
    out_civil->year = year;
    out_civil->month = (uint8_t)(month0 + 1);
    out_civil->day = (uint8_t)day;
    out_civil->hour = (uint8_t)hour;
    out_civil->minute = (uint8_t)minute;
    out_civil->second = (uint8_t)second;
    out_civil->nanosecond = nanosecond;

    if (!_civil_is_valid(out_civil)) {
        ucal_close(cal);
        return OBI_STATUS_ERROR;
    }

    st = _obi_time_icu_offset_minutes(cal, out_offset_minutes);
    ucal_close(cal);
    return st;
}

static obi_status _obi_time_icu_civil_to_unix_ns_ext(void* ctx,
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

    UCalendar* cal = NULL;
    obi_status st = _obi_time_icu_open_calendar(zone, &cal);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    ucal_clear(cal);
    ucal_setDateTime(cal,
                     civil->year,
                     (int32_t)civil->month - 1,
                     civil->day,
                     civil->hour,
                     civil->minute,
                     civil->second,
                     &err);
    if (U_FAILURE(err)) {
        ucal_close(cal);
        return OBI_STATUS_BAD_ARG;
    }

    ucal_set(cal, UCAL_MILLISECOND, (int32_t)(civil->nanosecond / 1000000u));

    int32_t got_month0 = ucal_get(cal, UCAL_MONTH, &err);
    int32_t got_day = ucal_get(cal, UCAL_DATE, &err);
    int32_t got_hour = ucal_get(cal, UCAL_HOUR_OF_DAY, &err);
    int32_t got_minute = ucal_get(cal, UCAL_MINUTE, &err);
    int32_t got_second = ucal_get(cal, UCAL_SECOND, &err);
    if (U_FAILURE(err)) {
        ucal_close(cal);
        return OBI_STATUS_ERROR;
    }

    if ((flags & OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID) != 0u) {
        if ((uint8_t)(got_month0 + 1) != civil->month ||
            (uint8_t)got_day != civil->day ||
            (uint8_t)got_hour != civil->hour ||
            (uint8_t)got_minute != civil->minute ||
            (uint8_t)got_second != civil->second) {
            ucal_close(cal);
            return OBI_STATUS_ERROR;
        }
    }

    st = _obi_time_icu_calendar_to_unix_ns(cal, civil->nanosecond, out_unix_ns);
    if (st != OBI_STATUS_OK) {
        ucal_close(cal);
        return st;
    }

    st = _obi_time_icu_offset_minutes(cal, out_offset_minutes);
    ucal_close(cal);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if ((flags & OBI_TIME_CIVIL_TO_UNIX_PREFER_LATER) != 0u) {
        int64_t chosen_unix_ns = *out_unix_ns;
        int32_t chosen_offset = *out_offset_minutes;

        int64_t hour_ns = 0;
        if (_mul_overflow_i64(3600ll, OBI_NS_PER_SEC, &hour_ns)) {
            return OBI_STATUS_ERROR;
        }

        for (int i = 1; i <= 4; i++) {
            int64_t delta_ns = 0;
            int64_t candidate_unix_ns = 0;
            if (_mul_overflow_i64((int64_t)i, hour_ns, &delta_ns) ||
                _add_overflow_i64(*out_unix_ns, delta_ns, &candidate_unix_ns)) {
                break;
            }

            obi_time_civil_v0 probe_civil;
            memset(&probe_civil, 0, sizeof(probe_civil));
            int32_t probe_offset = 0;
            st = _obi_time_icu_unix_ns_to_civil_ext(ctx,
                                                    candidate_unix_ns,
                                                    zone,
                                                    &probe_civil,
                                                    &probe_offset);
            if (st != OBI_STATUS_OK) {
                continue;
            }

            if (_obi_time_icu_civil_equal(civil, &probe_civil)) {
                chosen_unix_ns = candidate_unix_ns;
                chosen_offset = probe_offset;
            }
        }

        *out_unix_ns = chosen_unix_ns;
        *out_offset_minutes = chosen_offset;
    }

    return OBI_STATUS_OK;
}
