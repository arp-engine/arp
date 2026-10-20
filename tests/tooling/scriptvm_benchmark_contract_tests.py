"""Correctness oracles and malformed measurement rejection, no timing gates."""
import sys
import unittest
import tempfile
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"bench"))
from scriptvm_benchmark_suite import BenchmarkSuite, expected_checksum, parse_native
from scriptvm_python_corpus import CASE_NAMES, PythonCorpus
from scriptvm_stress_benchmark import Result, enforce_performance_gate


class BenchmarkContractTests(unittest.TestCase):
    def test_inline_interpreter_can_outperform_aot_value_frames(self):
        enforce_performance_gate({"power_floor":Result(1,1,1.0,1,5.0)})

    def test_native_scalar_lowering_still_requires_speed_advantage(self):
        with self.assertRaisesRegex(RuntimeError,"scalar speedup"):
            enforce_performance_gate({"int_loop":Result(1,1,1.0,1,1.0)})

    def test_performance_gate_rejects_invalid_times(self):
        for milliseconds in (0.0,-1.0,float("nan"),float("inf")):
            with self.subTest(milliseconds=milliseconds), self.assertRaisesRegex(RuntimeError,"invalid"):
                enforce_performance_gate({"power_floor":Result(1,1,1.0,1,milliseconds)})

    def test_independent_oracle_at_edges_and_partial_cycles(self):
        corpus=PythonCorpus()
        corpus.initialize(corpus.compile())
        for name in CASE_NAMES:
            for n in (0,1,2,3,4,7,9,17,33,257):
                if name=="recursive_fib" and n>4: continue
                with self.subTest(name=name,n=n):
                    self.assertEqual(corpus.invoke(name,n),expected_checksum(name,n))

    def test_unsupported_translation_fails_closed(self):
        with self.assertRaisesRegex(ValueError,"unsupported typed local"):
            PythonCorpus("def f():\n    var x: FutureType = 1\n")

    def native_output(self):
        return "\n".join(("PROFILE Release","SETUP 3.0 0.0",
            "COLD int_loop interpreter 4 6 0.2 4 0",
            "SAMPLE int_loop interpreter 4 6 0.1 8 0",
            "BATCH int_loop interpreter 2","PROCESS_PEAK_WORKING_SET 4096"))

    def test_batch_normalization(self):
        result=parse_native(self.native_output(),"int_loop","interpreter",4)
        self.assertEqual(result["allocations_per_call"],4)
        self.assertEqual(result["execution_ms"],0.1)

    def test_corrupt_protocol_is_rejected(self):
        text=self.native_output()
        corrupted=(text.replace("PROFILE Release","PROFILE Debug"),
            text+"\nPROFILE Release",text.replace("int_loop interpreter 4 6 0.1","int_loop aot 4 6 0.1"),
            text.replace("SAMPLE int_loop interpreter 4 6","SAMPLE int_loop interpreter 4 7"),
            text.replace("6 0.1 8","6 nan 8"),text.replace("6 0.1 8","6 -1 8"),
            text.replace("interpreter 2","interpreter 0"),
            text.replace("SAMPLE int_loop interpreter 4 6 0.1 8 0",""))
        for output in corrupted:
            with self.subTest(output=output), self.assertRaises(ValueError):
                parse_native(output,"int_loop","interpreter",4)

    def test_resume_rejects_changed_corpus_before_reading_samples(self):
        with tempfile.TemporaryDirectory() as temporary:
            suite=BenchmarkSuite(Path("unused.exe"),Path(sys.executable),Path(temporary),1,1)
            suite.write_environment({"executable_sha256":"old", "corpus_sha256":"old"})
            with self.assertRaisesRegex(RuntimeError,"resume metadata mismatch"):
                suite.restore({"executable_sha256":"new", "corpus_sha256":"old"})

    def test_metadata_contract_keys_are_not_duplicated_after_resume(self):
        with tempfile.TemporaryDirectory() as temporary:
            suite=BenchmarkSuite(Path("unused.exe"),Path(sys.executable),Path(temporary),1,1)
            suite.write_environment({"type":"arp.script.benchmark-result/v1",
                "contractVersion":1,"executable_sha256":"original"})
            keys=[line.split(":",1)[0] for line in (Path(temporary)/"environment.yaml").read_text().splitlines()]
            self.assertEqual(keys.count("type"),1)
            self.assertEqual(keys.count("contractVersion"),1)


if __name__=="__main__":
    unittest.main()
