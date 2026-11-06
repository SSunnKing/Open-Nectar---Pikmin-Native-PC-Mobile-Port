import pathlib
import tempfile
import unittest
from check_throw_flight import EXPECTED, check_log

LINE = ('[scenario] throw_flight_{case}: THROW {i} dist 25 yaw 90 pikidist 8.00 grab {g} release {r} flight {f} (0.967 s) '
        'relY 10.00 peak +{peak:.2f} (over ground 71.29) h 75.0 vS {vs:.2f} vel (24.99 271.33 0.00) naviVel (0.00 -13.07 0.00) '
        'hold 0.000 charge 0 land (24.99 0.00) off (-0.00 0.00) err 0.00 y-ground {yg:.2f} state 0 mode 0\n')


class CheckerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = pathlib.Path(self.temp.name) / 'scenario_throw_flight_flower_60.log'

    def write(self, case='flower', rate=60, change=None, count=None, passed=True):
        n, values = EXPECTED[case]
        g, r, f, peak, vs = values[rate]
        lines = []
        for i in range(n if count is None else count):
            fields = dict(case=case, i=i, g=g, r=r, f=f, peak=peak, vs=vs, yg=0.0)
            if change and i == 3: fields.update(change)
            lines.append(LINE.format(**fields))
        lines.append(f'[scenario] PASS throw_flight_{case} at tick 2917 (intro 30 ticks)\n' if passed else
                     f'[scenario] FAIL throw_flight_{case} at tick 2917\n')
        self.path.write_text(''.join(lines))
        return check_log(self.path, case, rate)

    def test_disc_values_pass(self):
        for case in EXPECTED:
            for rate in (30, 60):
                self.assertEqual(self.write(case, rate), [], (case, rate))

    def test_float_noise_within_tolerance(self):
        self.assertEqual(self.write(change={'peak': 68.04, 'vs': 289.91}), [])

    def test_flight_tick_off_by_one(self):
        self.assertRegex(' '.join(self.write(change={'f': 70})), r'throw 3 .*grab/release/flight \(2, 3, 70\)')

    def test_grab_and_release(self):
        self.assertTrue(self.write(change={'g': 3}))
        self.assertTrue(self.write(change={'r': 2}))

    def test_peak_and_launch(self):
        self.assertRegex(' '.join(self.write(change={'peak': 68.10})), 'peak 68.10')
        self.assertRegex(' '.join(self.write(change={'vs': 289.8})), 'launch vy 289.80')

    def test_landing_on_an_object(self):
        self.assertRegex(' '.join(self.write(change={'yg': 85.29})), 'on an object')

    def test_missing_throws_or_pass(self):
        self.assertTrue(self.write(count=17))
        self.assertTrue(self.write(passed=False))

    def test_the_old_glide(self):
        # p48 at the code default: the stand-in flower glided 122 ticks at 60 Hz.
        self.assertTrue(self.write(change={'f': 122}))

    def test_yellow_on_the_onion(self):
        # The stand-in yellow from (-228,-276): throws onto the red Onion ended after 22 or 27 ticks.
        self.assertEqual(self.write('yellow', 30), [])
        self.assertTrue(self.write('yellow', 30, change={'f': 27, 'yg': 107.62}))
        self.assertTrue(self.write('yellow', 30, change={'f': 22}))
        self.assertTrue(self.write('yellow', 60, change={'peak': 104.40}))
        self.assertTrue(self.write('yellow', 60, change={'vs': 290.0}))


if __name__ == '__main__':
    unittest.main()
