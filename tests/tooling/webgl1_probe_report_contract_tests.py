"""Schema-negative tests only; synthetic reports never prove native execution."""
import copy
import unittest

from webgl1_probe_report_contract import WebGlProbeReportContract as Contract


class ReportContractTests(unittest.TestCase):
    def report(self):
        result = dict(type="WebGl1RuntimeProbe", contractVersion=6, success=True,
                      version="WebGL 1.0 (test fixture)", shadingLanguage="WebGL GLSL ES 1.0 (test fixture)",
                      gpuCompletionClaimed=False, productionBackendValidated=False, issuedFrames=60,
                      uploadSourceMutationProbe=True, textureSubImageSourceMutationProbe=True,
                      bufferSubDataSourceMutationProbe=True, issuedObjectDeletionProbe=True,
                      precisionFactsValidated=True, formatFacts=dict(contractVersion=1, validated=True),
                      identityFacts=dict(contractVersion=1, validated=True, sourceAuthenticated=False, fields={}),
                      retirementFacts=dict(contractVersion=1))
        for field in Contract.IDENTITY_FIELDS:
            result["identityFacts"]["fields"][field] = dict(queried=True, value="")
        result["identityFacts"]["fields"]["contextVersion"]["value"] = result["version"]
        result["identityFacts"]["fields"]["shaderVersion"]["value"] = result["shadingLanguage"]
        result["retirementFacts"].update({field: True for field in Contract.RETIREMENT_FIELDS})
        return result

    def test_complete_report_is_immutable(self):
        result = self.report()
        before = copy.deepcopy(result)
        Contract.validate(result, 60)
        self.assertEqual(before, result)

    def test_every_retirement_fact_mandatory_and_boolean(self):
        for field in Contract.RETIREMENT_FIELDS:
            for value in (None, False, 1, "true", [], {}):
                with self.subTest(field=field, value=value):
                    result = self.report()
                    if value is None:
                        del result["retirementFacts"][field]
                    else:
                        result["retirementFacts"][field] = value
                    with self.assertRaisesRegex(ValueError, "RETIREMENT-" + field):
                        Contract.validate(result)

    def test_versions_and_readiness_rejected(self):
        for path in (("contractVersion",), ("retirementFacts", "contractVersion"),
                     ("identityFacts", "contractVersion"), ("formatFacts", "contractVersion")):
            for value in (None, False, True, 0, 7, "1"):
                result = self.report()
                owner = result if len(path) == 1 else result[path[0]]
                owner[path[-1]] = value
                with self.subTest(path=path, value=value), self.assertRaises(ValueError):
                    Contract.validate(result)
        for field in ("gpuCompletionClaimed", "productionBackendValidated"):
            result = self.report()
            result[field] = True
            with self.assertRaisesRegex(ValueError, "READINESS"):
                Contract.validate(result)
        result = self.report()
        result["identityFacts"]["sourceAuthenticated"] = True
        with self.assertRaisesRegex(ValueError, "IDENTITY"):
            Contract.validate(result)

    def test_missing_records_and_failed_probe(self):
        for field in self.report():
            result = self.report()
            del result[field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                Contract.validate(result)
        for value in (False, 1, "true"):
            result = self.report()
            result["success"] = value
            with self.assertRaises(ValueError):
                Contract.validate(result)

    def test_identity_and_frame_mismatch(self):
        for field in Contract.IDENTITY_FIELDS:
            for value in (None, {"queried": False, "value": ""}, {"queried": True, "value": "a\0b"}):
                result = self.report()
                result["identityFacts"]["fields"][field] = value
                with self.assertRaises(ValueError):
                    Contract.validate(result)
        for version in ("WebGL 2.0", "WebGL 1.00", "WebGL 1.0spoof", None):
            result = self.report()
            result["version"] = version
            with self.assertRaises(ValueError):
                Contract.validate(result)
        for count in (False, True, 0, -1, 2147483648, "60", 61):
            result = self.report()
            result["issuedFrames"] = count
            with self.assertRaises(ValueError):
                Contract.validate(result, 60)


if __name__ == "__main__":
    unittest.main()
