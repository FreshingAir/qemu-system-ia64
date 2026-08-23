"""Typed, explicit registry for IA-64 architectural microprogram cases."""

from __future__ import annotations

from collections import Counter

from . import (cases_core, cases_fp, cases_interrupt, cases_memory_nat,
               cases_mmu, cases_pal, cases_rse, encoding)
from .case import IA64Case


GROUP_MODULES = {
    "core": cases_core,
    "memory-nat": cases_memory_nat,
    "fp": cases_fp,
    "rse": cases_rse,
    "mmu": cases_mmu,
    "interrupt": cases_interrupt,
    "pal": cases_pal,
}

GROUPS = tuple(GROUP_MODULES)


def _registered_cases() -> dict[str, IA64Case]:
    cases: dict[str, IA64Case] = {}
    for module in GROUP_MODULES.values():
        for name, case in module.CASES.items():
            if name in cases:
                raise RuntimeError(f"duplicate IA-64 case id {name!r}")
            cases[name] = case
    return cases


CASES_BY_NAME = _registered_cases()


def cases_for_group(group: str) -> dict[str, IA64Case]:
    if group not in GROUP_MODULES:
        raise ValueError(f"unknown IA-64 test group {group!r}")
    return dict(GROUP_MODULES[group].CASES)


def all_cases() -> dict[str, IA64Case]:
    return dict(sorted(CASES_BY_NAME.items()))


def _validate_case_programs(cases: dict[str, IA64Case]) -> None:
    duplicate_bundle_cases: list[str] = []
    missing_successor_cases: list[str] = []
    effect_at_terminal_cases: list[str] = []

    for name, case in cases.items():
        if not case.bundles:
            continue
        program = encoding.normalized_bundles(case.bundles)
        address_counts = Counter(bundle[0] for bundle in program)
        for address, count in address_counts.items():
            if count > 1:
                duplicate_bundle_cases.append(f"{name}@0x{address:x}")

        for bundle in program:
            address = bundle[0]
            has_effect = bundle[2] != encoding.nop_m() or bundle[3] not in (
                encoding.nop_i(), encoding.nop_f(), encoding.nop_m())
            if not has_effect:
                continue
            if bundle[4] == encoding.br_cond(address, address + 0x10) and \
                    address + 0x10 not in address_counts:
                missing_successor_cases.append(f"{name}@0x{address:x}")

        terminal_ip = case.expected.get("ip")
        # A fault leaves IP at the faulting bundle, whose effects do not
        # retire.  That is a valid observation point, not a terminal loop.
        if not isinstance(terminal_ip, int) or \
                case.expected.get("fault_ip") == terminal_ip:
            continue
        for bundle in program:
            if bundle[0] == terminal_ip:
                if bundle[2] != encoding.nop_m() or bundle[3] not in (
                        encoding.nop_i(), encoding.nop_f(),
                        encoding.nop_m()):
                    effect_at_terminal_cases.append(name)
                break

    if duplicate_bundle_cases:
        raise RuntimeError(
            "IA-64 cases contain colliding bundle addresses: " +
            ", ".join(sorted(duplicate_bundle_cases)))
    if missing_successor_cases:
        raise RuntimeError(
            "IA-64 effect bundles branch to an absent successor: " +
            ", ".join(sorted(missing_successor_cases)))
    if effect_at_terminal_cases:
        raise RuntimeError(
            "IA-64 cases observe a terminal bundle before its effects retire: " +
            ", ".join(sorted(effect_at_terminal_cases)))


def validate_registry() -> None:
    manifest_names = [
        name
        for module in GROUP_MODULES.values()
        for name in module.CASE_NAMES
    ]
    manifest_duplicates = sorted(
        name for name, count in Counter(manifest_names).items() if count > 1)
    registered_names = set(CASES_BY_NAME)
    missing = sorted(set(manifest_names) - registered_names)
    extra = sorted(registered_names - set(manifest_names))
    if manifest_duplicates or missing or extra:
        raise RuntimeError(
            "invalid explicit IA-64 family membership: "
            f"duplicates={manifest_duplicates!r}, missing={missing!r}, "
            f"extra={extra!r}")

    for name, case in CASES_BY_NAME.items():
        if not isinstance(case, IA64Case):
            raise RuntimeError(f"untyped IA-64 case {name!r}")
        if case.name != name:
            raise RuntimeError(
                f"IA-64 registry key {name!r} disagrees with {case.name!r}")
    _validate_case_programs(CASES_BY_NAME)
