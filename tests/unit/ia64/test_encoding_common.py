#!/usr/bin/env python3
"""Host-only tests for IA-64 bundle stop selection."""

from __future__ import annotations

import os
import sys
import unittest

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
    from ia64.encoding_common import (EndGroupInsn, bundle_words, nop_i,
                                      nop_m)
else:
    from .encoding_common import EndGroupInsn, bundle_words, nop_i, nop_m


def encoded_template(template, slot0, slot1, slot2):
    low, _ = bundle_words(template, slot0, slot1, slot2)
    return low & 0x1f


class BundleStopTest(unittest.TestCase):
    def test_slot0_stop_uses_mmi_template(self):
        self.assertEqual(
            encoded_template(0x00, EndGroupInsn(nop_m()), nop_i(), nop_i()),
            0x0a)
        self.assertEqual(
            encoded_template(0x01, EndGroupInsn(nop_m()), nop_i(), nop_i()),
            0x0b)
        self.assertEqual(
            encoded_template(0x08, EndGroupInsn(nop_m()), nop_i(), nop_i()),
            0x0a)
        self.assertEqual(
            encoded_template(0x09, EndGroupInsn(nop_m()), nop_i(), nop_i()),
            0x0b)

    def test_slot0_stop_does_not_silently_change_non_nop_unit(self):
        with self.assertRaisesRegex(ValueError, "non-NOP MII slot 1"):
            bundle_words(0x00, EndGroupInsn(nop_m()), 0, nop_i())

    def test_slot1_stop_uses_mii_template(self):
        self.assertEqual(
            encoded_template(0x00, nop_m(), EndGroupInsn(nop_i()), nop_i()),
            0x02)
        self.assertEqual(
            encoded_template(0x01, nop_m(), EndGroupInsn(nop_i()), nop_i()),
            0x03)

    def test_slot2_stop_sets_end_bit(self):
        self.assertEqual(
            encoded_template(0x00, nop_m(), nop_i(),
                             EndGroupInsn(nop_i())),
            0x01)


if __name__ == "__main__":
    unittest.main()
