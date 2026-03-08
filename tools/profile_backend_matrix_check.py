#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0

"""Validate backend planning matrix and emit shared conformance commands."""

import argparse
import csv
import sys
from collections import defaultdict


REQUIRED_COLUMNS = (
    "profile_id",
    "provider_id",
    "status",
    "backend_libraries",
    "matrix_lane",
    "provider_disposition",
)
ALLOWED_STATUS = {"implemented", "planned"}
ALLOWED_LANES = {"roadmap", "bootstrap"}
ALLOWED_DISPOSITIONS = {"final", "temporary_in_house", "remove"}
FAMILY_HINTS_BY_SUFFIX = {
    "cgltf_fastobj": ("cgltf", "fastobj"),
    "cmark": ("cmark",),
    "gdkpixbuf": ("gdkpixbuf",),
    "gio": ("gio",),
    "glib": ("glib",),
    "icu": ("icu",),
    "libxml2": ("libxml2",),
    "magic": ("magic",),
    "lmdb": ("lmdb",),
    "postgres": ("postgres", "libpq"),
    "raylib": ("raylib",),
    "sdl3": ("sdl3",),
    "sqlite": ("sqlite",),
    "sokol": ("sokol",),
    "ufbx": ("ufbx",),
}
NATIVE_STYLE_PROVIDER_IDS = {
    "obi.provider:core.cancel.atomic",
}
NATIVE_FINAL_ALLOWED_PROFILES = {
    "obi.profile:core.cancel-0",
    "obi.profile:net.socket-0",
    "obi.profile:os.env-0",
    "obi.profile:os.fs-0",
    "obi.profile:os.process-0",
    "obi.profile:os.dylib-0",
    "obi.profile:math.scientific_ops-0",
    "obi.profile:hw.gpio-0",
    "obi.profile:gfx.gpu_device-0",
}
NATIVE_FINAL_REQUIRED_PROFILES = {
    "obi.profile:core.cancel-0",
    "obi.profile:net.socket-0",
    "obi.profile:os.env-0",
    "obi.profile:os.fs-0",
    "obi.profile:os.process-0",
    "obi.profile:os.dylib-0",
    "obi.profile:gfx.gpu_device-0",
}
NATIVE_FINAL_REMOVED_PROFILES = {
    "obi.profile:time.datetime-0",
    "obi.profile:asset.mesh_io-0",
    "obi.profile:asset.scene_io-0",
    "obi.profile:crypto.hash-0",
    "obi.profile:crypto.aead-0",
    "obi.profile:crypto.kdf-0",
    "obi.profile:crypto.sign-0",
    "obi.profile:data.archive-0",
    "obi.profile:data.compression-0",
    "obi.profile:data.serde_emit-0",
    "obi.profile:data.serde_events-0",
    "obi.profile:data.uri-0",
    "obi.profile:db.kv-0",
    "obi.profile:db.sql-0",
    "obi.profile:doc.markdown_commonmark-0",
    "obi.profile:doc.markdown_events-0",
    "obi.profile:doc.markup_events-0",
    "obi.profile:doc.paged_document-0",
    "obi.profile:doc.text_decode-0",
    "obi.profile:ipc.bus-0",
    "obi.profile:math.bigfloat-0",
    "obi.profile:math.bigint-0",
    "obi.profile:math.blas-0",
    "obi.profile:math.decimal-0",
    "obi.profile:media.audio_device-0",
    "obi.profile:media.audio_mix-0",
    "obi.profile:media.audio_resample-0",
    "obi.profile:media.demux-0",
    "obi.profile:media.mux-0",
    "obi.profile:media.av_decode-0",
    "obi.profile:media.av_encode-0",
    "obi.profile:media.video_scale_convert-0",
    "obi.profile:net.dns-0",
    "obi.profile:net.tls-0",
    "obi.profile:net.http_client-0",
    "obi.profile:net.websocket-0",
    "obi.profile:os.fs_watch-0",
    "obi.profile:phys.world2d-0",
    "obi.profile:phys.world3d-0",
    "obi.profile:text.layout-0",
    "obi.profile:text.ime-0",
    "obi.profile:text.spellcheck-0",
    "obi.profile:text.regex-0",
}
MIX_HARD_GATE_PROFILES = {
    "obi.profile:core.cancel-0",
    "obi.profile:core.pump-0",
    "obi.profile:core.waitset-0",
    "obi.profile:os.fs_watch-0",
    "obi.profile:ipc.bus-0",
    "obi.profile:net.socket-0",
    "obi.profile:net.http_client-0",
    "obi.profile:time.datetime-0",
    "obi.profile:text.regex-0",
    "obi.profile:data.serde_events-0",
    "obi.profile:data.serde_emit-0",
}


def _provider_token(provider_id: str) -> str:
    if not provider_id.startswith("obi.provider:"):
        return ""
    token = provider_id[len("obi.provider:") :]
    token = token.replace(":", "_").replace(".", "_").replace("-", "_")
    return token


def _provider_suffix(provider_id: str) -> str:
    if not provider_id.startswith("obi.provider:"):
        return ""
    token = provider_id[len("obi.provider:") :]
    if "." not in token:
        return ""
    return token.rsplit(".", 1)[1]


