import unittest
from summarize_pipeline_latency import analyze, elapsed


class TimingTest(unittest.TestCase):
    def test_missing_or_reversed_is_not_zero(self):
        self.assertIsNone(elapsed({}, "a", "b"))
        self.assertIsNone(elapsed({"a": 2, "b": 3}, "a", "b"))
        self.assertEqual(elapsed({"a": 3, "b": 3}, "a", "b"), 0)

    def test_legacy_capture_gap_is_not_source_measurement(self):
        self.assertEqual(analyze(["[user-feedback] capGapUs=500000 totalUs=0"])["frame_samples"], 0)

    def test_compact_gaps_are_kept_outside_detailed_sample(self):
        r = analyze(["[present] seq=1 frameGapUs=16000",
                     "[present] seq=2 frameGapUs=500000 timingSchema=2"])
        self.assertEqual(r["present_gaps"]["samples"], 2)
        self.assertEqual(r["frame_samples"], 1)

    def test_two_clocks_and_full_queue(self):
        result = analyze([
            "stage=clock clientRecvUs=1100 clockOffsetUs=10000 rttUs=20",
            "[present] timingSchema=2 seq=1 gen=1 synthetic=0 frameGapUs=500000 "
            "hostCaptureUs=10900 hostEncodeStartUs=10910 hostEncodeEndUs=10930 hostSendUs=10950 "
            "clientRecvUs=1000 clientDecodeStartUs=1010 clientDecodeEndUs=1020 "
            "clientQueueSetUs=1030 clientPaintStartUs=1080 clientPresentUs=1100"])
        row = result["frames"][0]
        self.assertEqual(row["network_estimate_us"], 50)
        self.assertEqual(row["capture_to_present_estimate_us"], 200)
        self.assertEqual(row["queue_wait_us"], 50)
        self.assertEqual(row["paint_us"], 20)
        self.assertEqual(row["clock_uncertainty_us"], 10)
        self.assertEqual(row["capture_to_encode_us"], 10)

    def test_failed_input_and_missing_clock_remain_visible(self):
        r = analyze(["[input-timing] clientGeneratedUs=100 clientSendUs=300 clientDoneUs=800 ok=0",
                     "[present] timingSchema=2 clientPresentUs=900 hostCaptureUs=20000"])
        self.assertEqual(r["input_queue"]["max_us"], 200)
        self.assertEqual(r["input_exchange"]["max_us"], 500)
        self.assertIsNone(r["frames"][0]["capture_to_present_estimate_us"])


if __name__ == "__main__":
    unittest.main()
