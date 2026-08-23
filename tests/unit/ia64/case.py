"""Typed IA-64 architectural microprogram cases and registration helpers."""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from typing import Any


CaseRunner = Callable[[str], None]
Bundle = tuple[int, int, int, int, int]


@dataclass(frozen=True)
class IA64Case:
    """One named, callable IA-64 architectural test."""

    name: str
    runner: CaseRunner
    bundles: tuple[Bundle, ...] = ()
    expected: Mapping[str, Any] = field(default_factory=dict)

    def __call__(self, qemu: str) -> None:
        self.runner(qemu)


def _as_case(name: str, value: IA64Case | CaseRunner) -> IA64Case:
    if isinstance(value, IA64Case):
        if value.name != name:
            raise RuntimeError(
                f"IA-64 case id {name!r} disagrees with {value.name!r}")
        return value
    if callable(value):
        return IA64Case(name=name, runner=value)
    raise RuntimeError(f"IA-64 case {name!r} is not callable")


def bind_cases(group: str, case_names: Sequence[str],
               namespace: Mapping[str, Any], *,
               extras: Mapping[str, IA64Case | CaseRunner] | None = None
               ) -> dict[str, IA64Case]:
    """Resolve an explicit family manifest into typed, uniquely named cases."""

    extras = extras or {}
    duplicates = sorted({name for name in case_names
                         if case_names.count(name) > 1})
    if duplicates:
        raise RuntimeError(f"duplicate IA-64 case ids in {group}: {duplicates}")

    cases: dict[str, IA64Case] = {}
    for name in case_names:
        value = namespace.get("test_" + name)
        if value is None:
            value = extras.get(name)
        if value is None:
            raise RuntimeError(f"missing IA-64 {group} case {name!r}")
        cases[name] = _as_case(name, value)
    return cases