def _normalize(value: str) -> str:
    return "".join(ch for ch in value.lower() if ch.isalnum())


def _is_native_style_provider(provider_id: str) -> bool:
    if provider_id in NATIVE_STYLE_PROVIDER_IDS:
        return True
    return _provider_suffix(provider_id) == "native"


def _parse_rows(csv_path: str):
    with open(csv_path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise ValueError("matrix csv has no header")

        missing = [c for c in REQUIRED_COLUMNS if c not in reader.fieldnames]
        if missing:
            raise ValueError(f"matrix csv missing columns: {', '.join(missing)}")

        rows = []
        for idx, raw in enumerate(reader, start=2):
            row = {k: (raw.get(k) or "").strip() for k in REQUIRED_COLUMNS}
            row["_line"] = idx
            rows.append(row)
        return rows


def _validate(rows):
    errors = []
    warnings = []

    if not rows:
        errors.append("matrix has no rows")
        return errors, warnings

    seen_pairs = set()
    by_profile = defaultdict(list)
    implemented_counts = defaultdict(int)
    roadmap_counts = defaultdict(int)
    roadmap_implemented_counts = defaultdict(int)
    roadmap_native_final_counts = defaultdict(int)

    for row in rows:
        line = row["_line"]
        profile = row["profile_id"]
        provider = row["provider_id"]
        status = row["status"]
        libs = row["backend_libraries"]
        lane = row["matrix_lane"]
        disposition = row["provider_disposition"]
        provider_suffix = _provider_suffix(provider)
        is_native_style_provider = _is_native_style_provider(provider)

        if not profile:
            errors.append(f"line {line}: empty profile_id")
        if not provider:
            errors.append(f"line {line}: empty provider_id")
        if not libs:
            errors.append(f"line {line}: empty backend_libraries")
        if provider and not provider.startswith("obi.provider:"):
            errors.append(
                f"line {line}: provider_id must start with 'obi.provider:', got '{provider}'"
            )

        pair = (profile, provider)
        if pair in seen_pairs:
            errors.append(f"line {line}: duplicate profile/provider pair {profile} + {provider}")
        seen_pairs.add(pair)

        if status not in ALLOWED_STATUS:
            errors.append(
                f"line {line}: invalid status '{status}' (allowed: {', '.join(sorted(ALLOWED_STATUS))})"
            )
        if lane not in ALLOWED_LANES:
            errors.append(
                f"line {line}: invalid matrix_lane '{lane}' (allowed: {', '.join(sorted(ALLOWED_LANES))})"
            )
        if disposition not in ALLOWED_DISPOSITIONS:
            errors.append(
                "line "
                f"{line}: invalid provider_disposition '{disposition}' "
                f"(allowed: {', '.join(sorted(ALLOWED_DISPOSITIONS))})"
            )
        if lane == "roadmap" and disposition != "final":
            errors.append(
                f"line {line}: roadmap row must use provider_disposition 'final', got '{disposition}'"
            )
        if lane == "bootstrap" and disposition == "final":
            errors.append(
                f"line {line}: bootstrap row cannot use provider_disposition 'final'"
            )
        if (
            lane == "bootstrap"
            and disposition == "temporary_in_house"
            and provider_suffix not in {"inhouse", "native"}
        ):
            errors.append(
                "line "
                f"{line}: temporary_in_house bootstrap provider id must end with "
                f"'.inhouse' or '.native', got '{provider}'"
            )
        if lane == "bootstrap" and disposition == "remove" and provider_suffix != "bootstrap":
            errors.append(
                f"line {line}: remove bootstrap provider id must end with '.bootstrap', got '{provider}'"
            )
        if lane == "roadmap" and provider_suffix in {"inhouse", "bootstrap"}:
            errors.append(
                f"line {line}: roadmap provider id cannot use inhouse/bootstrap suffix, got '{provider}'"
            )
        if lane == "roadmap" and disposition == "final" and is_native_style_provider:
            roadmap_native_final_counts[profile] += 1
            if profile not in NATIVE_FINAL_ALLOWED_PROFILES:
                errors.append(
                    "line "
                    f"{line}: native-style roadmap final provider '{provider}' is not allowed for "
                    f"{profile} by the native-retention policy"
                )
        if provider_suffix in FAMILY_HINTS_BY_SUFFIX and provider_suffix not in {"inhouse", "bootstrap"}:
            libs_norm = _normalize(libs)
            expected_hints = FAMILY_HINTS_BY_SUFFIX[provider_suffix]
            if not any(hint in libs_norm for hint in expected_hints):
                errors.append(
                    "line "
                    f"{line}: provider id '{provider}' implies backend family "
                    f"{expected_hints}, but backend_libraries='{libs}'"
                )

        by_profile[profile].append(row)
        if status == "implemented":
            implemented_counts[profile] += 1
            if lane == "roadmap":
                roadmap_implemented_counts[profile] += 1
        if lane == "roadmap":
            roadmap_counts[profile] += 1

    for profile, profile_rows in sorted(by_profile.items()):
        if len(profile_rows) < 2:
            errors.append(f"profile {profile}: needs at least 2 backend rows, found {len(profile_rows)}")
        if implemented_counts[profile] == 0:
            errors.append(f"profile {profile}: needs at least 1 implemented backend row")
        if implemented_counts[profile] < 2:
            warnings.append(
                f"profile {profile}: only {implemented_counts[profile]} implemented backend(s); "
                "second backend still pending"
            )
        if roadmap_counts[profile] == 0:
            warnings.append(
                f"profile {profile}: no roadmap rows yet; current coverage is bootstrap-only"
            )
        elif roadmap_implemented_counts[profile] == 0:
            warnings.append(
                f"profile {profile}: roadmap rows exist but none are implemented in-tree yet"
            )

    for profile in sorted(NATIVE_FINAL_REQUIRED_PROFILES):
        if roadmap_native_final_counts.get(profile, 0) == 0:
            errors.append(
                f"profile {profile}: native final roadmap row required by native-retention policy, found none"
            )

    for profile in sorted(NATIVE_FINAL_REMOVED_PROFILES):
        if roadmap_native_final_counts.get(profile, 0) > 0:
            errors.append(
                f"profile {profile}: native final roadmap rows must be removed once two approved third-party backends are green"
            )

    for profile in sorted(MIX_HARD_GATE_PROFILES):
        roadmap_ready = roadmap_implemented_counts.get(profile, 0)
        if roadmap_ready < 2:
            errors.append(
                f"profile {profile}: mixed-runtime hard gate requires >=2 implemented roadmap backends, found {roadmap_ready}"
            )

    return errors, warnings


def _emit_tt_requests(rows, lib_ext, lane):
    implemented = [
        row
        for row in rows
        if row["status"] == "implemented"
        and row["profile_id"]
        and row["provider_id"]
        and (lane == "all" or row["matrix_lane"] == lane)
    ]
    implemented.sort(key=lambda r: (r["profile_id"], r["provider_id"]))
    for row in implemented:
        provider_token = _provider_token(row["provider_id"])
        if not provider_token:
            continue
        provider_path = (
            f"./build/providers/{provider_token}/libobi_provider_{provider_token}.{lib_ext}"
        )
        cmd = (
            f"./build/obi_provider_smoke --profile-provider "
            f"{row['profile_id']} {row['provider_id']} {provider_path}"
        )
        print(f"TT_TEST_REQUEST: {cmd}")


def _emit_procurement(rows):
    by_profile = defaultdict(list)
    for row in rows:
        by_profile[row["profile_id"]].append(row)

    for profile in sorted(by_profile):
        print(profile)
        items = sorted(by_profile[profile], key=lambda r: (r["status"], r["provider_id"]))
        for row in items:
            print(
                "  - "
                f"{row['provider_id']} [{row['status']}; {row['matrix_lane']}; {row['provider_disposition']}] "
                f":: {row['backend_libraries']}"
            )


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Validate docs/profile_backend_matrix.csv and optionally emit "
            "procurement lines / TT conformance test requests."
        )
    )
    parser.add_argument(
        "csv_path",
        nargs="?",
        default="docs/profile_backend_matrix.csv",
        help="Path to profile backend matrix CSV",
    )
    parser.add_argument(
        "--emit-tt-requests",
        action="store_true",
        help="Emit TT_TEST_REQUEST lines for implemented profile/provider pairs",
    )
    parser.add_argument(
        "--emit-procurement",
        action="store_true",
        help="Emit profile -> provider/library planning list",
    )
    parser.add_argument(
        "--lane",
        choices=("all", "roadmap", "bootstrap"),
        default="all",
        help="Filter emitted TT requests to a single matrix lane (default: all)",
    )
    parser.add_argument(
        "--lib-ext",
        default="so",
        help="Shared library extension for emitted provider paths (default: so)",
    )
    args = parser.parse_args()

    try:
        rows = _parse_rows(args.csv_path)
    except Exception as exc:  # pylint: disable=broad-except
        print(f"matrix error: {exc}", file=sys.stderr)
        return 2

    errors, warnings = _validate(rows)

    if warnings:
        for warning in warnings:
            print(f"warning: {warning}", file=sys.stderr)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    profiles = len({row["profile_id"] for row in rows})
    implemented = len([row for row in rows if row["status"] == "implemented"])
    roadmap = len([row for row in rows if row["matrix_lane"] == "roadmap"])
    roadmap_implemented = len(
        [row for row in rows if row["matrix_lane"] == "roadmap" and row["status"] == "implemented"]
    )
    bootstrap = len([row for row in rows if row["matrix_lane"] == "bootstrap"])
    print(
        "matrix_ok "
        f"profiles={profiles} rows={len(rows)} implemented_rows={implemented} "
        f"roadmap_rows={roadmap} roadmap_implemented_rows={roadmap_implemented} "
        f"bootstrap_rows={bootstrap}",
        file=sys.stderr,
    )

    if args.emit_procurement:
        _emit_procurement(rows)
    if args.emit_tt_requests:
        _emit_tt_requests(rows, args.lib_ext, args.lane)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
