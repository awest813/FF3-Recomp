#!/usr/bin/env python3
"""Asset-free validation plus optional integration with the real framework tools."""

import argparse
import contextlib
import csv
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

PROFILE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ff3", PROFILE / "tools/ff3.py")
ff3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ff3)
ANALYZER = None
RECOMPILER = None


def synthetic_elf():
    """Independent three-instruction MIPS function: return 42, with delay slot."""
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 8, 1, 0x08804000,
                         52, 0, 0, 52, 32, 1, 40, 0, 0)
    program = struct.pack("<8I", 1, 0x80, 0x08804000, 0x08804000, 12, 12, 5, 16)
    return header + program + bytes(0x80 - 84) + struct.pack("<3I", 0x2402002A, 0x03E00008, 0)


class ToolingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ff3 tests with spaces ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.elf = self.root / "synthetic.elf"
        self.elf.write_bytes(synthetic_elf())
        self.config = ff3.read_config(PROFILE / "config/profile.json")

    def test_identity_and_hash_pin(self):
        identity = ff3.inspect_elf(self.elf)
        self.assertEqual(identity["entry"], "0x08804000")
        self.assertFalse(identity["identity_pinned"])
        self.assertTrue(ff3.inspect_elf(self.elf, identity["sha256"])["identity_pinned"])
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            ff3.inspect_elf(self.elf, "0" * 64)

    def test_reject_containers_and_malformed_elf(self):
        cases = [(b"~PSP" + bytes(48), "decrypted"), (b"\0PBP" + bytes(48), "PBP"),
                 (bytes(52), "Expected a decrypted"), (b"\x7fELF", "complete ELF32")]
        for offset, value, message in [(4, b"\x02", "ELF32"), (18, b"\x3e\x00", "MIPS"),
                                       (44, b"\x08\x00", "program-header"),
                                       (68, struct.pack("<I", 1000), "past end")]:
            data = bytearray(synthetic_elf())
            data[offset:offset + len(value)] = value
            cases.append((data, message))
        for data, message in cases:
            with self.subTest(message=message):
                self.elf.write_bytes(data)
                with self.assertRaisesRegex(ValueError, message):
                    ff3.inspect_elf(self.elf)

    def test_bad_configuration_and_missing_input(self):
        config_path = self.root / "config.json"
        for key, value in [("load_base", "0x08804001"), ("unit_span_bytes", 0),
                           ("expected_sha256", "not-a-hash"), ("schema_version", 2)]:
            config = dict(self.config, **{key: value})
            config_path.write_text(json.dumps(config))
            with self.subTest(key=key), self.assertRaises(ValueError):
                ff3.read_config(config_path)
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(ff3.main(["inspect", str(self.root / "missing.elf")]), 1)
        self.assertIn("ff3:", errors.getvalue())

    def test_prepare_pinned_dump_and_preserve_existing_file(self):
        identity = ff3.inspect_elf(self.elf)
        config = dict(self.config, expected_size_bytes=self.elf.stat().st_size,
                      expected_sha256=identity["sha256"])
        padded = self.root / "dump.bin"
        padded.write_bytes(self.elf.read_bytes() + b"dump trailing bytes")
        output = self.root / "game/EBOOT.ELF"
        ff3.prepare_input(padded, output, config)
        self.assertEqual(output.read_bytes(), self.elf.read_bytes())
        ff3.prepare_input(padded, output, config)  # identical destination is safe
        self.assertTrue(padded.read_bytes().endswith(b"dump trailing bytes"))
        changed = bytearray(output.read_bytes()); changed[0x80] ^= 1
        output.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            ff3.prepare_input(padded, output, config)
        self.assertEqual(output.read_bytes(), changed)
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            ff3.prepare_input(output, self.root / "bad.elf", config)
        self.assertFalse((self.root / "bad.elf").exists())
        with self.assertRaisesRegex(ValueError, "requires a config"):
            ff3.prepare_input(padded, output, self.config)

    def test_generation_gates_and_failed_generator_preserve_output(self):
        analysis = self.root / "analysis"
        generated = analysis / "generated"
        generated.mkdir(parents=True)
        marker = generated / "ff3_codegen.json"
        marker.write_text('{"old": true}')
        args = argparse.Namespace(analysis_dir=analysis, elf=self.elf, recompiler="unused")
        report = {"relocations": {"invalid": 0, "unsupported": 0},
                  "automatic_analysis": {"function_count": 1, "truncated_functions": 0,
                                         "unsupported_instruction_occurrences": 0}}
        identity = ff3.inspect_elf(self.elf)
        for section, key in [("relocations", "invalid"), ("relocations", "unsupported"),
                             ("automatic_analysis", "truncated_functions"),
                             ("automatic_analysis", "unsupported_instruction_occurrences")]:
            report[section][key] = 1
            with self.subTest(key=key), patch.object(ff3, "run_tool") as run:
                with self.assertRaisesRegex(ValueError, "Generation stopped"):
                    ff3.generate(args, self.config, identity, report)
                run.assert_not_called()
            report[section][key] = 0
        with patch.object(ff3, "run_tool", side_effect=subprocess.CalledProcessError(1, "generator")):
            with self.assertRaises(subprocess.CalledProcessError):
                ff3.generate(args, self.config, identity, report)
        self.assertEqual(marker.read_text(), '{"old": true}')
        marker.unlink()
        with self.assertRaisesRegex(ValueError, "unmanaged"):
            ff3.generate(args, self.config, identity, report)

    def test_probe_reports_stops_errors_and_timeouts(self):
        host = self.root / "probe-host"
        host.write_bytes(b"synthetic host identity")
        args = argparse.Namespace(host=str(host), max_dispatches=64, timeout=1, hle="bootstrap",
                                  analysis_dir=self.root / "probe", elf=self.elf)
        identity = ff3.inspect_elf(self.elf)
        with patch.object(ff3.shutil, "which", return_value=str(host)), \
             patch.object(ff3.subprocess, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            run.return_value = subprocess.CompletedProcess([], 2, stdout=b"stop=Missing HLE import fixture\n")
            self.assertEqual(ff3.probe(args, self.config, identity), 0)
            report = json.loads((args.analysis_dir / "ff3_probe.json").read_text())
            self.assertEqual(report["status"], "diagnostic_stop")
            self.assertFalse(report["boot_validated"])
            self.assertEqual(report["hle_mode"], "bootstrap")
            self.assertEqual(run.call_args.args[0][-1], "bootstrap")
            args.max_dispatches = 100000000
            self.assertEqual(ff3.probe(args, self.config, identity), 0)
            self.assertEqual(run.call_args.args[0][2], "100000000")
            args.max_dispatches = 100000001
            with self.assertRaisesRegex(ValueError, "Probe limits"):
                ff3.probe(args, self.config, identity)
            args.max_dispatches = 64
            args.hle = "none"
            self.assertEqual(ff3.probe(args, self.config, identity), 0)
            report = json.loads((args.analysis_dir / "ff3_probe.json").read_text())
            self.assertEqual(report["hle_mode"], "none")
            self.assertEqual(run.call_args.args[0][-1], "none")
            self.assertEqual(run.call_args.kwargs["timeout"], 1)
            self.assertEqual(run.call_args.kwargs["env"]["PSPRECOMP_NO_CHAIN"], "1")
            run.return_value = subprocess.CompletedProcess([], 1, stdout=b"bad identity")
            self.assertEqual(ff3.probe(args, self.config, identity), 1)
            run.side_effect = subprocess.TimeoutExpired([], 1, output=b"last dispatch\n")
            self.assertEqual(ff3.probe(args, self.config, identity), 1)
            report = json.loads((args.analysis_dir / "ff3_probe.json").read_text())
            self.assertEqual(report["status"], "wall_clock_limit")
            self.assertEqual((args.analysis_dir / "ff3_probe.log").read_text(), "last dispatch\n")
            args.max_dispatches = 0
            with self.assertRaisesRegex(ValueError, "Probe limits"):
                ff3.probe(args, self.config, identity)

    def test_real_analysis_and_generation(self):
        if not ANALYZER or not RECOMPILER:
            self.skipTest("Supply --analyzer and --recompiler to run integration")
        analysis = self.root / "analysis with spaces"
        args = ["generate", str(self.elf), "--analysis-dir", str(analysis),
                "--analyzer", ANALYZER, "--recompiler", RECOMPILER]
        self.assertEqual(ff3.main(args), 0)
        report = json.loads((analysis / "ff3_report.json").read_text())
        self.assertEqual(report["automatic_analysis"]["function_count"], 1)
        self.assertEqual(report["entry_runtime"], "0x08804000")
        for artifact in report["artifacts"].values():
            self.assertTrue(Path(artifact).is_file())
        generated = analysis / "generated"
        original = {path.name: path.read_bytes() for path in generated.glob("*.cpp")}
        self.assertIn("generated_registry.cpp", original)
        self.assertTrue(any(name.startswith("generated_unit_") for name in original))
        for path in generated.glob("*.cpp"):
            os.utime(path, ns=(1_600_000_000_123_000_000, 1_600_000_000_123_000_000))
        mtimes = {path.name: path.stat().st_mtime_ns for path in generated.glob("*.cpp")}
        # Successful regeneration replaces obsolete units and preserves determinism.
        (generated / "generated_unit_9999.cpp").write_text("stale generated source")
        self.assertEqual(ff3.main(args), 0)
        self.assertEqual({path.name: path.read_bytes() for path in generated.glob("*.cpp")}, original)
        self.assertEqual({path.name: path.stat().st_mtime_ns for path in generated.glob("*.cpp")}, mtimes)
        manifest = json.loads((generated / "ff3_codegen.json").read_text())
        self.assertEqual(manifest["sha256"], ff3.inspect_elf(self.elf)["sha256"])
        self.assertEqual(manifest["command"][3], str(generated))

    def test_real_unsupported_delay_slot_blocks_generation(self):
        if not ANALYZER or not RECOMPILER:
            self.skipTest("Supply --analyzer and --recompiler to run integration")
        data = bytearray(synthetic_elf())
        struct.pack_into("<I", data, 0x88, 0x48000000)  # unsupported COP2 after jr ra
        self.elf.write_bytes(data)
        analysis = self.root / "unsupported"
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            status = ff3.main(["generate", str(self.elf), "--analysis-dir", str(analysis),
                               "--analyzer", ANALYZER, "--recompiler", RECOMPILER])
        self.assertEqual(status, 1)
        self.assertIn("unsupported instructions", errors.getvalue())
        with (analysis / "ff3_report_unsupported.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["address"], "0x08804008")
        self.assertEqual(rows[0]["delay_slot"], "true")
        self.assertFalse((analysis / "generated").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--analyzer")
    parser.add_argument("--recompiler")
    options, remaining = parser.parse_known_args()
    ANALYZER, RECOMPILER = options.analyzer, options.recompiler
    unittest.main(argv=[sys.argv[0]] + remaining)
