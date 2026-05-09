#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Validate Ratworld paired-runtime harness fixture/report evidence.

This checker enforces the item-80 paired baseline-vs-PRAXIS contract:
- harness-format comparison artifact shape is preserved,
- auditable headless and GUI-capable evidence bundles carry explicit refs,
- checked-in behavioral outcome reporting stays explicit and bounded,
- missing baseline/praxis evidence refs are rejected.
"""

from __future__ import annotations

import argparse
import json
import sys
from copy import deepcopy
from pathlib import Path
from typing import Any, Dict, List, Sequence

LIBOBI_ROOT = Path(__file__).resolve().parents[1]
SELFTEST_REPO_ROOT = LIBOBI_ROOT / "tools/testdata/paired_runtime_repo"
SELFTEST_FIXTURE_PATH = (
    SELFTEST_REPO_ROOT
    / "fixtures/praxis/ratworld/paired_runtime"
    / "ratworld_tower_zone_cycle_paired_runtime_parity_fixture.v1.json"
)
SELFTEST_REPORT_PATH = (
    SELFTEST_REPO_ROOT
    / "reports/praxis/ratworld/paired_runtime"
    / "ratworld_tower_zone_cycle_paired_runtime_parity_report.v1.json"
)

PAIR_SCHEMA_ID = "ratworld.paired_runtime.comparison_record.v1"
PAIR_ARTIFACT_SCHEMA_ID = "ratworld.paired_runtime.comparison_artifact.v1"
PAIR_BUNDLE_SCHEMA_ID = "ratworld.paired_runtime.evidence_bundle.v1"
REPORT_SCHEMA_ID = "ratworld.paired_runtime.comparison_report.v1"
PAIR_CLASSIFICATIONS: Sequence[str] = (
    "infrastructure_failure",
    "parity_no_improvement",
    "improvement_below_threshold",
    "improvement_accepted",
    "regression_demotion",
)
REQUIRED_LANE_FIELDS: Sequence[str] = (
    "accepted_actions",
    "rejected_actions",
    "learning_delta",
    "survival_metric",
    "foraging_metric",
    "exit_metric",
    "replay_ref",
    "visible_bundle_ref",
    "lineage_refs",
)
REQUIRED_LANE_EVALUATOR_ROOT_FIELDS: Sequence[str] = (
    "with_scout",
    "without_scout",
    "ally_usefulness_delta",
    "coordination_metric",
    "coordination_without_omniscience",
)
REQUIRED_LANE_EVALUATOR_RATE_FIELDS: Sequence[str] = (
    "exit_rate",
    "death_rate",
    "learning_delta",
    "dead_turn_rate",
    "board_coord_turn_rate",
    "board_pressure_turn_rate",
    "envelope_turn_rate",
)
REQUIRED_BUNDLE_FIELDS: Sequence[str] = (
    "run_mode",
    "baseline_replay_ref",
    "baseline_evidence_ref",
    "praxis_replay_ref",
    "praxis_evidence_ref",
    "overlay_selection",
    "overlay_ref",
)
REQUIRED_DECISION_HEADER_FIELDS: Sequence[str] = (
    "run_mode",
    "seed_suite",
    "learner_ref",
    "teacher_ref",
    "culture_seed",
    "heuristic_or_optimization_teacher_source",
    "episode_budget",
    "promotion_threshold",
    "demotion_threshold",
    "evidence_capture_mode",
    "gui_overlay_selection",
)
REQUIRED_CRN_SUMMARY_FIELDS: Sequence[str] = (
    "crn_key",
    "pair_id",
    "pair_index",
    "seed_pair",
    "seed_suite_entry",
    "control_source",
    "accepted_action_deltas",
    "rejected_action_deltas",
    "learning_deltas",
    "survival_ticks",
    "forage_counts",
    "exit_outcomes",
    "evidence_refs",
)
REQUIRED_CRN_DELTA_FIELDS: Sequence[str] = ("baseline", "praxis", "delta")
NO_IMPROVEMENT_CLASSIFICATIONS = {
    "infrastructure_failure",
    "parity_no_improvement",
    "improvement_below_threshold",
}
REQUIRED_SEED_SWEEP_LANE_RATE_FIELDS: Sequence[str] = (
    "exit_rate",
    "death_rate",
    "dead_turn_rate",
    "ally_usefulness_delta",
)
REQUIRED_SEED_SWEEP_SUMMARY_FIELDS: Sequence[str] = (
    "coordination_metric",
    "coordination_without_omniscience",
    "omniscient_read_turns",
    "omniscience_guard_signal",
    "coordination_signal_source",
    "board_signal",
)
REQUIRED_SEED_SWEEP_REF_FIELDS: Sequence[str] = (
    "summary_ref",
    "detail_ref",
    "source_ref",
)
REQUIRED_SEED_SWEEP_AGGREGATE_RATE_FIELDS: Sequence[str] = (
    "seed_sweep_exit_rate",
    "seed_sweep_death_rate",
    "seed_sweep_dead_turn_rate",
    "seed_sweep_ally_usefulness_delta",
)
REQUIRED_SEED_SWEEP_AGGREGATE_SUMMARY_FIELDS: Sequence[str] = (
    "coordination_metric_avg",
    "coordination_without_omniscience_all",
    "coordination_without_omniscience_any",
    "omniscient_read_turns_max",
)
REQUIRED_SEED_SWEEP_AGGREGATE_TOKEN_LIST_FIELDS: Sequence[str] = (
    "omniscience_guard_signals",
    "coordination_signal_sources",
    "board_signals",
)
REQUIRED_SEED_SWEEP_AGGREGATE_REF_LIST_FIELDS: Sequence[str] = (
    "summary_refs",
    "detail_refs",
    "source_refs",
)
CONTROLLER_COLLAPSE_CASTE_FIELDS: Sequence[str] = (
    "pc_rat",
    "scout_ally",
    "sentry_foe",
    "hunter_foe",
)
CONTROLLER_COLLAPSE_METRIC_FIELDS: Sequence[str] = (
    "turns_observed",
    "present_turns",
    "max_repeated_action_streak",
    "max_same_target_streak",
    "dead_turn_total",
)
CONTROLLER_COLLAPSE_AGGREGATE_FIELDS: Sequence[str] = (
    "max_repeated_action_streak",
    "max_same_target_streak",
    "dead_turn_total",
)
CONTROLLER_DEAD_TURN_CAUSE_FIELDS: Sequence[str] = (
    "none",
    "controller_requested_wait",
    "budget_exhausted",
    "blocked_by_geometry",
    "stair_gate_closed",
    "controller_unavailable",
    "local_context_unavailable",
    "over_budget_request",
    "action_sanitized",
)
FORBIDDEN_OVERCLAIM_PHRASES: Sequence[str] = (
    "cep authority",
    "cep-authority",
    "cep-s authority",
    "cep_authority",
    "praxis proven",
    "praxis-proven",
    "praxis_proven",
    "release admission",
    "release-admission",
    "release_admission",
    "release admitted",
    "release-admitted",
    "release_admitted",
    "superiority",
    "superior to",
    "superior_to",
)



def _load_json(path: Path) -> Dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: expected top-level object")
    return payload


def _expect(condition: bool, message: str, errors: List[str]) -> None:
    if not condition:
        errors.append(message)


def _as_dict(value: Any) -> Dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _as_list(value: Any) -> List[Any]:
    return value if isinstance(value, list) else []


def _token_missing(value: Any) -> bool:
    token = str(value).strip().lower()
    return (not token) or token == "not_reported"


def _artifact_path(repo_root: Path, ref: str) -> Path:
    text = str(ref).split("#", 1)[0].strip()
    if not text:
        return Path("")
    path = Path(text)
    return path if path.is_absolute() else (repo_root / path)


def _expected_fixture_claim_status(classification: str) -> str:
    if classification == "infrastructure_failure":
        return "blocked_by_infrastructure"
    if classification == "improvement_accepted":
        return "accepted"
    if classification == "regression_demotion":
        return "regression_detected"
    return "not_claimed"


def _expected_report_improvement_claim(classification: str) -> str:
    if classification == "improvement_accepted":
        return "claimed"
    return "not_claimed"


def _expected_improvement_detected(classification: str) -> bool:
    return classification == "improvement_accepted"


def _parse_nonnegative_int(
    *,
    value: Any,
    context: str,
    errors: List[str],
) -> int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        errors.append(f"{context} must be numeric")
        return None
    normalized = int(value)
    if normalized < 0:
        errors.append(f"{context} must be non-negative")
        return None
    return normalized


def _validate_controller_output_collapse_by_caste(
    *,
    collapse_row: Any,
    context: str,
    errors: List[str],
) -> None:
    _expect(isinstance(collapse_row, dict), f"{context} must be an object", errors)
    if not isinstance(collapse_row, dict):
        return

    caste_dead_turn_totals: Dict[str, int] = {}
    for caste in CONTROLLER_COLLAPSE_CASTE_FIELDS:
        caste_row = _as_dict(collapse_row.get(caste))
        _expect(bool(caste_row), f"{context}.{caste} missing", errors)
        if not caste_row:
            continue

        metric_values: Dict[str, int] = {}
        metric_invalid = False
        for metric_field in CONTROLLER_COLLAPSE_METRIC_FIELDS:
            metric_value = _parse_nonnegative_int(
                value=caste_row.get(metric_field),
                context=f"{context}.{caste}.{metric_field}",
                errors=errors,
            )
            if metric_value is None:
                metric_invalid = True
            else:
                metric_values[metric_field] = metric_value
        if not metric_invalid:
            _expect(
                metric_values["present_turns"] <= metric_values["turns_observed"],
                f"{context}.{caste}.present_turns exceeds turns_observed",
                errors,
            )

        dead_turn_cause_totals = _as_dict(caste_row.get("dead_turn_cause_totals"))
        _expect(
            bool(dead_turn_cause_totals),
            f"{context}.{caste}.dead_turn_cause_totals must be an object",
            errors,
        )
        if not dead_turn_cause_totals:
            continue
        non_none_dead_turn_total = 0
        dead_turn_causes_valid = True
        for cause_field in CONTROLLER_DEAD_TURN_CAUSE_FIELDS:
            cause_value = _parse_nonnegative_int(
                value=dead_turn_cause_totals.get(cause_field),
                context=f"{context}.{caste}.dead_turn_cause_totals.{cause_field}",
                errors=errors,
            )
            if cause_value is None:
                dead_turn_causes_valid = False
            elif cause_field != "none":
                non_none_dead_turn_total += cause_value
        if (
            dead_turn_causes_valid
            and "dead_turn_total" in metric_values
            and non_none_dead_turn_total != metric_values["dead_turn_total"]
        ):
            errors.append(
                f"{context}.{caste}.dead_turn_total does not match dead_turn_cause_totals"
            )
        if "dead_turn_total" in metric_values:
            caste_dead_turn_totals[caste] = metric_values["dead_turn_total"]

    aggregate_row = _as_dict(collapse_row.get("aggregate"))
    _expect(bool(aggregate_row), f"{context}.aggregate must be an object", errors)
    if not aggregate_row:
        return
    aggregate_values: Dict[str, int] = {}
    aggregate_invalid = False
    for field in CONTROLLER_COLLAPSE_AGGREGATE_FIELDS:
        aggregate_field_value = _parse_nonnegative_int(
            value=aggregate_row.get(field),
            context=f"{context}.aggregate.{field}",
            errors=errors,
        )
        if aggregate_field_value is None:
            aggregate_invalid = True
        else:
            aggregate_values[field] = aggregate_field_value
    if not aggregate_invalid and len(caste_dead_turn_totals) == len(CONTROLLER_COLLAPSE_CASTE_FIELDS):
        expected_aggregate_dead_turn_total = sum(caste_dead_turn_totals.values())
        _expect(
            aggregate_values["dead_turn_total"] == expected_aggregate_dead_turn_total,
            f"{context}.aggregate.dead_turn_total does not match caste dead_turn_total sum",
            errors,
        )


def _validate_behavioral_statement(
    *,
    classification: str,
    statement: str,
    context: str,
    errors: List[str],
) -> None:
    lowered = statement.strip().lower()
    _expect(bool(lowered), f"{context} behavioral_outcome_statement missing", errors)
    _expect(
        not any(phrase in lowered for phrase in FORBIDDEN_OVERCLAIM_PHRASES),
        f"{context} behavioral_outcome_statement contains forbidden overclaim wording",
        errors,
    )
    if classification == "parity_no_improvement":
        _expect(
            "no behavioral improvement" in lowered,
            f"{context} parity statement must mention no behavioral improvement",
            errors,
        )
    elif classification == "improvement_accepted":
        _expect(
            "improvement" in lowered and "threshold" in lowered,
            f"{context} improvement statement should mention thresholded improvement",
            errors,
        )
    elif classification == "improvement_below_threshold":
        _expect(
            "threshold" in lowered,
            f"{context} sub-threshold statement should mention threshold status",
            errors,
        )
    elif classification == "regression_demotion":
        _expect(
            "regression" in lowered or "demotion" in lowered,
            f"{context} regression statement should mention regression or demotion",
            errors,
        )
    else:
        _expect(
            "no behavioral improvement claim is valid" in lowered
            or "infrastructure_failure" in lowered,
            f"{context} infrastructure statement must block improvement claims",
            errors,
        )


def _validate_crn_delta_block(
    *,
    block: Dict[str, Any],
    field_name: str,
    errors: List[str],
) -> None:
    for required in REQUIRED_CRN_DELTA_FIELDS:
        value = block.get(required)
        _expect(
            isinstance(value, (int, float)),
            f"crn_behavioral_summary.{field_name}.{required} must be numeric",
            errors,
            )


def _validate_seed_sweep_lane_payload(
    *,
    lane_name: str,
    lane_row: Dict[str, Any],
    context: str,
    errors: List[str],
) -> None:
    _expect(bool(lane_row), f"{context} missing lane payload for {lane_name}", errors)
    for field in REQUIRED_SEED_SWEEP_LANE_RATE_FIELDS:
        _expect(
            isinstance(lane_row.get(field), (int, float)),
            f"{context} {lane_name}.{field} must be numeric",
            errors,
        )

    summary = _as_dict(lane_row.get("bounded_coordination_evidence_summary"))
    _expect(
        bool(summary),
        f"{context} {lane_name}.bounded_coordination_evidence_summary missing",
        errors,
    )
    if summary:
        _expect(
            isinstance(summary.get("coordination_metric"), (int, float)),
            f"{context} {lane_name}.bounded_coordination_evidence_summary.coordination_metric must be numeric",
            errors,
        )
        _expect(
            isinstance(summary.get("coordination_without_omniscience"), bool),
            (
                f"{context} {lane_name}.bounded_coordination_evidence_summary."
                "coordination_without_omniscience must be bool"
            ),
            errors,
        )
        _expect(
            isinstance(summary.get("omniscient_read_turns"), (int, float)),
            (
                f"{context} {lane_name}.bounded_coordination_evidence_summary."
                "omniscient_read_turns must be numeric"
            ),
            errors,
        )
        for token_field in ("omniscience_guard_signal", "coordination_signal_source", "board_signal"):
            _expect(
                not _token_missing(summary.get(token_field)),
                (
                    f"{context} {lane_name}.bounded_coordination_evidence_summary."
                    f"{token_field} must be a non-empty token"
                ),
                errors,
            )

    refs = _as_dict(lane_row.get("bounded_coordination_evidence_refs"))
    _expect(
        bool(refs),
        f"{context} {lane_name}.bounded_coordination_evidence_refs missing",
        errors,
    )
    if refs:
        for ref_field in REQUIRED_SEED_SWEEP_REF_FIELDS:
            _expect(
                not _token_missing(refs.get(ref_field)),
                f"{context} {lane_name}.bounded_coordination_evidence_refs.{ref_field} missing",
                errors,
            )

    controller_output_collapse_status = str(
        lane_row.get("controller_output_collapse_status", "")
    ).strip()
    _expect(
        controller_output_collapse_status == "present",
        f"{context} {lane_name}.controller_output_collapse_status must be present",
        errors,
    )
    controller_output_collapse_error = str(
        lane_row.get("controller_output_collapse_error", "")
    ).strip()
    _expect(
        controller_output_collapse_error == "",
        f"{context} {lane_name}.controller_output_collapse_error must be empty",
        errors,
    )
    _validate_controller_output_collapse_by_caste(
        collapse_row=lane_row.get("controller_output_collapse_by_caste"),
        context=f"{context} {lane_name}.controller_output_collapse_by_caste",
        errors=errors,
    )

    _expect(
        str(lane_row.get("fairness_bundle_status", "")).strip() == "present",
        f"{context} {lane_name}.fairness_bundle_status must be present",
        errors,
    )
    for missing_key in (
        "fairness_bundle_missing_fields",
        "fairness_bundle_missing_summary_fields",
        "fairness_bundle_missing_ref_fields",
    ):
        missing_values = _as_list(lane_row.get(missing_key))
        _expect(
            not missing_values,
            f"{context} {lane_name}.{missing_key} must be empty",
            errors,
        )


def _validate_seed_sweep_arbiter_payload(
    *,
    payload: Dict[str, Any],
    context: str,
    errors: List[str],
) -> None:
    _expect(bool(payload), f"{context} payload missing", errors)
    if not payload:
        return

    baseline = _as_dict(payload.get("baseline"))
    praxis = _as_dict(payload.get("praxis"))
    _validate_seed_sweep_lane_payload(
        lane_name="baseline",
        lane_row=baseline,
        context=context,
        errors=errors,
    )
    _validate_seed_sweep_lane_payload(
        lane_name="praxis",
        lane_row=praxis,
        context=context,
        errors=errors,
    )

    aggregate_rates = _as_dict(payload.get("aggregate_rates"))
    _expect(bool(aggregate_rates), f"{context} aggregate_rates missing", errors)
    for field in REQUIRED_SEED_SWEEP_AGGREGATE_RATE_FIELDS:
        _expect(
            isinstance(aggregate_rates.get(field), (int, float)),
            f"{context} aggregate_rates.{field} must be numeric",
            errors,
        )

    aggregate_summary = _as_dict(payload.get("aggregate_summary"))
    _expect(bool(aggregate_summary), f"{context} aggregate_summary missing", errors)
    for field in REQUIRED_SEED_SWEEP_AGGREGATE_SUMMARY_FIELDS:
        expected_type = bool if "without_omniscience" in field else (int, float)
        _expect(
            isinstance(aggregate_summary.get(field), expected_type),
            f"{context} aggregate_summary.{field} type mismatch",
            errors,
        )
    for field in REQUIRED_SEED_SWEEP_AGGREGATE_TOKEN_LIST_FIELDS:
        values = _as_list(aggregate_summary.get(field))
        _expect(values, f"{context} aggregate_summary.{field} must be a non-empty list", errors)
        if values:
            _expect(
                all(not _token_missing(value) for value in values),
                f"{context} aggregate_summary.{field} contains empty/not_reported tokens",
                errors,
            )

    aggregate_refs = _as_dict(payload.get("aggregate_ref_tokens"))
    _expect(bool(aggregate_refs), f"{context} aggregate_ref_tokens missing", errors)
    for field in REQUIRED_SEED_SWEEP_AGGREGATE_REF_LIST_FIELDS:
        values = _as_list(aggregate_refs.get(field))
        _expect(values, f"{context} aggregate_ref_tokens.{field} must be a non-empty list", errors)
        if values:
            _expect(
                all(not _token_missing(value) for value in values),
                f"{context} aggregate_ref_tokens.{field} contains empty/not_reported tokens",
                errors,
            )

    missing_bundle_data = _as_list(payload.get("missing_bundle_data"))
    _expect(
        not missing_bundle_data,
        f"{context} missing_bundle_data must be empty",
        errors,
    )


def _validate_lane(
    *,
    repo_root: Path,
    lane_name: str,
    lane_row: Dict[str, Any],
    errors: List[str],
) -> None:
    lane_log_ref = str(lane_row.get("lane_log_ref", "")).strip()
    evidence_ref = str(lane_row.get("visible_bundle_ref", "")).strip()
    lane_context = (
        f"{lane_name} malformed evidence file {lane_log_ref or evidence_ref}"
        if (lane_log_ref or evidence_ref)
        else f"{lane_name} lane"
    )
    for field in REQUIRED_LANE_FIELDS:
        _expect(field in lane_row, f"{lane_context} missing {field}", errors)

    accepted = lane_row.get("accepted_actions")
    rejected = lane_row.get("rejected_actions")
    _expect(isinstance(accepted, list) and bool(accepted), f"{lane_context} accepted_actions missing", errors)
    _expect(isinstance(rejected, list), f"{lane_context} rejected_actions must be a list", errors)
    accepted_tokens = [str(action).strip() for action in accepted] if isinstance(accepted, list) else []
    _expect(
        all(bool(token) for token in accepted_tokens),
        f"{lane_context} accepted_actions contains empty token",
        errors,
    )
    accepted_unique_count = len({token for token in accepted_tokens if token})
    _expect(
        accepted_unique_count > 1,
        f"{lane_context} accepted_actions collapsed to single unique token",
        errors,
    )
    accepted_action_unique_count = _parse_nonnegative_int(
        value=lane_row.get("accepted_action_unique_count"),
        context=f"{lane_context} accepted_action_unique_count",
        errors=errors,
    )
    if accepted_action_unique_count is not None:
        _expect(
            accepted_action_unique_count == accepted_unique_count,
            f"{lane_context} accepted_action_unique_count does not match accepted_actions",
            errors,
        )
    accepted_action_collapse_detected = lane_row.get("accepted_action_collapse_detected")
    _expect(
        isinstance(accepted_action_collapse_detected, bool),
        f"{lane_context} accepted_action_collapse_detected must be bool",
        errors,
    )
    if isinstance(accepted_action_collapse_detected, bool):
        _expect(
            accepted_action_collapse_detected is False,
            f"{lane_context} accepted_action_collapse_detected must be false",
            errors,
        )
    _validate_controller_output_collapse_by_caste(
        collapse_row=lane_row.get("controller_output_collapse_by_caste"),
        context=f"{lane_context}.controller_output_collapse_by_caste",
        errors=errors,
    )

    for metric_name in ("learning_delta", "survival_metric", "foraging_metric", "exit_metric"):
        _expect(
            isinstance(lane_row.get(metric_name), (int, float)),
            f"{lane_context} metric {metric_name} must be numeric",
            errors,
        )

    evaluator = _as_dict(lane_row.get("evaluator_metrics"))
    _expect(bool(evaluator), f"{lane_context} evaluator_metrics missing", errors)
    for field in REQUIRED_LANE_EVALUATOR_ROOT_FIELDS:
        _expect(field in evaluator, f"{lane_context} evaluator_metrics missing {field}", errors)
    for evaluator_lane in ("with_scout", "without_scout"):
        evaluator_row = _as_dict(evaluator.get(evaluator_lane))
        _expect(
            bool(evaluator_row),
            f"{lane_context} evaluator_metrics.{evaluator_lane} missing",
            errors,
        )
        for rate_field in REQUIRED_LANE_EVALUATOR_RATE_FIELDS:
            _expect(
                isinstance(evaluator_row.get(rate_field), (int, float)),
                f"{lane_context} evaluator_metrics.{evaluator_lane}.{rate_field} must be numeric",
                errors,
            )
    _expect(
        isinstance(evaluator.get("ally_usefulness_delta"), (int, float)),
        f"{lane_context} evaluator_metrics.ally_usefulness_delta must be numeric",
        errors,
    )
    _expect(
        isinstance(evaluator.get("coordination_metric"), (int, float)),
        f"{lane_context} evaluator_metrics.coordination_metric must be numeric",
        errors,
    )
    _expect(
        isinstance(evaluator.get("coordination_without_omniscience"), bool),
        f"{lane_context} evaluator_metrics.coordination_without_omniscience must be bool",
        errors,
    )

    replay_ref = str(lane_row.get("replay_ref", "")).strip()
    _expect(bool(replay_ref), f"{lane_context} replay_ref missing", errors)
    _expect(bool(evidence_ref), f"{lane_context} visible_bundle_ref missing", errors)
    replay_path = _artifact_path(repo_root, replay_ref)
    evidence_path = _artifact_path(repo_root, evidence_ref)
    _expect(replay_path.exists(), f"{lane_context} replay_ref does not exist: {replay_path}", errors)
    _expect(
        evidence_path.exists(),
        f"{lane_context} visible_bundle_ref does not exist: {evidence_path}",
        errors,
    )

    lineage = _as_dict(lane_row.get("lineage_refs"))
    learner_refs = lineage.get("learner_refs")
    _expect(isinstance(learner_refs, dict) and bool(learner_refs), f"{lane_context} learner_refs missing", errors)
    _expect(bool(str(lineage.get("teacher_ref", "")).strip()), f"{lane_context} teacher_ref missing", errors)
    _expect(bool(str(lineage.get("culture_ref", "")).strip()), f"{lane_context} culture_ref missing", errors)


def _validate_fixture_contract(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []

    fixture_schema = str(fixture.get("schema_id", "")).strip()
    _expect(
        fixture_schema in {PAIR_SCHEMA_ID, PAIR_ARTIFACT_SCHEMA_ID},
        "fixture schema_id mismatch",
        errors,
    )
    _expect(str(fixture.get("phase", "")).strip() == "paired_runtime_comparison", "fixture phase mismatch", errors)
    pair_id = str(fixture.get("pair_id", "")).strip()
    classification = str(fixture.get("classification", "")).strip()
    pair_index = int(fixture.get("pair_index", 0) or 0)
    layout_seed = int(fixture.get("layout_seed", 0) or 0)
    run_seed = int(fixture.get("run_seed", 0) or 0)
    suite_line = str(fixture.get("suite_line", "")).strip()
    seed_suite = str(fixture.get("seed_suite", "")).strip()
    _expect(bool(pair_id), "fixture pair_id missing", errors)
    _expect(pair_index > 0, "fixture pair_index must be positive", errors)
    _expect(layout_seed > 0 and run_seed > 0, "fixture layout_seed/run_seed must be positive", errors)
    _expect(bool(suite_line), "fixture suite_line missing", errors)
    _expect(bool(seed_suite), "fixture seed_suite missing", errors)
    expected_suite_line = f"{layout_seed} {run_seed}" if layout_seed > 0 and run_seed > 0 else ""
    if expected_suite_line:
        _expect(
            suite_line == expected_suite_line,
            "fixture suite_line must align with layout_seed/run_seed pair",
            errors,
        )
    expected_pair_id = (
        f"ratworld.seed_pair.ls{layout_seed}.rs{run_seed}" if layout_seed > 0 and run_seed > 0 else ""
    )
    if expected_pair_id:
        _expect(pair_id == expected_pair_id, "fixture pair_id must align with layout_seed/run_seed", errors)
    _expect(classification in PAIR_CLASSIFICATIONS, "fixture classification invalid", errors)
    _expect(
        fixture.get("classification_allowlist") == list(PAIR_CLASSIFICATIONS),
        "fixture classification_allowlist mismatch",
        errors,
    )
    reducer = _as_dict(fixture.get("comparison_reducer"))
    _expect(
        str(reducer.get("classification", "")).strip() == classification,
        "fixture comparison_reducer classification mismatch",
        errors,
    )
    _expect(
        str(reducer.get("pair_id", "")).strip() == pair_id,
        "fixture comparison_reducer pair_id mismatch",
        errors,
    )
    reducer_seed_sweep = _as_dict(reducer.get("seed_sweep_arbiter"))
    _validate_seed_sweep_arbiter_payload(
        payload=reducer_seed_sweep,
        context="fixture comparison_reducer.seed_sweep_arbiter",
        errors=errors,
    )
    improvement_detected = fixture.get("behavioral_improvement_detected")
    _expect(isinstance(improvement_detected, bool), "fixture missing improvement bool", errors)
    _expect(
        improvement_detected is _expected_improvement_detected(classification),
        "fixture behavioral_improvement_detected does not match classification",
        errors,
    )
    claim_status = str(fixture.get("behavioral_claim_status", "")).strip()
    _expect(bool(claim_status), "fixture behavioral_claim_status missing", errors)
    _expect(
        claim_status == _expected_fixture_claim_status(classification),
        "fixture behavioral_claim_status does not match classification",
        errors,
    )
    outcome_statement = str(fixture.get("behavioral_outcome_statement", "")).strip()
    _validate_behavioral_statement(
        classification=classification,
        statement=outcome_statement,
        context="fixture",
        errors=errors,
    )
    if classification in NO_IMPROVEMENT_CLASSIFICATIONS:
        no_improvement = _as_dict(fixture.get("no_improvement_result"))
        _expect(bool(no_improvement), "fixture no_improvement_result missing", errors)
        if no_improvement:
            _expect(
                str(no_improvement.get("classification", "")).strip() == classification,
                "fixture no_improvement_result classification mismatch",
                errors,
            )
            _expect(
                str(no_improvement.get("status", "")).strip() == "explicit_no_improvement",
                "fixture no_improvement_result status mismatch",
                errors,
            )

    common_random_number_control = _as_dict(fixture.get("common_random_number_control"))
    _expect(bool(common_random_number_control), "fixture common_random_number_control missing", errors)
    if common_random_number_control:
        _expect(
            str(common_random_number_control.get("seed_pair", "")).strip() == suite_line,
            "fixture common_random_number_control.seed_pair must match suite_line",
            errors,
        )
        _expect(
            int(common_random_number_control.get("seed_suite_entry", 0) or 0) == pair_index,
            "fixture common_random_number_control.seed_suite_entry must match pair_index",
            errors,
        )
        _expect(
            str(common_random_number_control.get("control_source", "")).strip() == seed_suite,
            "fixture common_random_number_control.control_source must match seed_suite",
            errors,
        )

    canonical = _as_dict(fixture.get("canonical_pair_record"))
    _expect(bool(canonical), "fixture canonical_pair_record missing", errors)
    _expect(str(canonical.get("pair_id", "")).strip() == pair_id, "canonical pair_id mismatch", errors)
    _expect(int(canonical.get("pair_index", 0) or 0) == pair_index, "canonical pair_index mismatch", errors)
    _expect(int(canonical.get("layout_seed", 0) or 0) == layout_seed, "canonical layout_seed mismatch", errors)
    _expect(int(canonical.get("run_seed", 0) or 0) == run_seed, "canonical run_seed mismatch", errors)
    _expect(str(canonical.get("suite_line", "")).strip() == suite_line, "canonical suite_line mismatch", errors)
    _expect(
        _as_dict(canonical.get("common_random_number_control")) == common_random_number_control,
        "canonical common_random_number_control mismatch",
        errors,
    )
    lanes = _as_dict(canonical.get("lanes"))
    baseline_lane = _as_dict(lanes.get("baseline"))
    praxis_lane = _as_dict(lanes.get("praxis"))
    _expect(bool(baseline_lane), "fixture baseline lane missing", errors)
    _expect(bool(praxis_lane), "fixture praxis lane missing", errors)
    if baseline_lane:
        _validate_lane(repo_root=repo_root, lane_name="baseline", lane_row=baseline_lane, errors=errors)
    if praxis_lane:
        _validate_lane(repo_root=repo_root, lane_name="praxis", lane_row=praxis_lane, errors=errors)

    decision_header = _as_dict(canonical.get("decision_variable_header"))
    _expect(bool(decision_header), "fixture decision_variable_header missing", errors)
    for required in REQUIRED_DECISION_HEADER_FIELDS:
        _expect(required in decision_header, f"decision_variable_header missing {required}", errors)
        if required in fixture:
            _expect(
                decision_header.get(required) == fixture.get(required),
                f"decision_variable_header.{required} mismatch with fixture root",
                errors,
            )

    crn_summary = _as_dict(canonical.get("crn_behavioral_summary"))
    _expect(bool(crn_summary), "fixture crn_behavioral_summary missing", errors)
    for required in REQUIRED_CRN_SUMMARY_FIELDS:
        _expect(required in crn_summary, f"crn_behavioral_summary missing {required}", errors)
    if crn_summary:
        _expect(
            str(crn_summary.get("pair_id", "")).strip() == pair_id,
            "crn_behavioral_summary pair_id mismatch",
            errors,
        )
        _expect(
            int(crn_summary.get("pair_index", 0) or 0) == pair_index,
            "crn_behavioral_summary pair_index mismatch",
            errors,
        )
        _expect(
            str(crn_summary.get("seed_pair", "")).strip() == suite_line,
            "crn_behavioral_summary seed_pair mismatch",
            errors,
        )
        _expect(
            int(crn_summary.get("seed_suite_entry", 0) or 0) == pair_index,
            "crn_behavioral_summary seed_suite_entry mismatch",
            errors,
        )
        _expect(
            str(crn_summary.get("control_source", "")).strip() == seed_suite,
            "crn_behavioral_summary control_source mismatch",
            errors,
        )

        accepted_deltas = _as_dict(crn_summary.get("accepted_action_deltas"))
        rejected_deltas = _as_dict(crn_summary.get("rejected_action_deltas"))
        for label, block in (("accepted_action_deltas", accepted_deltas), ("rejected_action_deltas", rejected_deltas)):
            for key in ("baseline_count", "praxis_count", "delta_count"):
                _expect(
                    isinstance(block.get(key), (int, float)),
                    f"crn_behavioral_summary.{label}.{key} must be numeric",
                    errors,
                )
            for key in ("actions_only_in_baseline", "actions_only_in_praxis"):
                _expect(
                    isinstance(block.get(key), list),
                    f"crn_behavioral_summary.{label}.{key} must be a list",
                    errors,
                )

        for label in ("learning_deltas", "survival_ticks", "forage_counts", "exit_outcomes"):
            _validate_crn_delta_block(
                block=_as_dict(crn_summary.get(label)),
                field_name=label,
                errors=errors,
            )

        evidence_refs = _as_dict(crn_summary.get("evidence_refs"))
        _expect(bool(evidence_refs), "crn_behavioral_summary evidence_refs missing", errors)
        if evidence_refs:
            _expect(
                str(evidence_refs.get("baseline_replay_ref", "")).strip()
                == str(baseline_lane.get("replay_ref", "")).strip(),
                "crn_behavioral_summary baseline_replay_ref mismatch",
                errors,
            )
            _expect(
                str(evidence_refs.get("baseline_visible_bundle_ref", "")).strip()
                == str(baseline_lane.get("visible_bundle_ref", "")).strip(),
                "crn_behavioral_summary baseline_visible_bundle_ref mismatch",
                errors,
            )
            _expect(
                str(evidence_refs.get("praxis_replay_ref", "")).strip()
                == str(praxis_lane.get("replay_ref", "")).strip(),
                "crn_behavioral_summary praxis_replay_ref mismatch",
                errors,
            )
            _expect(
                str(evidence_refs.get("praxis_visible_bundle_ref", "")).strip()
                == str(praxis_lane.get("visible_bundle_ref", "")).strip(),
                "crn_behavioral_summary praxis_visible_bundle_ref mismatch",
                errors,
            )

    bundle_ref = str(canonical.get("auditable_evidence_bundle_ref", "")).strip()
    _expect(bool(bundle_ref), "fixture auditable_evidence_bundle_ref missing", errors)
    bundle_manifest_path = _artifact_path(repo_root, bundle_ref)
    _expect(bundle_manifest_path.exists(), "bundle manifest ref path missing", errors)
    bundle_manifest = {}
    if bundle_manifest_path.exists():
        bundle_manifest = _load_json(bundle_manifest_path)
        _expect(
            bundle_manifest.get("schema_id") == PAIR_BUNDLE_SCHEMA_ID,
            "bundle manifest schema_id mismatch",
            errors,
        )
        _expect(
            str(bundle_manifest.get("pair_id", "")).strip() == pair_id,
            "bundle manifest pair_id mismatch",
            errors,
        )

    bundles = _as_dict(canonical.get("auditable_evidence_bundles"))
    _expect(bool(bundles), "fixture auditable_evidence_bundles missing", errors)
    if bundle_manifest:
        _expect(
            bundle_manifest.get("auditable_evidence_bundles") == bundles,
            "bundle manifest payload mismatch",
            errors,
        )
    auditable_capture = _as_dict(canonical.get("auditable_evidence_capture"))
    _expect(bool(auditable_capture), "fixture auditable_evidence_capture missing", errors)
    selected_modes = _as_list(auditable_capture.get("selected_modes"))
    _expect(bool(selected_modes), "fixture auditable_evidence_capture.selected_modes missing", errors)
    normalized_selected_modes = [str(mode).strip() for mode in selected_modes if str(mode).strip()]
    for mode in normalized_selected_modes:
        _expect(
            str(mode) in {"headless", "gui_capable"},
            "fixture auditable_evidence_capture.selected_modes includes unsupported mode",
            errors,
        )
    _expect(
        bool(str(auditable_capture.get("evidence_capture_mode", "")).strip()),
        "fixture auditable_evidence_capture.evidence_capture_mode missing",
        errors,
    )
    overlay_refs = _as_dict(canonical.get("auditable_evidence_overlay_refs"))
    _expect(bool(overlay_refs), "fixture auditable_evidence_overlay_refs missing", errors)
    _expect(
        set(bundles.keys()) == set(normalized_selected_modes),
        "fixture auditable_evidence_bundles mode keys must match selected_modes",
        errors,
    )
    for mode in normalized_selected_modes:
        mode_bundle = _as_dict(bundles.get(mode))
        _expect(bool(mode_bundle), f"{mode} auditable evidence bundle missing", errors)
        if not mode_bundle:
            continue
        for field in REQUIRED_BUNDLE_FIELDS:
            value = str(mode_bundle.get(field, "")).strip()
            _expect(bool(value), f"{mode} bundle missing {field}", errors)

        _expect(
            str(mode_bundle.get("baseline_replay_ref", "")).strip()
            == str(baseline_lane.get("replay_ref", "")).strip(),
            f"{mode} baseline_replay_ref must match baseline lane replay_ref",
            errors,
        )
        _expect(
            str(mode_bundle.get("baseline_evidence_ref", "")).strip()
            == str(baseline_lane.get("visible_bundle_ref", "")).strip(),
            f"{mode} baseline_evidence_ref must match baseline lane visible_bundle_ref",
            errors,
        )
        _expect(
            str(mode_bundle.get("praxis_replay_ref", "")).strip()
            == str(praxis_lane.get("replay_ref", "")).strip(),
            f"{mode} praxis_replay_ref must match praxis lane replay_ref",
            errors,
        )
        _expect(
            str(mode_bundle.get("praxis_evidence_ref", "")).strip()
            == str(praxis_lane.get("visible_bundle_ref", "")).strip(),
            f"{mode} praxis_evidence_ref must match praxis lane visible_bundle_ref",
            errors,
        )
        overlay_ref = str(mode_bundle.get("overlay_ref", "")).strip()
        overlay_path = _artifact_path(repo_root, overlay_ref)
        _expect(overlay_path.exists(), f"{mode} overlay_ref does not exist: {overlay_path}", errors)
        if overlay_path.exists():
            overlay = _load_json(overlay_path)
            _expect(
                overlay.get("schema_id") == PAIR_BUNDLE_SCHEMA_ID,
                f"{mode} overlay schema_id mismatch",
                errors,
            )
            _expect(str(overlay.get("pair_id", "")).strip() == pair_id, f"{mode} overlay pair_id mismatch", errors)
            _expect(
                str(overlay.get("run_mode", "")).strip() == str(mode_bundle.get("run_mode", "")).strip(),
                f"{mode} overlay run_mode mismatch",
                errors,
            )
            _expect(
                str(overlay.get("overlay_selection", "")).strip()
                == str(mode_bundle.get("overlay_selection", "")).strip(),
                f"{mode} overlay selection mismatch",
                errors,
            )
        overlay_ref_key = f"{mode}_overlay_ref"
        _expect(
            str(overlay_refs.get(overlay_ref_key, "")).strip() == overlay_ref,
            f"auditable_evidence_overlay_refs.{overlay_ref_key} mismatch",
            errors,
        )

    artifact_ref = str(fixture.get("comparison_artifact_ref", "")).strip()
    if artifact_ref:
        artifact_path = _artifact_path(repo_root, artifact_ref)
        _expect(artifact_path.exists(), f"fixture comparison_artifact_ref path missing: {artifact_path}", errors)
        _expect(
            fixture_path == artifact_path,
            "fixture comparison_artifact_ref must point at checked-in artifact",
            errors,
        )
        if fixture_path == artifact_path:
            _expect(
                fixture_schema == PAIR_ARTIFACT_SCHEMA_ID,
                "fixture schema_id must be comparison artifact when self-referenced",
                errors,
            )
        elif artifact_path.exists():
            artifact = _load_json(artifact_path)
            _expect(
                artifact.get("schema_id") == PAIR_ARTIFACT_SCHEMA_ID,
                "comparison artifact schema_id mismatch",
                errors,
            )
            _expect(
                str(artifact.get("classification", "")).strip() == classification,
                "comparison artifact classification mismatch",
                errors,
            )

    return errors


def _validate_report_contract(
    report: Dict[str, Any],
    *,
    repo_root: Path,
    report_path: Path,
    fixture: Dict[str, Any],
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    _expect(report.get("schema_id") == REPORT_SCHEMA_ID, "report schema_id mismatch", errors)
    _expect(bool(str(report.get("report_id", "")).strip()), "report_id missing", errors)

    source_fixture = str(report.get("source_fixture", "")).strip()
    expected_rel_fixture = str(fixture_path.relative_to(repo_root))
    _expect(source_fixture == expected_rel_fixture, "report source_fixture mismatch", errors)

    suite_reducer = _as_dict(report.get("suite_reducer"))
    fixture_classification = str(fixture.get("classification", "")).strip()
    reducer_classification = str(suite_reducer.get("classification", "")).strip()
    _expect(
        reducer_classification in PAIR_CLASSIFICATIONS,
        "suite_reducer classification invalid",
        errors,
    )
    _expect(
        reducer_classification == fixture_classification,
        "suite_reducer classification mismatch",
        errors,
    )
    classification_counts = _as_dict(suite_reducer.get("classification_counts"))
    _expect(bool(classification_counts), "suite_reducer classification_counts missing", errors)
    count_total = 0
    for classification in PAIR_CLASSIFICATIONS:
        count_value = classification_counts.get(classification)
        _expect(
            isinstance(count_value, (int, float)),
            f"suite_reducer classification_counts missing numeric value for {classification}",
            errors,
        )
        if isinstance(count_value, (int, float)):
            count_total += int(count_value)
    pair_count = int(suite_reducer.get("pair_count", 0) or 0)
    _expect(pair_count > 0, "suite_reducer pair_count must be positive", errors)
    _expect(
        count_total == pair_count,
        "suite_reducer classification_counts total must equal pair_count",
        errors,
    )
    _expect(
        int(classification_counts.get(reducer_classification, 0) or 0) > 0,
        "suite_reducer classification_counts must include the winning classification",
        errors,
    )
    suite_seed_sweep = _as_dict(suite_reducer.get("seed_sweep_arbiter"))
    _validate_seed_sweep_arbiter_payload(
        payload=suite_seed_sweep,
        context="report suite_reducer.seed_sweep_arbiter",
        errors=errors,
    )
    cleanliness = _as_dict(suite_reducer.get("paired_runtime_comparison_cleanliness"))
    _expect(
        bool(cleanliness),
        "suite_reducer paired_runtime_comparison_cleanliness missing",
        errors,
    )
    if cleanliness:
        _expect(
            isinstance(cleanliness.get("comparison_clean"), bool),
            "suite_reducer paired_runtime_comparison_cleanliness.comparison_clean must be bool",
            errors,
        )
        _expect(
            bool(str(cleanliness.get("status_reason", "")).strip()),
            "suite_reducer paired_runtime_comparison_cleanliness.status_reason missing",
            errors,
        )
        _expect(
            bool(str(cleanliness.get("evidence_ref", "")).strip()),
            "suite_reducer paired_runtime_comparison_cleanliness.evidence_ref missing",
            errors,
        )

    outcome = _as_dict(report.get("behavioral_outcome"))
    outcome_classification = str(outcome.get("classification", "")).strip()
    _expect(
        outcome_classification == reducer_classification,
        "behavioral_outcome classification mismatch",
        errors,
    )
    outcome_detected = outcome.get("behavioral_improvement_detected")
    _expect(
        isinstance(outcome_detected, bool),
        "behavioral_outcome missing behavioral_improvement_detected bool",
        errors,
    )
    _expect(
        outcome_detected is _expected_improvement_detected(outcome_classification),
        "behavioral_outcome behavioral_improvement_detected mismatch",
        errors,
    )
    outcome_statement = str(outcome.get("behavioral_outcome_statement", "")).strip()
    _validate_behavioral_statement(
        classification=outcome_classification,
        statement=outcome_statement,
        context="report",
        errors=errors,
    )
    improvement_claim = str(outcome.get("improvement_claim", "")).strip()
    _expect(bool(improvement_claim), "behavioral_outcome improvement_claim missing", errors)
    _expect(
        improvement_claim == _expected_report_improvement_claim(outcome_classification),
        "behavioral_outcome improvement_claim mismatch",
        errors,
    )

    review_refs = _as_dict(report.get("comparison_review_refs"))
    comparison_ref = str(review_refs.get("comparison_artifact_ref", "")).strip()
    _expect(bool(comparison_ref), "comparison_review_refs comparison_artifact_ref missing", errors)
    if comparison_ref:
        comparison_path = _artifact_path(repo_root, comparison_ref)
        _expect(comparison_path.exists(), "comparison_review_refs artifact path missing", errors)
        _expect(comparison_path == fixture_path, "comparison_review_refs artifact must point to fixture", errors)
    canonical = _as_dict(fixture.get("canonical_pair_record"))
    canonical_overlay_refs = _as_dict(canonical.get("auditable_evidence_overlay_refs"))
    auditable_capture = _as_dict(canonical.get("auditable_evidence_capture"))
    selected_modes = [str(mode).strip() for mode in _as_list(auditable_capture.get("selected_modes")) if str(mode).strip()]
    for mode in selected_modes:
        ref_key = f"{mode}_overlay_ref"
        value = str(review_refs.get(ref_key, "")).strip()
        _expect(bool(value), f"comparison_review_refs {ref_key} missing", errors)
        if value:
            _expect(_artifact_path(repo_root, value).exists(), f"{ref_key} path does not exist", errors)
            _expect(
                value == str(canonical_overlay_refs.get(ref_key, "")).strip(),
                f"comparison_review_refs {ref_key} mismatch with fixture canonical refs",
                errors,
            )

    _expect(
        bool(str(report.get("bounded_scope_note", "")).strip()),
        "report bounded_scope_note missing",
        errors,
    )
    _expect(report_path.exists(), "report path should exist", errors)
    return errors


def _validate_missing_evidence_ref_rejected(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(fixture)
    canonical = _as_dict(mutated.get("canonical_pair_record"))
    capture = _as_dict(canonical.get("auditable_evidence_capture"))
    selected_modes = [str(mode).strip() for mode in _as_list(capture.get("selected_modes")) if str(mode).strip()]
    if not selected_modes:
        errors.append("mutation probe missing selected_modes")
        return errors
    probe_mode = selected_modes[0]
    bundles = _as_dict(canonical.get("auditable_evidence_bundles"))
    mode_bundle = _as_dict(bundles.get(probe_mode))
    if not mode_bundle:
        errors.append(f"mutation probe missing {probe_mode} bundle")
        return errors
    mode_bundle.pop("baseline_evidence_ref", None)
    mutated_errors = _validate_fixture_contract(mutated, repo_root=repo_root, fixture_path=fixture_path)
    if not mutated_errors:
        errors.append("validator failed to reject missing baseline_evidence_ref mutation")
    elif not any("baseline_evidence_ref" in message for message in mutated_errors):
        errors.append("mutation was rejected, but baseline_evidence_ref was not reported in errors")
    return errors


def _validate_missing_replay_artifact_rejected(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(fixture)
    canonical = _as_dict(mutated.get("canonical_pair_record"))
    lanes = _as_dict(canonical.get("lanes"))
    baseline = _as_dict(lanes.get("baseline"))
    if not baseline:
        errors.append("mutation probe missing baseline lane")
        return errors
    baseline["replay_ref"] = "reports/praxis/ratworld/paired_runtime/evidence/does_not_exist.baseline.frames"
    mutated_errors = _validate_fixture_contract(mutated, repo_root=repo_root, fixture_path=fixture_path)
    if not mutated_errors:
        errors.append("validator failed to reject missing baseline replay artifact mutation")
    elif not any(
        "baseline" in message and "replay_ref does not exist" in message
        for message in mutated_errors
    ):
        errors.append("mutation was rejected, but baseline replay_ref missing-artifact error was not reported")
    return errors


def _validate_common_random_number_instability_rejected(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(fixture)
    control = _as_dict(mutated.get("common_random_number_control"))
    if not control:
        errors.append("mutation probe missing common_random_number_control block")
        return errors
    control["control_source"] = "build/logs/paired_runtime/seed_suites/unstable_control_source.txt"
    mutated_errors = _validate_fixture_contract(mutated, repo_root=repo_root, fixture_path=fixture_path)
    if not mutated_errors:
        errors.append("validator failed to reject unstable common-random-number control_source mutation")
    elif not any("common_random_number_control.control_source" in message for message in mutated_errors):
        errors.append(
            "mutation was rejected, but common_random_number_control.control_source mismatch was not reported"
        )
    return errors


def _validate_single_action_collapse_rejected(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(fixture)
    canonical = _as_dict(mutated.get("canonical_pair_record"))
    lanes = _as_dict(canonical.get("lanes"))
    baseline = _as_dict(lanes.get("baseline"))
    if not baseline:
        errors.append("mutation probe missing baseline lane")
        return errors
    baseline["accepted_actions"] = ["move_west"]
    baseline["accepted_action_unique_count"] = 1
    baseline["accepted_action_collapse_detected"] = True
    mutated_errors = _validate_fixture_contract(mutated, repo_root=repo_root, fixture_path=fixture_path)
    if not mutated_errors:
        errors.append("validator failed to reject single-action collapse mutation")
    elif not any(
        "accepted_actions collapsed to single unique token" in message
        or "accepted_action_collapse_detected" in message
        for message in mutated_errors
    ):
        errors.append("mutation was rejected, but accepted-action collapse error was not reported")
    return errors


def _validate_missing_dead_turn_cause_totals_rejected(
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(fixture)
    canonical = _as_dict(mutated.get("canonical_pair_record"))
    lanes = _as_dict(canonical.get("lanes"))
    baseline = _as_dict(lanes.get("baseline"))
    collapse = _as_dict(baseline.get("controller_output_collapse_by_caste"))
    pc_rat = _as_dict(collapse.get("pc_rat"))
    dead_turn_cause_totals = _as_dict(pc_rat.get("dead_turn_cause_totals"))
    if not baseline or not collapse or not pc_rat or not dead_turn_cause_totals:
        errors.append("mutation probe missing baseline controller_output_collapse_by_caste dead_turn data")
        return errors
    dead_turn_cause_totals.pop("blocked_by_geometry", None)
    mutated_errors = _validate_fixture_contract(mutated, repo_root=repo_root, fixture_path=fixture_path)
    if not mutated_errors:
        errors.append("validator failed to reject missing dead_turn_cause_totals mutation")
    elif not any("dead_turn_cause_totals.blocked_by_geometry" in message for message in mutated_errors):
        errors.append(
            "mutation was rejected, but missing dead_turn_cause_totals.blocked_by_geometry was not reported"
        )
    return errors


def _validate_no_overclaim_report_rejected(
    report: Dict[str, Any],
    fixture: Dict[str, Any],
    *,
    repo_root: Path,
    report_path: Path,
    fixture_path: Path,
) -> List[str]:
    errors: List[str] = []
    mutated = deepcopy(report)
    outcome = _as_dict(mutated.get("behavioral_outcome"))
    if not outcome:
        errors.append("mutation probe missing behavioral_outcome")
        return errors
    outcome["behavioral_outcome_statement"] = (
        "PRAXIS-proven superiority release-admission over baseline."
    )
    outcome["improvement_claim"] = "claimed"
    mutated_errors = _validate_report_contract(
        mutated,
        repo_root=repo_root,
        report_path=report_path,
        fixture=fixture,
        fixture_path=fixture_path,
    )
    if not mutated_errors:
        errors.append("validator failed to reject paired-runtime no-overclaim mutation")
    elif not any(
        "overclaim" in message or "improvement_claim" in message
        for message in mutated_errors
    ):
        errors.append(
            "mutation was rejected, but overclaim or improvement_claim mismatch was not reported"
        )
    return errors


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fixture",
        default=str(SELFTEST_FIXTURE_PATH),
    )
    parser.add_argument(
        "--report",
        default=str(SELFTEST_REPORT_PATH),
    )
    parser.add_argument("--repo-root", default=str(SELFTEST_REPO_ROOT))
    parser.add_argument("--self-check", action="store_true", default=False)
    parser.add_argument("--check", action="store_true", default=False)
    return parser.parse_args()


def run_paired_runtime_evidence_check(
    *,
    repo_root: Path,
    fixture_path: Path,
    report_path: Path,
) -> int:
    try:
        repo_root = repo_root.resolve()
    except Exception:  # noqa: BLE001
        pass

    try:
        fixture = _load_json(fixture_path)
        report = _load_json(report_path)
    except Exception as exc:  # noqa: BLE001
        print(f"[error] failed to load paired-runtime fixture/report: {exc}", file=sys.stderr)
        return 2

    errors: List[str] = []
    fixture_errors = _validate_fixture_contract(fixture, repo_root=repo_root, fixture_path=fixture_path)
    if fixture_errors:
        print("[error] ratworld paired-runtime harness evidence check failed:", file=sys.stderr)
        print(f"  - {fixture_path}: {fixture_errors[0]}", file=sys.stderr)
        return 1
    report_errors = _validate_report_contract(
            report,
            repo_root=repo_root,
            report_path=report_path,
            fixture=fixture,
            fixture_path=fixture_path,
        )
    if report_errors:
        print("[error] ratworld paired-runtime harness evidence check failed:", file=sys.stderr)
        print(f"  - {report_path}: {report_errors[0]}", file=sys.stderr)
        return 1

    mutation_errors = _validate_missing_evidence_ref_rejected(
        fixture,
        repo_root=repo_root,
        fixture_path=fixture_path,
    )
    errors.extend(f"{fixture_path}: {message}" for message in mutation_errors)

    mutation_errors = _validate_missing_replay_artifact_rejected(
        fixture,
        repo_root=repo_root,
        fixture_path=fixture_path,
    )
    errors.extend(f"{fixture_path}: {message}" for message in mutation_errors)

    mutation_errors = _validate_common_random_number_instability_rejected(
        fixture,
        repo_root=repo_root,
        fixture_path=fixture_path,
    )
    errors.extend(f"{fixture_path}: {message}" for message in mutation_errors)

    mutation_errors = _validate_single_action_collapse_rejected(
        fixture,
        repo_root=repo_root,
        fixture_path=fixture_path,
    )
    errors.extend(f"{fixture_path}: {message}" for message in mutation_errors)

    mutation_errors = _validate_missing_dead_turn_cause_totals_rejected(
        fixture,
        repo_root=repo_root,
        fixture_path=fixture_path,
    )
    errors.extend(f"{fixture_path}: {message}" for message in mutation_errors)

    mutation_errors = _validate_no_overclaim_report_rejected(
        report,
        fixture,
        repo_root=repo_root,
        report_path=report_path,
        fixture_path=fixture_path,
    )
    errors.extend(f"{report_path}: {message}" for message in mutation_errors)
    if errors:
        print("[error] ratworld paired-runtime harness evidence check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        "[ok] ratworld paired-runtime harness evidence verified: auditable selected-mode refs, "
        "canonical behavioral-evidence fields (including anti-collapse controller/dead-turn exports), "
        "pair/common-random-number alignment, "
        "classification-consistent bounded report wording, no-overclaim rejection, "
        "and missing-artifact rejection"
    )
    return 0


def main() -> int:
    args = _parse_args()
    if args.self_check:
        repo_root = SELFTEST_REPO_ROOT
        fixture_path = SELFTEST_FIXTURE_PATH
        report_path = SELFTEST_REPORT_PATH
    else:
        repo_root = Path(args.repo_root)
        fixture_path = Path(args.fixture)
        report_path = Path(args.report)
    return run_paired_runtime_evidence_check(
        repo_root=repo_root,
        fixture_path=fixture_path,
        report_path=report_path,
    )


if __name__ == "__main__":
    raise SystemExit(main())
