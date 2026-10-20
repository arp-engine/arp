"""Hostile report tests only; these synthetic reports prove no GPU execution."""
import copy
import unittest
from bindless_webgpu_rhi_smoke import validate_finite_graphics_root_report
from browser_contract_runner import BrowserProgressJournal

class GraphicsRootEvidenceTests(unittest.TestCase):
    def report(self,whole=False,buffer="uniform",dynamic=False,terminal="abandon"):
        count=1
        report={"success":True,"scenario":"original-general-finite-graphics-content-root",
            "bufferType":buffer,"rangeKind":"whole" if whole else "dynamic" if dynamic else "static",
            "rollbackKind":terminal,"rootCount":count,"rootRetirementCount":count,"pendingCount":count,
            "initializedSourcesRetired":True,"usageDependencyRetired":True,"nativeErrors":[],"diagnostics":[],
            "progressTransport":{"complete":True},"progress":[
                "BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root",
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-never-completed no-root-draw-output"]}
        for field in ("nativeDrawReadbackValidated","finiteGraphicsValidated","fullFiniteExecutionValidated",
                "finiteLifecycle4Validated","publicBindlessExecutionAvailable","nativeSyncPromoted","platformValidated","productionReady"):
            report[field]=False
        for _ in range(count):
            report["progress"].extend([
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_OK completed1 attachment1 cells1 bind1 draw1 attachmentSteps2 commands42 projections108 finalCells5 stateSteps17 rootBytes16384 exact-less-exhausted same-original-Stream-Usage-Dependency pending-rollback content-unchanged hot0-query0 native-closed",
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_RETIRE_OK original-journal-rollback transaction-count0 quota0 no-native-draw-or-completion",
                "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_RETIRE_OK original-attachment-state-stream-quotas-zero native-closed",
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_PENDING_OK sampled-completed-stamp-current attachment-private-proposal original-journal no-native-completion",
                "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROLLBACK_OK " + terminal
                    + " stale-observations-refused original-Inputs-recaptured sampled-stamp-current attachment-uninitialized generation-one-exhausted"])
        report["progress"] = [{"message": message, "elapsedSeconds": 0.0}
            for message in report["progress"]]
        report["progressTransport"] = {"contractVersion": 1, "complete": True,
            "messages": len(report["progress"]), "codeUnits": sum(
                BrowserProgressJournal.code_units_of(item["message"]) for item in report["progress"])}
        return report
    def test_driver_journal_shape_and_transport_counts(self):
        report=self.report()
        self.assertTrue(validate_finite_graphics_root_report(report,"uniform",False))
        bad=copy.deepcopy(report)
        bad["progress"]=[item["message"] for item in bad["progress"]]
        self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False))
        for field in ("contractVersion","messages","codeUnits"):
            bad=copy.deepcopy(report);bad["progressTransport"][field]+=1
            self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False),field)
        bad=copy.deepcopy(report);bad["progress"][0]["message"]+="forged"
        self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False))
    def test_exact_types_and_ranges(self):
        for whole,dynamic in ((False,False),(False,True),(True,False)):
            for buffer in ("uniform","readonly-storage"):
                for terminal in ("abandon","drop"):
                    report=self.report(whole,buffer,dynamic,terminal)
                    self.assertTrue(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal))
                    self.assertFalse(validate_finite_graphics_root_report(report,buffer,not whole,dynamic,terminal))
                    self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,not dynamic,terminal))
                    self.assertFalse(validate_finite_graphics_root_report(report,"wrong",whole,dynamic,terminal))
                    self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,"wrong"))
    def test_every_required_field_is_required(self):
        report=self.report()
        for field in report:
            bad=copy.deepcopy(report);del bad[field]
            self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False),field)
    def test_promoted_or_coerced_gates_refuse(self):
        report=self.report()
        for field,value in report.items():
            if type(value) is bool:
                for mutation in (not value,0,1,"false",None):
                    bad=copy.deepcopy(report);bad[field]=mutation
                    self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False),field)
        for field in ("rootCount","rootRetirementCount","pendingCount"):
            for mutation in (True,False,0,3,"2",None):
                bad=copy.deepcopy(report);bad[field]=mutation
                self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False),field)
    def test_missing_duplicate_and_prior_stage_markers_refuse(self):
        report=self.report()
        for i in range(len(report["progress"])):
            bad=copy.deepcopy(report);del bad["progress"][i]
            self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False),i)
        bad=copy.deepcopy(report);bad["progress"].append(report["progress"][2])
        self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False))
        bad=copy.deepcopy(report);bad["progress"]=["BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_STREAM5_OK commands14"]
        self.assertFalse(validate_finite_graphics_root_report(bad,"uniform",False))
    def test_native_errors_and_incomplete_transport_refuse(self):
        for field,value in (("nativeErrors",["native validation error"]),("diagnostics",["quota failure"]),
                ("progressTransport",{"complete":1}),("progress",[None])):
            report=self.report();report[field]=value
            self.assertFalse(validate_finite_graphics_root_report(report,"uniform",False))

    def test_initialized_shader_buffer_requires_its_original_evidence(self):
        for buffer in ("uniform","readonly-storage"):
            for interval in ("static","dynamic","whole"):
                for terminal in ("abandon","drop"):
                    whole,dynamic=interval=="whole",interval=="dynamic"
                    report=self.report(whole,buffer,dynamic,terminal)
                    report.update(scenario="original-general-finite-graphics-shader-buffer",
                        shaderBufferInitialized=True,shaderBufferRetired=True)
                    markers=["BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK "+buffer+" "+interval+" original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw",
                        "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw",
                        "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZED_OK complete-backing Staging4 original-write scopes3 queue1 authentic-ready pending-refused execution-closed"]
                    report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers)
                    def seal(value):
                        value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                            BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
                    seal(report)
                    self.assertTrue(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True))
                    self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal))
                    for marker in markers:
                        bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
                        self.assertFalse(validate_finite_graphics_root_report(bad,buffer,whole,dynamic,terminal,True))
                    for field in ("shaderBufferInitialized","shaderBufferRetired"):
                        bad=copy.deepcopy(report);bad[field]=False
                        self.assertFalse(validate_finite_graphics_root_report(bad,buffer,whole,dynamic,terminal,True))

    def test_native_custody_requires_independent_evidence_and_keeps_execution_closed(self):
        report=self.report()
        report.update(scenario="original-general-finite-graphics-native-custody",
            shaderBufferInitialized=True,shaderBufferRetired=True,nativeCustodyValidated=True)
        markers=[
            "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK uniform static original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw",
            "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw",
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZED_OK complete-backing Staging4 original-write scopes3 queue1 authentic-ready pending-refused execution-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK sources10 resources8 pipeline1 descriptor1 custody2 complete-backing528 closureBytes512 retainedBytes4096 owners94 exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETIRE_OK same-original-Prepared-root initializer-caller-drop native-owners-retained closure-quota-baseline no-draw-or-completion"]
        report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers)
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        def validate(value):
            return validate_finite_graphics_root_report(value,"uniform",False,False,"abandon",True,True)
        seal(report);self.assertTrue(validate(report))
        self.assertFalse(validate_finite_graphics_root_report(report,"uniform",False,False,"abandon",True))
        for marker in markers:
            bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
            self.assertFalse(validate(bad))
        for field in ("nativeDrawReadbackValidated","finiteGraphicsValidated","finiteLifecycle4Validated",
                "publicBindlessExecutionAvailable","nativeSyncPromoted","platformValidated","productionReady"):
            bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad),field)
        for value in (False,0,1,None,"true"):
            bad=copy.deepcopy(report);bad["nativeCustodyValidated"]=value;self.assertFalse(validate(bad))
        for replacement in ("BINDLESS_RHI_GENERAL_CLOSURE9_OK sources8 resources6",
                markers[3].replace("sources10","sources8"),markers[3].replace("complete-backing528","complete-backing0")):
            bad=copy.deepcopy(report);bad["progress"][-2]["message"]=replacement;seal(bad);self.assertFalse(validate(bad))
        bad=copy.deepcopy(report);bad["progress"].append(bad["progress"][-2]);seal(bad);self.assertFalse(validate(bad))

    def frozen_report(self,whole=False,buffer="uniform",dynamic=False,terminal="abandon"):
        report=self.report(whole,buffer,dynamic,terminal)
        report.update(scenario="original-general-finite-graphics-dependency-freeze",
            shaderBufferInitialized=True,shaderBufferRetired=True,nativeCustodyValidated=True,
            nativeDependencyFreezeValidated=True)
        markers=[
            "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK "+buffer+" "+("whole" if whole else "dynamic" if dynamic else "static")+" original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw",
            "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw",
            "BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZED_OK complete-backing Staging4 original-write scopes3 queue1 authentic-ready pending-refused execution-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK sources10 resources8 pipeline1 descriptor1 custody2 complete-backing528 closureBytes512 retainedBytes4096 owners94 exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETAIN_OK same-original-Prepared-root initializer-caller-drop Freeze8-retained no-draw-or-completion",
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETIRE_OK same-original-Pending-root Freeze8-child-drop closure-quota-baseline no-draw-or-completion",
            "BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_OK Closure10 ContentPlan6 commands14 dependencies6 initial2 final4 endpoints14 cells7 passes24 visits168 pairs21 freezeBytes512 owners95 exact-less-exhausted hostile-clauses-refused same-Prepared-root hot0-query0 payload-native-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_RETIRE_OK same-original-Pending-root stale-candidates-refused caller-drop quota-baseline no-draw-or-completion"]
        report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers)
        return report,markers

    def test_freeze8_requires_same_root_retention_and_terminal_refusal_without_promotion(self):
        report,markers=self.frozen_report()
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        def validate(value):
            return validate_finite_graphics_root_report(value,"uniform",False,False,"abandon",True,True,True)
        seal(report);self.assertTrue(validate(report))
        self.assertFalse(validate_finite_graphics_root_report(report,"uniform",False,False,"abandon",True,True))
        for marker in markers:
            bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
            self.assertFalse(validate(bad))
            bad=copy.deepcopy(report);bad["progress"].append({"message":marker,"elapsedSeconds":0.0});seal(bad)
            self.assertFalse(validate(bad))
        for original,replacement in [("Closure10","Closure9"),("dependencies6","dependencies4"),
                ("initial2","initial1"),("final4","final3"),("freezeBytes512","freezeBytes0")]:
            bad=copy.deepcopy(report);bad["progress"][-2]["message"]=markers[-2].replace(original,replacement);seal(bad)
            self.assertFalse(validate(bad))
        for field in ("nativeDependencyFreezeValidated","nativeCustodyValidated"):
            for value in (False,0,1,"true",None):
                bad=copy.deepcopy(report);bad[field]=value;self.assertFalse(validate(bad))
        for field in ("nativeDrawReadbackValidated","publicBindlessExecutionAvailable","nativeSyncPromoted"):
            bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad))

    def test_payload8_requires_native_inputs_retention_and_refusal_for_each_selected_profile(self):
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        for buffer in ("uniform","readonly-storage"):
          for interval in ("static","dynamic","whole"):
            for terminal in ("abandon","drop"):
                whole,dynamic=interval=="whole",interval=="dynamic"
                report,_=self.frozen_report(whole,buffer,dynamic,terminal)
                report.update(scenario="original-general-finite-graphics-native-payload",nativePayloadInputsValidated=True)
                markers=[
                    "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK Freeze8 Closure10 ContentPlan6 commands14 calls9 render1 compute0 pipeline1 group1 draw1 dispatch0 dependencies6 implicit8 viewport1 scissor1 attachment1 dynamicOffsets"+("0" if whole else "1")+" owners96 exact-less-exhausted original-pass-group-attachment-mutated same-Prepared-root hot0-query0 cold-record-execution-closed",
                    "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETAIN_OK same-original-Prepared-root freeze-closure-initializer-caller-drop original-native-inputs-retained no-record-or-completion",
                    "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETIRE_OK same-original-Pending-root stale-inputs-refused payload-child-drop no-record-or-completion",
                    "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_REJECT_OK graphics-Pending-root genuine-frozen-native-input original-root-stale counts-cleared hot0 execution-closed",
                    "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-graphics10 commands14 calls9 render1 compute0 draws1 dispatches0 dependencies6 colors1 bytes512 original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed"]
                report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers);seal(report)
                def validate(value):
                    return validate_finite_graphics_root_report(value,buffer,whole,dynamic,terminal,True,True,True,True)
                self.assertTrue(validate(report),(buffer,interval,terminal))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True))
                for marker in markers:
                    bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
                    self.assertFalse(validate(bad))
                    bad=copy.deepcopy(report);bad["progress"].append({"message":marker,"elapsedSeconds":0.0});seal(bad)
                    self.assertFalse(validate(bad))
                for before,after in (("calls9","calls8"),("Closure10","Closure9"),("attachment1","attachment0"),
                        ("dynamicOffsets0","dynamicOffsets1") if whole else ("dynamicOffsets1","dynamicOffsets0")):
                    bad=copy.deepcopy(report);bad["progress"][-5]["message"]=markers[0].replace(before,after);seal(bad)
                    self.assertFalse(validate(bad))
                for value in (False,0,1,None,"true"):
                    bad=copy.deepcopy(report);bad["nativePayloadInputsValidated"]=value;self.assertFalse(validate(bad))
                for field in ("nativeDrawReadbackValidated","finiteGraphicsValidated","finiteLifecycle4Validated",
                        "publicBindlessExecutionAvailable","nativeSyncPromoted","platformValidated","productionReady"):
                    bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad),field)

    def cold_report(self,whole=False,buffer="uniform",dynamic=False,terminal="abandon"):
        report,_=self.frozen_report(whole,buffer,dynamic,terminal)
        report["progress"]=[i for i in report["progress"] if not any(tag in i["message"] for tag in
            ("CLOSURE10_RETAIN_OK","CLOSURE10_RETIRE_OK","FREEZE8_RETIRE_OK"))]
        report.update(scenario="original-general-finite-graphics-native-cold",nativePayloadInputsValidated=True,
            nativeColdAdmissionValidated=True,coldCandidateIndependentOfCpuBaseline=True)
        markers=[
            "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK Freeze8 Closure10 ContentPlan6 commands14 calls9 render1 compute0 pipeline1 group1 draw1 dispatch0 dependencies6 implicit8 viewport1 scissor1 attachment1 dynamicOffsets"+("0" if whole else "1")+" owners96 exact-less-exhausted original-pass-group-attachment-mutated same-Prepared-root hot0-query0 cold-record-execution-closed",
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-graphics10 commands14 calls9 render1 compute0 draws1 dispatches0 dependencies6 colors1 bytes512 original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_OK Payload8 Lifecycle5 ContentPlan6 commands14 calls9 render1 compute0 callbacks8 owners101 coldBytes512 exact-less-exhausted forged-reservation-native-mutated legacy-refused same-Prepared-root sealed-CPU-refused hot0-query0 no-encoder-or-execution",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETAIN_OK same-original-Prepared-root payload-freeze-closure-initializer-seal-reservation-caller-drop sealed-CPU-refused no-record-or-completion",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETIRE_OK same-original-Abandoned-root original-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion",
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_REJECT_OK graphics-Cold-abandoned-root genuine-frozen-native-input original-root-stale counts-cleared hot0 execution-closed",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_CANDIDATE_RETIRE_OK before-any-proposal original-factory transaction0 quota0 generation-unchanged CPU-baseline-separate"]
        report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers)
        return report,markers

    def test_cold8_requires_sealed_candidate_retention_and_retirement_before_cpu_baseline(self):
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        for buffer in ("uniform","readonly-storage"):
          for interval in ("static","dynamic","whole"):
            for terminal in ("abandon","drop"):
                whole,dynamic=interval=="whole",interval=="dynamic"
                report,markers=self.cold_report(whole,buffer,dynamic,terminal);seal(report)
                def validate(value):
                    return validate_finite_graphics_root_report(value,buffer,whole,dynamic,terminal,True,True,True,True,True)
                self.assertTrue(validate(report),(buffer,interval,terminal))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True,True))
                for marker in markers:
                    bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
                    self.assertFalse(validate(bad),marker)
                    bad=copy.deepcopy(report);bad["progress"].append({"message":marker,"elapsedSeconds":0.0});seal(bad)
                    self.assertFalse(validate(bad),marker)
                for field in ("nativeColdAdmissionValidated","coldCandidateIndependentOfCpuBaseline"):
                    for value in (False,0,1,"true",None):
                        bad=copy.deepcopy(report);bad[field]=value;self.assertFalse(validate(bad),field)
                for before,after in (("Lifecycle5","Lifecycle4"),("ContentPlan6","ContentPlan5"),
                        ("callbacks8","callbacks7"),("coldBytes512","coldBytes0")):
                    bad=copy.deepcopy(report);bad["progress"][-5]["message"]=markers[2].replace(before,after);seal(bad)
                    self.assertFalse(validate(bad),before)
                for field in ("nativeDrawReadbackValidated","publicBindlessExecutionAvailable","nativeSyncPromoted","productionReady"):
                    bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad),field)

    def command_report(self,whole=False,buffer="uniform",dynamic=False,terminal="abandon"):
        report,cold_markers=self.cold_report(whole,buffer,dynamic,terminal)
        report.update(scenario="original-general-finite-graphics-native-command",nativeCommandConnectionValidated=True)
        markers=[
            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_OK Cold8 Payload8 Lifecycle5 ContentPlan6 commands14 calls9 command1 owners104 owner-add3 legacy-refused short-exhausted-recover duplicate-peer-forged-native-mutated-refused sealed-CPU-refused hot0-query0 no-encoder-callback-or-execution",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETAIN_OK same-original-Prepared-root original-Cold8-caller-drop payload-freeze-closure-initializer-seal-reservation-retained sealed-CPU-refused no-encoder-or-completion",
            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETIRE_OK same-original-Abandoned-root original-shell-Cold8-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion"]
        report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers)
        report["progressTransport"].update(messages=len(report["progress"]),codeUnits=sum(
            BrowserProgressJournal.code_units_of(i["message"]) for i in report["progress"]))
        return report,markers,cold_markers

    def test_command8_requires_exact_cold_custody_and_keeps_successor_authority_closed(self):
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        for buffer in ("uniform","readonly-storage"):
          for interval in ("static","dynamic","whole"):
            for terminal in ("abandon","drop"):
                whole,dynamic=interval=="whole",interval=="dynamic"
                report,markers,cold_markers=self.command_report(whole,buffer,dynamic,terminal)
                def validate(value):
                    return validate_finite_graphics_root_report(value,buffer,whole,dynamic,terminal,True,True,True,True,True,True)
                self.assertTrue(validate(report),(buffer,interval,terminal))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True,True,True))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True,True,False,True))
                for marker in markers+cold_markers:
                    bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
                    self.assertFalse(validate(bad),marker)
                    bad=copy.deepcopy(report);bad["progress"].append({"message":marker,"elapsedSeconds":0.0});seal(bad)
                    self.assertFalse(validate(bad),marker)
                for before,after in (("COMMAND8","COMMAND7"),("Cold8","Cold7"),("Lifecycle5","Lifecycle4"),
                        ("ContentPlan6","ContentPlan5"),("command1","command2"),("owner-add3","owner-add4"),
                        ("owners104","owners0"),("no-encoder-callback-or-execution","execution-proven")):
                    bad=copy.deepcopy(report);bad["progress"][-3]["message"]=markers[0].replace(before,after);seal(bad)
                    self.assertFalse(validate(bad),before)
                for field in ("nativeCommandConnectionValidated","nativeColdAdmissionValidated","coldCandidateIndependentOfCpuBaseline"):
                    for value in (False,0,1,"true",None):
                        bad=copy.deepcopy(report);bad[field]=value;self.assertFalse(validate(bad),field)
                    bad=copy.deepcopy(report);del bad[field];self.assertFalse(validate(bad),field)
                for field in ("nativeDrawReadbackValidated","finiteGraphicsValidated","finiteLifecycle4Validated",
                        "fullFiniteExecutionValidated","publicBindlessExecutionAvailable","nativeSyncPromoted","platformValidated","productionReady"):
                    bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad),field)

    def test_callback8_requires_original_observer_retention_and_closed_successors(self):
        def seal(value):
            value["progressTransport"].update(messages=len(value["progress"]),codeUnits=sum(
                BrowserProgressJournal.code_units_of(i["message"]) for i in value["progress"]))
        for buffer in ("uniform","readonly-storage"):
          for interval in ("static","dynamic","whole"):
            for terminal in ("abandon","drop"):
                whole,dynamic=interval=="whole",interval=="dynamic"
                report,command_markers,cold_markers=self.command_report(whole,buffer,dynamic,terminal)
                report.update(scenario="original-general-finite-graphics-native-callback",nativeCallbackObserverValidated=True)
                markers=[
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_OK Command8 Cold8 Lifecycle5 ContentPlan6 callbacks8 native7 local1 shared1 owners115 owner-add11 legacy-short-enlarged-exhausted-forged-native-mutated-refused original-subscription-no-new-registration hot0-query0 no-issuer-record-execution",
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETAIN_OK same-original-Prepared-root Command8-Cold8-caller-drop original-observer-seal-reservation-initializers-retained pending1 armed1 no-native-execution",
                    "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETIRE_OK same-original-Abandoned-root original-observer-suppressed sticky-claim reservations0 callbacks0 quota-baseline no-completion-or-public-execution"]
                report["progress"].extend({"message":m,"elapsedSeconds":0.0} for m in markers);seal(report)
                def validate(value):
                    return validate_finite_graphics_root_report(value,buffer,whole,dynamic,terminal,True,True,True,True,True,True,True)
                self.assertTrue(validate(report),(buffer,interval,terminal))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True,True,True,True))
                self.assertFalse(validate_finite_graphics_root_report(report,buffer,whole,dynamic,terminal,True,True,True,True,True,False,True))
                for marker in markers+command_markers+cold_markers:
                    bad=copy.deepcopy(report);bad["progress"]=[i for i in bad["progress"] if i["message"]!=marker];seal(bad)
                    self.assertFalse(validate(bad),marker)
                    bad=copy.deepcopy(report);bad["progress"].append({"message":marker,"elapsedSeconds":0.0});seal(bad)
                    self.assertFalse(validate(bad),marker)
                for before,after in (("CALLBACK8","CALLBACK7"),("Command8","Command7"),("Lifecycle5","Lifecycle4"),
                        ("ContentPlan6","ContentPlan5"),("callbacks8","callbacks7"),("shared1","shared2"),
                        ("owner-add11","owner-add12"),("owners115","owners0"),("no-issuer-record-execution","execution-proven")):
                    bad=copy.deepcopy(report);bad["progress"][-3]["message"]=markers[0].replace(before,after);seal(bad)
                    self.assertFalse(validate(bad),before)
                for value in (False,0,1,"true",None):
                    bad=copy.deepcopy(report);bad["nativeCallbackObserverValidated"]=value;self.assertFalse(validate(bad))
                bad=copy.deepcopy(report);del bad["nativeCallbackObserverValidated"];self.assertFalse(validate(bad))
                for field in ("nativeDrawReadbackValidated","finiteGraphicsValidated","fullFiniteExecutionValidated",
                        "finiteLifecycle4Validated","publicBindlessExecutionAvailable","nativeSyncPromoted","platformValidated","productionReady"):
                    bad=copy.deepcopy(report);bad[field]=True;self.assertFalse(validate(bad),field)

if __name__=="__main__":
    unittest.main()
