import importlib.util, io, os, struct, tempfile, unittest
from contextlib import redirect_stderr, redirect_stdout

try:
    import numpy as np
except ImportError:   # the tool needs numpy; without it there is nothing to test
    np = None

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'audio-check.py')
if np is not None:
    spec = importlib.util.spec_from_file_location('audio_check', TOOL)
    audio_check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audio_check)

RATE = 48000


def wav_bytes(samples, rate=RATE):
    """A 16-bit PCM WAV of an (frames, channels) int16 array, with an extra chunk before the data."""
    frames, channels = samples.shape
    body = samples.astype('<i2').tobytes()
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * 2, channels * 2, 16)
    extra = b'LIST' + struct.pack('<I', 3) + b'abc' + b'\x00'   # odd-sized chunk and its pad byte
    chunks = b'fmt ' + struct.pack('<I', len(fmt)) + fmt + extra + b'data' + struct.pack('<I', len(body)) + body
    return b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks


def tone(seconds, frequency=440.0, amplitude=8000.0, channels=2):
    t = np.arange(int(seconds * RATE)) / RATE
    wave = (amplitude * np.sin(2 * np.pi * frequency * t)).astype(np.int16)
    return np.stack([wave] * channels, axis=1)


@unittest.skipIf(np is None, 'numpy is not installed')
class AudioCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, samples, name='a.wav'):
        path = os.path.join(self.tmp.name, name)
        with open(path, 'wb') as f:
            f.write(wav_bytes(samples))
        return path

    def test_reads_a_wav_past_other_chunks(self):
        samples = tone(0.5)
        rate, back = audio_check.read_wav(self.write(samples))
        self.assertEqual(rate, RATE)
        self.assertTrue((back == samples).all())

    def test_level_of_a_sine(self):
        report = audio_check.measure(tone(1.0, amplitude=16384.0), RATE)
        self.assertAlmostEqual(report['peak_dbfs'][0], -6.02, delta=0.05)
        self.assertAlmostEqual(report['rms_dbfs'][0], -9.03, delta=0.05)
        self.assertEqual(report['clipped_share'], 0.0)
        self.assertEqual(report['clicks'], 0)
        self.assertEqual(report['silent_gaps'], [])
        self.assertAlmostEqual(report['left_right_correlation'], 1.0, places=6)

    def test_silent_gap_is_found_with_its_position(self):
        samples = np.concatenate((tone(0.5), np.zeros((RATE // 4, 2), dtype=np.int16), tone(0.5)))
        report = audio_check.measure(samples, RATE, offset=10.0)
        self.assertEqual(len(report['silent_gaps']), 1)
        start, length = report['silent_gaps'][0]
        self.assertAlmostEqual(start, 10.5, delta=0.01)
        self.assertAlmostEqual(length, 0.25, delta=0.01)
        self.assertAlmostEqual(report['silent_share'], 0.2, delta=0.01)

    def test_a_short_pause_is_not_a_gap(self):
        samples = np.concatenate((tone(0.5), np.zeros((RATE // 100, 2), dtype=np.int16), tone(0.5)))
        self.assertEqual(audio_check.measure(samples, RATE)['silent_gaps'], [])

    def test_clipping_share(self):
        samples = tone(1.0, amplitude=8000.0)
        samples[1000:1100] = 32767
        samples[2000:2100, 0] = -32768
        self.assertAlmostEqual(audio_check.measure(samples, RATE)['clipped_share'], 300 / samples.size, places=9)

    def test_clipping_under_a_master_volume_just_below_one_counts(self):
        # The SPU2 clamps the mix and then applies the master volume; Burnout 3's is 0x3FFF, so its clipped
        # samples sit at +32765 and -32766, not at the 16-bit limits.
        samples = tone(1.0, amplitude=8000.0)
        samples[1000:1100] = 32765
        samples[2000:2100, 0] = -32766
        samples[3000:3100] = 32764                       # one step lower is not clipping
        self.assertAlmostEqual(audio_check.measure(samples, RATE)['clipped_share'], 300 / samples.size, places=9)

    def test_an_isolated_step_is_a_click_and_a_loud_tone_is_not(self):
        quiet = tone(1.0, frequency=200.0, amplitude=2000.0)
        quiet[RATE // 2:, 0] += 9000                     # one step in the left channel at 0.5 s
        report = audio_check.measure(quiet, RATE)
        self.assertEqual(report['clicks'], 1)
        self.assertAlmostEqual(report['click_times'][0], 0.5, delta=0.001)
        loud = tone(1.0, frequency=6000.0, amplitude=30000.0)   # large jumps, but every one of them
        self.assertEqual(audio_check.measure(loud, RATE)['clicks'], 0)

    def test_batch_seams_show_a_glitch_once_per_batch(self):
        clean = tone(1.0, frequency=300.0)
        self.assertAlmostEqual(audio_check.batch_ratio(clean[:, 0], 32), 1.0, delta=0.1)
        seamed = clean.copy()
        seamed[(np.arange(len(seamed)) // 32) % 2 == 1] += 3000   # every other batch shifted: a step at each seam only
        self.assertGreater(audio_check.batch_ratio(seamed[:, 0], 32), 10.0)

    def test_energy_lands_in_the_band_of_the_tone(self):
        bands = audio_check.measure(tone(1.0, frequency=1000.0), RATE)['bands']
        self.assertGreater(bands['500-2000 Hz'], 0.99)

    def test_spectrogram_is_a_png_of_the_asked_size(self):
        path = os.path.join(self.tmp.name, 's.png')
        self.assertTrue(audio_check.spectrogram(tone(2.0)[:, 0].astype(np.float64), RATE, path, width=300, height=100))
        with open(path, 'rb') as f:
            data = f.read()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack('>II', data[16:24]), (300, 100))

    def test_command_line(self):
        path = self.write(np.concatenate((tone(1.0), tone(1.0, amplitude=16384.0))))
        out = io.StringIO()
        with redirect_stdout(out):
            code = audio_check.main(['audio-check.py', path, '--from', '1', '--window', '0.5'])
        self.assertEqual(code, 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0], '1.000 s, 48000 Hz, 2 channels, 48000 frames')
        self.assertIn('peak dBFS -6.0 -6.0', lines[1])
        self.assertEqual(lines[-2].split()[0], '1.0')    # window starts are in the file's time
        self.assertEqual(lines[-1].split()[0], '1.5')

    def test_bad_arguments_and_bad_files(self):
        err = io.StringIO()
        with redirect_stderr(err):
            self.assertEqual(audio_check.main(['audio-check.py']), 2)
            self.assertEqual(audio_check.main(['audio-check.py', 'a.wav', '--nonsense']), 2)
            self.assertEqual(audio_check.main(['audio-check.py', os.path.join(self.tmp.name, 'missing.wav')]), 1)
            bad = os.path.join(self.tmp.name, 'bad.wav')
            with open(bad, 'wb') as f:
                f.write(b'not a wav file at all')
            self.assertEqual(audio_check.main(['audio-check.py', bad]), 1)


if __name__ == '__main__':
    unittest.main()
