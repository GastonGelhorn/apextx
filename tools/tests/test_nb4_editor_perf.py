# SPDX-License-Identifier: GPL-2.0-only
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'nb4-editor-perf.py'


class PerformanceAcceptance(unittest.TestCase):
    def compare(self, ui=None, lvgl=None, idle=False):
        def capture():
            return dict(format='apextx-ui-perf-1', serial='NB4', firmware='test',
                        samples=[dict(loopAvgUs=100, lvglAvgUs=50, pixels=0, areas=0)
                                 for _ in range(30)])
        with tempfile.TemporaryDirectory() as folder:
            baseline, candidate = capture(), capture()
            for index, value in enumerate(ui or [], 2):
                candidate['samples'][index]['loopAvgUs'] = value
            for index, value in enumerate(lvgl or [], 2):
                candidate['samples'][index]['lvglAvgUs'] = value
            paths = [Path(folder) / name for name in ('baseline.json', 'candidate.json')]
            for path, value in zip(paths, (baseline, candidate)):
                path.write_text(json.dumps(value))
            args = [str(paths[0]), '--idle'] if idle else [str(path) for path in paths]
            result = subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True)
            return result.returncode, json.loads(result.stdout)

    def test_mean_rejects_spike_hidden_by_median(self):
        code, report = self.compare(ui=[1000])
        self.assertEqual(code, 1)
        self.assertGreater(report['timings']['ui']['overhead'], .10)

    def test_lvgl_regression_is_checked_independently(self):
        code, report = self.compare(lvgl=[100] * 26)
        self.assertEqual(code, 1)
        self.assertEqual(report['timings']['lvgl']['overhead'], 1)

    def test_both_means_below_limit_pass(self):
        code, report = self.compare(ui=[109] * 26, lvgl=[54] * 26)
        self.assertEqual(code, 0)
        self.assertTrue(report['passed'])

    def test_idle_accepts_zero_repaints(self):
        code, report = self.compare(idle=True)
        self.assertEqual(code, 0)
        self.assertTrue(report['idle'])


if __name__ == '__main__':
    unittest.main()
