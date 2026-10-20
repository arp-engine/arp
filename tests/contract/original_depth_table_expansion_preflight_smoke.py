"""Actual created-device query smoke; no pipeline/command or visual readiness claim."""
import argparse
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--runner", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, required=True)
    parser.add_argument("--geometry-only", action="store_true")
    parser.add_argument("--completed-depth-only", action="store_true")
    parser.add_argument("--mixed-content-plan", action="store_true")
    parser.add_argument("--mixed-execution", action="store_true")
    parser.add_argument("--mixed-fault", action="store_true")
    mixed_terminals={"caller-drop":1,"retired-lifetime":2,"recorded-cancel":3,"retire-before-submit":4,
        "recording-error":5,"reversed-receipts":6,"submission-error":7,"throw-before":8,"throw-after":9,"pending-cancel":10}
    parser.add_argument("--mixed-terminal",choices=mixed_terminals)
    terminal_scenarios = {"recording-error":3,"submission-error":4,"pending-cancel":6,"caller-drop":7,
                          "throw-after":8,"throw-before":9,"retired-lifetime":10}
    parser.add_argument("--completed-depth-terminal", choices=terminal_scenarios)
    native_mode = parser.add_mutually_exclusive_group()
    native_mode.add_argument("--native-emission", action="store_true")
    native_mode.add_argument("--native-creation", action="store_true")
    native_mode.add_argument("--native-registration", action="store_true")
    native_mode.add_argument("--native-publication", action="store_true")
    native_mode.add_argument("--native-execution", action="store_true")
    native_mode.add_argument("--native-issuers", action="store_true")
    native_mode.add_argument("--native-recording", action="store_true")
    native_mode.add_argument("--native-results", action="store_true")
    native_mode.add_argument("--native-completion", action="store_true")
    native_mode.add_argument("--native-submitted-loss", action="store_true")
    native_mode.add_argument("--native-map-loss", action="store_true")
    native_mode.add_argument("--native-recording-cancel", action="store_true")
    native_mode.add_argument("--native-recording-uncertain", action="store_true")
    native_mode.add_argument("--native-submission-uncertain", action="store_true")
    native_mode.add_argument("--native-map-uncertain", action="store_true")
    native_mode.add_argument("--native-recording-scope-uncertain", action="store_true")
    native_mode.add_argument("--native-submission-scope-uncertain", action="store_true")
    native_mode.add_argument("--native-queue-registration-uncertain", action="store_true")
    native_mode.add_argument("--native-scope-registration-uncertain", action="store_true")

    args = parser.parse_args()
    if args.mixed_content_plan and (not args.completed_depth_only or args.completed_depth_terminal):
        parser.error("Mixed content planning requires the normal completed depth producer")
    if args.mixed_execution and (not args.completed_depth_only or args.completed_depth_terminal or args.mixed_content_plan):
        parser.error("Mixed execution requires its independent completed-depth invocation")
    if args.mixed_fault and not args.mixed_execution:
        parser.error("Mixed fault requires --mixed-execution")
    if args.mixed_terminal and not args.mixed_execution:
        parser.error("Mixed terminal requires --mixed-execution")
    mixed_terminal=mixed_terminals.get(args.mixed_terminal,0)
    mixed = args.mixed_content_plan or args.mixed_execution
    if args.completed_depth_terminal and not args.completed_depth_only:
        parser.error("Depth terminal lifecycle requires --completed-depth-only")
    depth_terminal = terminal_scenarios.get(args.completed_depth_terminal)
    if args.completed_depth_only and (args.geometry_only or any(
            value for name, value in vars(args).items() if name.startswith("native_"))):
        parser.error("Completed depth publication smoke is an independent affected-scope run")
    if args.geometry_only and (args.native_registration or args.native_publication or args.native_execution
            or args.native_issuers or args.native_recording or args.native_results
            ):
        parser.error("GraphicsGeometry supports creation or explicit composed completion/lifecycle; other modes remain closed")
    invocation_cases = ("recording-uncertain", "submission-uncertain", "map-uncertain",
                        "recording-scope-uncertain", "submission-scope-uncertain",
                        "queue-registration-uncertain", "scope-registration-uncertain")
    invocation_mode = next((case for case in invocation_cases
                            if getattr(args, "native_" + case.replace("-", "_"))), None)
    invocation_uncertain = invocation_mode is not None
    recording_uncertain = args.native_recording_uncertain or args.native_recording_scope_uncertain
    scope_uncertain = args.native_recording_scope_uncertain or args.native_submission_scope_uncertain
    if (args.native_recording_cancel or invocation_uncertain) and not args.geometry_only:
        parser.error("Recording cancellation/invocation uncertainty requires the explicit GraphicsGeometry composition")
    args.native_completion = args.native_completion or args.native_submitted_loss or args.native_map_loss or args.native_recording_cancel or invocation_uncertain
    args.native_issuers = args.native_issuers or args.native_recording or args.native_results or args.native_completion
    args.native_execution = args.native_execution or args.native_issuers
    args.native_publication = args.native_publication or args.native_execution
    native_requested = args.completed_depth_only or args.native_emission or args.native_creation or args.native_registration or args.native_publication
    creation_requested = args.native_creation or args.native_registration or args.native_publication
    directory = args.runner.resolve()
    for profile in (("Graphics",) if args.completed_depth_only else ("GraphicsGeometry",) if args.geometry_only else ("Compute", "Graphics")):
        pipeline_profile = "Graphics" if profile == "GraphicsGeometry" else profile
        entry = ("original-depth-table-creation-" if args.native_creation else "original-depth-table-emission-" if args.native_emission else "original-depth-table-preflight-") + profile.lower() + ".html"
        if args.completed_depth_only:
            # Distinct invocation modes must not overwrite another live server's
            # entry in the shared runner directory.
            entry = entry.removesuffix(".html") + "-completed-depth-" + ("mixed-execution-fault" if args.mixed_fault else "mixed-execution" if args.mixed_execution else "mixed-content-plan" if args.mixed_content_plan else args.completed_depth_terminal or "normal") + ".html"
            if args.mixed_terminal:entry=entry.removesuffix('.html')+'-'+args.mixed_terminal+'.html'
        html = """<!doctype html><meta charset="utf-8"><canvas id="canvas" width="2" height="2"></canvas><script>
const result={profile:PROFILE,messages:[],errors:[],nativeScopeErrors:[],nativeCalls:{},runtimeProbeCalls:{},nativeLayouts:[],nativeVertexLayouts:[],mixedDispatches:[],queryOnly:QUERY_ONLY};
const mixedPipelines=new WeakMap();let mixedPipelineCount=0,mixedActive=false;
let sent=false;
function finish(code){
  if(sent)return;sent=true;result.exitCode=code;
  result.asyncifyContinuation=window.Module?.tableAsyncifyContinuation;
  result.success=code===0 && result.errors.length===0 &&
    REQUIRED_MESSAGES &&
    NATIVE_EXPECTATION;
  fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(result)});
}
STORAGE_FORMAT_PROBE
addEventListener('error',e=>{result.errors.push(String(e.error?.stack||e.message));finish(1);});
addEventListener('unhandledrejection',e=>{result.errors.push(String(e.reason?.stack||e.reason));finish(1);});
(async()=>{
  if(!navigator.gpu)throw new Error('RHI-ORIGINAL-TABLE-PREFLIGHT-DEVICE');
  const adapter=await navigator.gpu.requestAdapter();
  if(!adapter)throw new Error('RHI-ORIGINAL-TABLE-PREFLIGHT-DEVICE');
  const device=await adapter.requestDevice();
  // Use the shared bootstrap query on this exact device, before workload
  // instrumentation. Unsupported format capability remains false.
  const rgba16FloatStorage=await arpProbeWebGpuStorageFormat(device,'rgba16float');
  result.storageFormatProbes={rgba16float:rgba16FloatStorage};
  result.nativeCalls.copyTextureToBuffer=0;
  result.nativeCalls.mapAsync=0;result.nativeCalls.getMappedRange=0;result.nativeCalls.unmap=0;
  result.nativeMapReceipts=[];
  result.nativeScopeReceipts=[];result.nativeQueueReceipts=[];
  const observeReceipt=(returned,receipts)=>{
    const receipt={issuedAt:performance.now(),settledAt:null,outcome:'pending'};
    receipts.push(receipt);
    returned.then(outcome=>{receipt.settledAt=performance.now();receipt.outcome=outcome?String(outcome.message||outcome):'success';},
      error=>{receipt.settledAt=performance.now();receipt.outcome=String(error);});
  };
  const queueDone=device.queue.onSubmittedWorkDone;
  device.queue.onSubmittedWorkDone=function(...args){
    const returned=queueDone.apply(this,args);observeReceipt(returned,result.nativeQueueReceipts);return returned;};
  for(const name of ['createShaderModule','createBindGroupLayout','createPipelineLayout','createComputePipeline',
      'createRenderPipeline','createBindGroup','createBuffer','createTexture','createCommandEncoder','pushErrorScope','popErrorScope']){
    result.nativeCalls[name]=0;result.runtimeProbeCalls[name]=0;const method=device[name];
    device[name]=function(...args){result.nativeCalls[name]++;
      if(name==='createRenderPipeline' && !Module.tableRegistrationRuntimeProbe)
        result.nativeVertexLayouts.push(args[0].vertex.buffers?.map(b=>({arrayStride:b.arrayStride,stepMode:b.stepMode,
          attributes:b.attributes.map(a=>({shaderLocation:a.shaderLocation,offset:a.offset,format:a.format}))})) ?? null);
      if(window.Module && Module.tableRegistrationRuntimeProbe)result.runtimeProbeCalls[name]++;
      if(!QUERY_ONLY && name==='createBindGroupLayout' && !Module.tableEmissionFailure && !Module.tableRegistrationRuntimeProbe)
        result.nativeLayouts.push(args[0].entries.map(e=>({binding:e.binding,visibility:e.visibility,
          texture:e.texture ? {sampleType:e.texture.sampleType,viewDimension:e.texture.viewDimension,multisampled:e.texture.multisampled} : null,
          sampler:e.sampler ? {type:e.sampler.type} : null,
          storageTexture:e.storageTexture ? {access:e.storageTexture.access,format:e.storageTexture.format,viewDimension:e.storageTexture.viewDimension} : null,
          buffer:e.buffer ? {type:e.buffer.type,hasDynamicOffset:e.buffer.hasDynamicOffset,minBindingSize:e.buffer.minBindingSize} : null,
          bindingArraySize:e.bindingArraySize||0})));
      if(!QUERY_ONLY && window.Module && Module.tableEmissionFailure){
        const failure=Module.tableEmissionFailure;
        if(failure===1 && name==='createShaderModule')args[0]={...args[0],code:'invalid selected shader source'};
        if(failure===2 && name==='createBindGroupLayout')args[0]={...args[0],entries:[...args[0].entries,...args[0].entries]};
        if(failure===3 && name==='createComputePipeline')args[0]={...args[0],compute:{...args[0].compute,entryPoint:'absent'}};
        if(failure===3 && name==='createRenderPipeline')args[0]={...args[0],fragment:{...args[0].fragment,entryPoint:'absent'}};
      }
      if(!QUERY_ONLY && window.Module && Module.tablePublicationFailure && name==='createBindGroup')
        args[0]={...args[0],entries:[...args[0].entries,...args[0].entries]};
      const value=method.apply(this,args);
      if(name==='createComputePipeline')mixedPipelines.set(value,++mixedPipelineCount);
      if(name==='popErrorScope')value.then(error=>{if(error)result.nativeScopeErrors.push(String(error.message));},
        error=>result.nativeScopeErrors.push(String(error)));
      if(name==='popErrorScope')observeReceipt(value,result.nativeScopeReceipts);
      if(name==='createBuffer'){
        for(const operation of ['mapAsync','getMappedRange','unmap']){
          const original=value[operation];
          value[operation]=function(...operationArgs){
            result.nativeCalls[operation]++;
            const returned=original.apply(this,operationArgs);
            if(operation==='mapAsync'){
              const receipt={issuedAt:performance.now(),settledAt:null,outcome:'pending'};
              result.nativeMapReceipts.push(receipt);
              returned.then(()=>{receipt.settledAt=performance.now();receipt.outcome='success';},
                error=>{receipt.settledAt=performance.now();receipt.outcome=String(error);});
            }
            return returned;};
        }
      }
      if(name==='createCommandEncoder'){
        const copy=value.copyTextureToBuffer;
        value.copyTextureToBuffer=function(...copyArgs){result.nativeCalls.copyTextureToBuffer++;return copy.apply(this,copyArgs);};
        const begin=value.beginComputePass;
        value.beginComputePass=function(...passArgs){
          const pass=begin.apply(this,passArgs),set=pass.setPipeline,dispatch=pass.dispatchWorkgroups;
          let pipeline=0;pass.setPipeline=function(p){pipeline=mixedPipelines.get(p)||0;return set.call(this,p);};
          pass.dispatchWorkgroups=function(x,y=1,z=1){
            if(mixedActive)result.mixedDispatches.push({pipeline,groups:[x,y,z]});
            return dispatch.call(this,x,y,z);};
          return pass;};
      }
      return value;};
  }
  result.nativeCalls.queueSubmit=0;const submit=device.queue.submit;
  device.queue.submit=function(...args){result.nativeCalls.queueSubmit++;return submit.apply(this,args);};
  device.addEventListener('uncapturederror',e=>result.errors.push(String(e.error.message)));
  result.deviceLimits={maxBindGroups:device.limits.maxBindGroups,maxBindingsPerBindGroup:device.limits.maxBindingsPerBindGroup,
    maxVertexBuffers:device.limits.maxVertexBuffers,maxVertexAttributes:device.limits.maxVertexAttributes,
    maxVertexBufferArrayStride:device.limits.maxVertexBufferArrayStride,
    maxSampledTexturesPerShaderStage:device.limits.maxSampledTexturesPerShaderStage,
    maxStorageBuffersPerShaderStage:device.limits.maxStorageBuffersPerShaderStage,
    maxComputeWorkgroupSizeX:device.limits.maxComputeWorkgroupSizeX,maxComputeWorkgroupSizeY:device.limits.maxComputeWorkgroupSizeY,
    maxComputeWorkgroupSizeZ:device.limits.maxComputeWorkgroupSizeZ,maxComputeInvocationsPerWorkgroup:device.limits.maxComputeInvocationsPerWorkgroup};
  const foreignAdapter=QUERY_ONLY ? null : await navigator.gpu.requestAdapter();
  const foreignDevice=foreignAdapter ? await foreignAdapter.requestDevice() : null;
  if(!QUERY_ONLY && !foreignDevice)throw new Error('TEST-TABLE-EMISSION-FOREIGN-DEVICE');
  window.Module={preinitializedWebGPUDevice:device,tableEmissionForeignDevice:foreignDevice,
    tableBrowserCompletionDeadline:performance.now()+BROWSER_TIMEOUT_MILLISECONDS,
    webGpuRgba16FloatStorage:rgba16FloatStorage,
    preRun:[()=>{
      if(typeof Asyncify==='undefined' || typeof HEAPU32==='undefined')return;
      const observation={capacityBytes:Asyncify.StackSize,peakBytes:0,unwinds:0};
      Module.tableAsyncifyContinuation=observation;
      const stop=Asyncify.maybeStopUnwind;
      Asyncify.maybeStopUnwind=function(){
        if(this.currData && this.state===this.State.Unwinding && this.exportCallStack.length===0){
          observation.peakBytes=Math.max(observation.peakBytes,HEAPU32[this.currData>>2]-this.currData-12);
          observation.unwinds++;
        }
        return stop.call(this);
      };
    }],
    arguments:['/original-depth-table-expansion/'+PROFILE.toLowerCase()+'/original-depth-table.expansion.yaml',PROFILE,'valid','RUN_MODE'DEPTH_SCENARIO_ARG],
    print:m=>{
      const first=result.messages.length;result.messages.push(String(m));
      if(String(m).startsWith('BINDLESS_WEBGPU_PUBLIC_MIXED_EXECUTION_BEGIN ')){
        mixedActive=true;result.mixedCallsBefore={...result.nativeCalls};}
      if(String(m).startsWith('BINDLESS_WEBGPU_PUBLIC_MIXED_EXECUTION12 ')){
        mixedActive=false;result.mixedCallsAfter={...result.nativeCalls};}
      // Reuse ProgressBatch/1. Synchronous test-only delivery preserves exact
      // order even during a long synchronous Wasm validation; no GPU authority.
      const progress=new XMLHttpRequest();progress.open('POST','/progress',false);
      progress.setRequestHeader('Content-Type','application/json');
      progress.send(JSON.stringify({contractVersion:1,first,messages:[String(m)]}));
      if(progress.status!==200)throw new Error('BROWSER-PROGRESS-DELIVERY');
      if(!QUERY_ONLY && (String(m).startsWith('ORIGINAL_TABLE_EXPANSION_ARTIFACT_OK ') ||
          String(m).startsWith('COMPLETED_DEPTH_ONLY_OK ') ||
          String(m).startsWith('COMPLETED_DEPTH_MIXED_TERMINAL_OK ') ||
          String(m).startsWith('COMPLETED_DEPTH_MIXED_EXECUTION_OK ')))setTimeout(()=>finish(0),0);
    },
    printErr:m=>result.errors.push(String(m)),onExit:finish,postRun:()=>{if(QUERY_ONLY)finish(0);},
    onAbort:m=>{
      // Reuse the full browser fixture's Asyncify ABI observation. This is
      // test-runtime continuation storage, never a GPU ownership budget.
      if(typeof Asyncify!=='undefined' && Asyncify.currData && typeof HEAPU32!=='undefined'){
        const base=Asyncify.currData;
        result.asyncifyStack={usedBytes:HEAPU32[base>>2]-base-12,
          capacityBytes:HEAPU32[(base+4)>>2]-base-12};
      }
      result.errors.push(String(m));result.abortStack=m?.stack||new Error(String(m)).stack;finish(1);}};
  const script=document.createElement('script');script.src='arp_original_depth_table_expansion_artifact_tests.js';document.head.append(script);
})().catch(e=>{result.errors.push(String(e.stack||e));finish(1);});
</script>""".replace("PIPELINE_PROFILE", "'" + pipeline_profile + "'").replace("PROFILE", "'" + profile + "'")
        required_messages = (
            "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_EXPANSION_PREFLIGHT_OK '+" + repr(pipeline_profile) + ") && m.includes('native-query1')) && "
            "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_EXPANSION_DEFINITION_OK '+" + repr(pipeline_profile) + ") && m.includes('native-query1')) && "
            "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_EXPANSION_ARTIFACT_OK '+" + repr(profile) + "))")
        if args.completed_depth_only:
            required_messages = (
                "result.messages.includes('COMPLETED_DEPTH_ONLY_OK genuine-publication-update-retirement public-depth-compute-execution') && "
                "result.messages.filter(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK ')).length===5 && "
                "['scenario1 fault0','scenario2 fault0','scenario0 fault0','scenario0 fault1','scenario5 fault0'].every(c=>"
                "result.messages.some(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK '+c+' '))) && "
                "result.messages.filter(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_NUMERIC_OK ')).length===3 && "
                "result.messages.includes('COMPLETED_DEPTH_PUBLIC_DESCRIPTOR3_OK registry9 original-clear2-mips genuine-completion retained-proof no-shader-execution') && "
                "result.messages.includes('COMPLETED_DEPTH_PUBLIC_UPDATE3_OK same-original-cells genuine-second-completion mixed-mip-generations untouched-mip-preserved stale-binding-refused atomic-republication old-proof-retired') && "
                "result.messages.includes('COMPLETED_DEPTH_SNAPSHOT4_OK authentic-package active1-physical2 same-depth-proof sealed stale-refused mixed-generation-replacement capacity-atomic internal-original-leases authorization-refused retired') && "
                "result.messages.includes('COMPLETED_DEPTH_READONLY_CELLS_OK same-Plan2-journal state-claim-exclusive content-unenlisted pending-proof-live rollback-preserved genuine-transition-completion mixed-generations-unchanged no-shader-execution') && "
                "result.messages.includes('COMPLETED_DEPTH_READ_INPUT1_OK canonical-view all-visible-cells mixed-generations pending-original-native bounded-cell-texel-owner hot0 input-only state-and-execution-closed') && "
                "result.messages.includes('COMPLETED_DEPTH_ROOT5_OK authentic-authorization pipeline physical-group canonical-root closure7 retained-input mixed-generations stale-refused bounded hot0 retired no-shader-state-or-execution') && "
                "result.messages.filter(m=>m==='COMPLETED_DEPTH_ROOT_READ_PLAN11_OK same-inventory journal ShaderRead Compute exact-cell-proof mixed-generations budgets hot0 cancel-preserved no-native-shader-execution').length===2 && "
                "result.messages.filter(m=>m.startsWith('COMPLETED_DEPTH_SOURCE1_OK ') && m.includes('cells2 ')).length===1")
        if depth_terminal is not None:
            required_messages = (
                "result.messages.includes('COMPLETED_DEPTH_ONLY_OK genuine-publication-update-retirement public-depth-compute-execution') && "
                "result.messages.filter(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK ')).length===1 && "
                f"result.messages.some(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK scenario{depth_terminal} fault0 ')) && "
                "!result.messages.some(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_NUMERIC_OK ')) && "
                "result.messages.filter(m=>m.startsWith('COMPLETED_DEPTH_ROOT_READ_PLAN11_OK ')).length===2")
        if args.mixed_content_plan:
            required_messages += (" && result.storageFormatProbes.rgba16float===true && result.messages.filter(m=>m===\"BINDLESS_WEBGPU_MIXED_CONTENT16_PLAN_OK controls2 cells5 steps12 commands33 entries2 exact-less quota-rollback depth-read-only hot0 no-execution\").length===1")
            required_messages += (" && result.messages.filter(m=>m===\"BINDLESS_WEBGPU_MIXED_CONTENT16_PLAN_OK controls2 cells5 steps12 commands34 entries3 exact-less quota-rollback depth-read-only hot0 no-execution\").length===1")
            required_messages += (" && result.messages.filter(m=>m==='BINDLESS_MIXED_IMAGE_SNAPSHOT9_OK shared-depth-storage exact-views no-authorization retirement0').length===1")
            required_messages += (" && result.messages.filter(m=>m==='BINDLESS_MIXED_IMAGE_PREPARATION10_OK backend=3 shared-authorization entries3 authentic-masks physical-bindings pool0 record-refused').length===1")
            required_messages += (" && result.messages.filter(m=>m==='BINDLESS_MIXED_ROOT_CLOSURE13_OK backend=3 shared-roots3 original-proofs exact-less quota0 execution-closed').length===1")
            required_messages += (" && result.messages.filter(m=>m==='BINDLESS_MIXED_ENTRY_FLOW1_OK backend=3 authenticated-order3 exact-less forged-refused execution-closed').length===1")
        if args.mixed_execution:
            fault = int(args.mixed_fault)
            completed=mixed_terminal in (0,6)
            retired_message='retired-device proof-refused' if mixed_terminal in (2,4) else 'same-depth-proof contents-preserved'
            end_message=(f"COMPLETED_DEPTH_MIXED_TERMINAL_OK terminal={mixed_terminal} retirement0" if args.mixed_terminal
                else 'COMPLETED_DEPTH_MIXED_EXECUTION_OK genuine-copy-reduce-read retirement0')
            required_messages = (
                "result.storageFormatProbes.rgba16float===true && "
                f"result.messages.includes('{end_message}') && "
                f"result.messages.includes('COMPLETED_DEPTH_MIXED_EXECUTION_RETIRED {retired_message} pool0') && "
                f"result.messages.filter(m=>m.startsWith('BINDLESS_WEBGPU_PUBLIC_MIXED_NUMERIC ')).length==={int(completed)} && "
                f"result.messages.filter(m=>m==='BINDLESS_WEBGPU_PUBLIC_MIXED_EXECUTION12 terminal={mixed_terminal} fault={fault} clear-store2 dispatch3 same-journal Content16 Completion12 hot-submit0 hot-completion0 retirement0').length===1 && "
                "result.messages.filter(m=>m.startsWith('BINDLESS_MIXED_NATIVE_OWNER_UNION3_OK ')).length===1 && "
                "!result.messages.some(m=>m.startsWith('BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK '))")
        html = html.replace("BROWSER_TIMEOUT_MILLISECONDS", str(args.timeout_seconds*1000))
        html = html.replace("STORAGE_FORMAT_PROBE", (ROOT / "tests/fixtures/webgpu_storage_format_probe.js").read_text(encoding="utf-8"))
        html = html.replace("REQUIRED_MESSAGES", required_messages)
        scenario_arg = "," + repr("fault") if args.mixed_fault else "" if depth_terminal is None else "," + repr(str(depth_terminal))
        if args.mixed_terminal:scenario_arg=','+repr('fault' if args.mixed_fault else 'clean')+','+repr(str(mixed_terminal))
        html = html.replace("DEPTH_SCENARIO_ARG", scenario_arg)
        html = html.replace("QUERY_ONLY", "false" if native_requested else "true").replace("RUN_MODE", "mixed-depth-storage-execution" if args.mixed_execution else "mixed-depth-storage-plan" if args.mixed_content_plan else "completed-depth-publication-only" if args.completed_depth_only else "native-" + invocation_mode if invocation_uncertain else "native-recording-cancel" if args.native_recording_cancel else "native-map-loss" if args.native_map_loss else "native-submitted-loss" if args.native_submitted_loss else "native-completion" if args.native_completion else "native-results" if args.native_results else "native-recording" if args.native_recording else "native-issuers" if args.native_issuers else "native-execution" if args.native_execution else "native-publication" if args.native_publication else "native-registration" if args.native_registration else "native-creation" if args.native_creation else "native-emission" if args.native_emission else "native-query")
        native_expectation = "Object.values(result.nativeCalls).every(n=>n===0)"
        if native_requested:
            native_expectation = "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_EXPANSION_EMISSION_OK ' + '" + pipeline_profile + "')) && result.nativeCalls.createShaderModule>0 && result.nativeCalls.createBindGroupLayout>0 && result.nativeCalls.createPipelineLayout>0 && result.nativeCalls." + ("createComputePipeline" if profile == "Compute" else "createRenderPipeline") + ">0 && result.nativeCalls.pushErrorScope===result.nativeCalls.popErrorScope && result.nativeCalls.queueSubmit===0 && result.nativeCalls.createCommandEncoder===0 && result.nativeCalls.createBindGroup===0 && result.nativeCalls.createTexture===0"
        if creation_requested:
            native_expectation = native_expectation.replace("ORIGINAL_TABLE_EXPANSION_EMISSION_OK ", "ORIGINAL_TABLE_EXPANSION_CREATION_ACTOR_OK ")
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_EXPANSION_ACCEPTED_WRAPPER_OK '))"
        if args.geometry_only:
            native_expectation += " && result.deviceLimits.maxVertexBuffers>=2 && result.deviceLimits.maxVertexAttributes>=3 && result.deviceLimits.maxVertexBufferArrayStride>=24"
            if native_requested:
                native_expectation += " && result.nativeVertexLayouts.length>0 && result.nativeVertexLayouts.every(v=>JSON.stringify(v)===JSON.stringify([{arrayStride:8,stepMode:'vertex',attributes:[{shaderLocation:0,offset:0,format:'float32x2'}]},{arrayStride:24,stepMode:'instance',attributes:[{shaderLocation:1,offset:0,format:'float32x2'},{shaderLocation:2,offset:8,format:'float32x4'}]}]))"
        if (args.native_registration or args.native_publication) and not args.geometry_only:
            # A registered facade uses the existing real runtime capability
            # probes. Their resource creation is explicit and separately counted;
            # the selected table path must still create no group/resource/command.
            facade_count = (5 if args.native_execution and not args.native_issuers else 3) if args.native_publication and profile == "Graphics" else 2
            native_expectation = native_expectation.replace(
                "result.nativeCalls.createBindGroup===0 && result.nativeCalls.createTexture===0",
                f"result.runtimeProbeCalls.createBindGroup==={2 * facade_count} && result.runtimeProbeCalls.createTexture==={facade_count} && "
                f"result.runtimeProbeCalls.createBuffer==={facade_count} && "
                "result.nativeCalls.createBindGroup===result.runtimeProbeCalls.createBindGroup && "
                "result.nativeCalls.createTexture===result.runtimeProbeCalls.createTexture && "
                "result.nativeCalls.createBuffer===result.runtimeProbeCalls.createBuffer")
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_REGISTRATION_NATIVE_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_REGISTRATION_PHYSICAL_LOSS_OK '))"
        if args.native_publication and not args.geometry_only:
            native_expectation = native_expectation.replace(
                "result.nativeCalls.createBindGroup===result.runtimeProbeCalls.createBindGroup",
                "result.nativeCalls.createBindGroup>result.runtimeProbeCalls.createBindGroup").replace(
                "result.nativeCalls.createTexture===result.runtimeProbeCalls.createTexture",
                "result.nativeCalls.createTexture>result.runtimeProbeCalls.createTexture")
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_PUBLICATION_NATIVE_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_PUBLICATION_PHYSICAL_LOSS_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_PUBLICATION_SOURCE2_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_PUBLICATION_CPU_BUDGET_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_PUBLICATION_CANONICAL_OWNERS_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_POSITIONAL_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_CONSUMPTION_OK '))"
            if profile == "Graphics" and not args.native_issuers:
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_CONTENT_PLAN2_OK Graphics'))"
                if not args.native_execution:
                    native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_CONTENT_PLAN2_LOSS_OK Graphics'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_CUSTODY_OK Graphics'))"
                if not args.native_execution:
                    native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_CUSTODY_LOSS_OK Graphics'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_PAYLOAD2_OK Graphics'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_COLD2_OK Graphics'))"
                if not args.native_execution:
                    native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_COLD2_LOSS_OK Graphics'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_RENDER_BINDINGS_OK '))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_STATE_CAPTURE_OK ') && m.includes('bindings2 ranges10'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_STATE_CAPTURE_OK ') && m.includes('bindings1 ranges7'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_SEQUENCE_OK ') && m.includes('commands11 cells14 steps14 implicit9'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_STREAM_OK ') && m.includes('commands11 descriptors8 references8 binds2 draw1 implicit9'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_USAGE_DEPENDENCY_OK ') && m.includes('sampled9 attachment1 cells10 dependencies4 pairs45 visits190'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_TRANSACTION_OK ') && m.includes('controls4 finals4 steps14 transitions4'))"
        if args.native_execution and profile == "Graphics":
            if not args.native_issuers:
                native_expectation = native_expectation.replace("result.nativeCalls.queueSubmit===0", "result.nativeCalls.queueSubmit===1").replace(
                    "result.nativeCalls.createCommandEncoder===0", "result.nativeCalls.createCommandEncoder===1")
            native_expectation = native_expectation.replace(
                "result.nativeCalls.createBuffer===result.runtimeProbeCalls.createBuffer",
                "result.nativeCalls.createBuffer===result.runtimeProbeCalls.createBuffer+" + ("1" if args.native_issuers else "2"))
            if not args.native_issuers:
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_NATIVE_EXECUTION2_OK Graphics'))"
                native_expectation += " && result.messages.filter(m=>m.startsWith('COMPLETED_DEPTH_SOURCE1_OK ')).length===4"
                native_expectation += " && result.messages.includes('COMPLETED_DEPTH_PUBLIC_DESCRIPTOR3_OK registry9 original-clear2-mips genuine-completion retained-proof no-shader-execution')"
                native_expectation += " && result.messages.includes('COMPLETED_DEPTH_PUBLIC_UPDATE3_OK same-original-cells genuine-second-completion mixed-mip-generations untouched-mip-preserved stale-binding-refused atomic-republication old-proof-retired')"
                native_expectation += " && result.messages.includes('COMPLETED_DEPTH_READONLY_CELLS_OK same-Plan2-journal state-claim-exclusive content-unenlisted pending-proof-live rollback-preserved genuine-transition-completion mixed-generations-unchanged no-shader-execution')"
                native_expectation += " && result.messages.includes('COMPLETED_DEPTH_READ_INPUT1_OK canonical-view all-visible-cells mixed-generations pending-original-native bounded-cell-texel-owner hot0 input-only state-and-execution-closed')"
                native_expectation = native_expectation.replace("result.nativeCalls.queueSubmit===1", "result.nativeCalls.queueSubmit===4").replace(
                    "result.nativeCalls.createCommandEncoder===1", "result.nativeCalls.createCommandEncoder===4")
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_OUTPUT_READBACK_OWNER2_OK Graphics'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_RETAINED_READBACK_CORRESPONDENCE1_OK '))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_MIXED_READBACK_RESULT2_OK '))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_CONTENT3_OK Graphics'))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_JOIN2_OK Graphics'))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_USAGE3_OK Graphics'))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_CUSTODY4_OK Graphics'))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_PAYLOAD3_OK Graphics'))"
            native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_COLD3_OK Graphics'))"
            if args.native_issuers and not args.native_results and not args.native_completion:
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_ISSUER3_OK Graphics') && m.includes('physical-registration0') && m.includes('execution1-2-4-rejected') && m.includes('submission3-closed'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('BINDLESS_RHI_TYPED_ISSUER_NATIVE_OK ') && m.includes('partial-failure3-cleared-retry'))"
            if args.native_results:
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_STORAGE2_OK Graphics') && m.includes('callbacks12 base8-map4') && m.includes('map-publication-completion-closed'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_TYPED_MAP_CUSTODY1_OK Graphics') && m.includes('map1-scope3 slots12') && m.includes('cancel-release hot0 query0') && m.includes('map-publication-completion-closed'))"
                native_expectation += " && result.nativeCalls.copyTextureToBuffer===0 && result.nativeCalls.queueSubmit===0 && result.nativeCalls.createCommandEncoder===0"
            if args.native_recording:
                native_expectation = native_expectation.replace("result.nativeCalls.createCommandEncoder===0", "result.nativeCalls.createCommandEncoder===1")
                native_expectation += " && result.nativeCalls.copyTextureToBuffer===1 && result.nativeCalls.queueSubmit===0"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_RECORDING3_OK Graphics') && m.includes('copy1 real-scopes3') && m.includes('encoder1 submit0'))"
                native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_RECAPTURE3_OK Graphics') && m.includes('old-State-Stream-refused same-Inputs-resources-budgets') && m.includes('publication-loss-probes-retained'))"
            if args.native_completion:
                native_expectation = native_expectation.replace("result.nativeCalls.createCommandEncoder===0", "result.nativeCalls.createCommandEncoder===1").replace("result.nativeCalls.queueSubmit===0", "result.nativeCalls.queueSubmit===1")
                native_expectation += " && result.nativeCalls.copyTextureToBuffer===1"
                if args.native_submitted_loss or args.native_map_loss:
                    loss_kind = "MAP" if args.native_map_loss else "SUBMITTED"
                    native_expectation += f" && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_{loss_kind}_LOSS1_OK Graphics') && m.includes('genuine-device-loss invalidated-before-drain foreign-callback-retention authentic-drain publication0 completion0') && m.includes('caller-drop result-submission-callback-quotas-zero hot-release0'))"
                    native_expectation += " && result.nativeCalls.getMappedRange===0"
                    native_expectation += " && result.nativeCalls.mapAsync===" + ("1 && result.nativeCalls.unmap>0" if args.native_map_loss else "0")
                    if args.native_map_loss:
                        native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_LOSS1_ARMED Graphics') && m.includes('actual-map1-C-callback-held map-scopes3-held'))"
                        native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_MAP_LOSS1_PARTIAL_DRAIN_OK') && m.includes('scope1-held same-root-retained unpublished'))"
                else:
                    native_expectation += " && result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_READBACK_COMPLETION1_OK Graphics') && m.includes('numeric4x2-rgb-quarter-half-threequarters-quarter-alpha1') && m.includes('same-journal-commit independent-publication1') && m.includes('native-unmap1') && m.includes('caller-drop retained-release hot0 query0'))"
                    native_expectation += " && result.nativeCalls.mapAsync===1 && result.nativeCalls.getMappedRange===1 && result.nativeCalls.unmap===1"
        if args.geometry_only and args.native_completion:
            lifecycle = "recording-cancel" if args.native_recording_cancel else "map-loss" if args.native_map_loss else "submission-loss" if args.native_submitted_loss else None
            execution_expectation = (
                f"result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_LIFECYCLE_OK {lifecycle} same-original31') && m.includes('unpublished completion0 result-quota-zero')) && "
                f"result.messages.some(m=>m.startsWith('BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK {lifecycle} same-original31') && m.includes('scopes2-held1') && m.includes('caller-drop') && m.includes('{'Abandoned-journal-empty' if args.native_recording_cancel else 'actual-device-loss'}')) && "
                "!result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_EXECUTION5_OK ')) && "
                f"result.nativeCalls.queueSubmit==={0 if args.native_recording_cancel else 1} && "
                f"result.nativeCalls.mapAsync==={1 if args.native_map_loss else 0} && "
                f"result.nativeCalls.getMappedRange===0 && result.nativeCalls.unmap==={1 if args.native_map_loss else 0} && " +
                ("result.messages.some(m=>m.includes('invalidated-before-drain journal-empty') && m.includes('same-original31')) && " if not args.native_recording_cancel else "")
            ) if lifecycle else (
                "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_EXECUTION5_OK ') && m.includes('numeric-readback hot0-query0')) && "
                "result.nativeCalls.queueSubmit===1 && result.nativeCalls.mapAsync===1 && result.nativeCalls.getMappedRange===1 && result.nativeCalls.unmap===1 && ")
            if invocation_uncertain:
                lifecycle = invocation_mode
                execution_expectation = (
                    f"result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_LIFECYCLE_OK {lifecycle} same-original31') && m.includes('unpublished completion0 result-quota-zero')) && "
                    f"result.messages.some(m=>m.startsWith('BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK {lifecycle} same-original31') && m.includes('no-retry') && m.includes('actual-device-loss') && m.includes('invalidated-before-drain journal-empty') && m.includes('caller-drop')) && "
                    "!result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_EXECUTION5_OK ')) && "
                    f"result.nativeCalls.queueSubmit==={0 if recording_uncertain or args.native_submission_scope_uncertain else 1} && "
                    f"result.nativeCalls.mapAsync==={1 if args.native_map_uncertain else 0} && "
                    f"result.nativeCalls.getMappedRange===0 && result.nativeCalls.unmap==={1 if args.native_map_uncertain else 0} && "
                )
                if args.native_recording_uncertain:
                    execution_expectation += "result.messages.some(m=>m.includes('genuine-encoder-then-throw scopes-released-still-retained')) && "
                if scope_uncertain:
                    execution_expectation += "result.messages.some(m=>m.includes('actual-push2-then-throw no-guessed-pop') && m.includes('loss-only-retirement')) && "
                if args.native_queue_registration_uncertain or args.native_scope_registration_uncertain:
                    retained_marker = "scopes-released-no-queue-proof-retained" if args.native_queue_registration_uncertain else "authentic-scope-queue-promises-held-through-loss"
                    execution_expectation += (
                        "result.messages.some(m=>m.includes('authentic-listener-then-throw') && "
                        f"m.includes('{retained_marker}') && m.includes('foreign-callback-drain')) && ")
            native_expectation = (
                "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_CUSTODY_OK ')) && " +
                execution_expectation +
                "result.messages.some(m=>m.startsWith('ORIGINAL_TABLE_GEOMETRY_RETIRE_OK ')) && "
                f"result.nativeCalls.createCommandEncoder==={0 if args.native_recording_scope_uncertain else 1} && result.nativeCalls.copyTextureToBuffer==={0 if recording_uncertain else 1} && "
                f"result.nativeCalls.pushErrorScope===result.nativeCalls.popErrorScope+{2 if scope_uncertain else 0} && "
                "result.deviceLimits.maxVertexBuffers>=2 && result.deviceLimits.maxVertexAttributes>=3 && "
                "result.deviceLimits.maxVertexBufferArrayStride>=24 && result.nativeVertexLayouts.length>0 && "
                "result.nativeVertexLayouts.every(v=>JSON.stringify(v)===JSON.stringify([{arrayStride:8,stepMode:'vertex',attributes:[{shaderLocation:0,offset:0,format:'float32x2'}]},{arrayStride:24,stepMode:'instance',attributes:[{shaderLocation:1,offset:0,format:'float32x2'},{shaderLocation:2,offset:8,format:'float32x4'}]}]))")
        if args.completed_depth_only:
            native_expectation = (
                "result.nativeScopeErrors.length===0 && "
                "result.nativeCalls.createCommandEncoder===10 && result.nativeCalls.queueSubmit===9 && "
                "result.nativeCalls.copyTextureToBuffer===0 && result.nativeCalls.mapAsync===6 && "
                "result.nativeCalls.pushErrorScope>0 && result.nativeCalls.pushErrorScope===result.nativeCalls.popErrorScope && "
                "result.runtimeProbeCalls.createTexture===1 && result.runtimeProbeCalls.createBindGroup===2 && "
                "result.runtimeProbeCalls.createBuffer===1 && result.nativeCalls.createBuffer===36 && "
                "result.nativeCalls.createTexture===2 && result.nativeCalls.createBindGroup===9 && "
                "result.nativeCalls.createShaderModule===result.runtimeProbeCalls.createShaderModule+7 && "
                "result.nativeCalls.createComputePipeline===result.runtimeProbeCalls.createComputePipeline+7 && "
                "result.nativeCalls.createRenderPipeline===0 && result.nativeLayouts.length===7")
            if args.mixed_content_plan:
                # Planning adds five buffers; each of Preparation9/10 adds four.
                # Three negative ordinary-record probes create encoders and their
                # two buffer rings each, but never submit or map mixed work.
                native_expectation = native_expectation.replace("createBuffer===36", "createBuffer===55")
                native_expectation = native_expectation.replace("createCommandEncoder===10", "createCommandEncoder===13")
                native_expectation = native_expectation.replace("createTexture===2", "createTexture===3")
                native_expectation = native_expectation.replace("createBindGroup===9", "createBindGroup===14")
                native_expectation = native_expectation.replace("createShaderModule+7", "createShaderModule+12")
                native_expectation = native_expectation.replace("createComputePipeline+7", "createComputePipeline+12")
                native_expectation = native_expectation.replace("nativeLayouts.length===7", "nativeLayouts.length===12")
            if depth_terminal is not None:
                submits = 3 if depth_terminal in (3,9) else 5 if depth_terminal == 4 else 4
                scope_errors = 1 if depth_terminal in (3,4) else 0
                # Recording/submission failures may stop before map admission;
                # the original observer still requires real callback drain and
                # unpublished output with all lifetime/capacity quotas returned.
                native_expectation = (
                    f"result.nativeScopeErrors.length==={scope_errors} && "
                    f"result.nativeCalls.createCommandEncoder===4 && result.nativeCalls.queueSubmit==={submits} && "
                    "result.nativeCalls.copyTextureToBuffer===0 && result.nativeCalls.mapAsync<=1 && "
                    "result.nativeCalls.pushErrorScope===result.nativeCalls.popErrorScope && "
                    "result.nativeCalls.createBuffer===18 && result.nativeCalls.createTexture===2 && "
                    "result.nativeCalls.createBindGroup===5 && result.nativeLayouts.length===3 && "
                    "result.nativeCalls.createShaderModule===result.runtimeProbeCalls.createShaderModule+3 && "
                    "result.nativeCalls.createComputePipeline===result.runtimeProbeCalls.createComputePipeline+3 && "
                    "result.nativeCalls.createRenderPipeline===0")
        if args.mixed_execution:
            completed=mixed_terminal in (0,6)
            # NativeError deliberately invokes the real queue twice with one
            # genuine command buffer to obtain a native validation receipt.
            submits=3 if completed else 0 if mixed_terminal in (3,4,5,8) else 2 if mixed_terminal==7 else 1
            maps=3 if completed else 0
            native_expectation = (
                f"result.nativeScopeErrors.length==={int(mixed_terminal in (5,7))} && result.nativeCalls.pushErrorScope>0 && "
                "result.nativeCalls.pushErrorScope===result.nativeCalls.popErrorScope && "
                "result.mixedCallsBefore && result.mixedCallsAfter && "
                f"result.mixedCallsAfter.createCommandEncoder-result.mixedCallsBefore.createCommandEncoder==={3 if completed else 1} && "
                f"result.mixedCallsAfter.queueSubmit-result.mixedCallsBefore.queueSubmit==={submits} && "
                "['mapAsync','getMappedRange','unmap'].every(k=>"
                f"result.mixedCallsAfter[k]-result.mixedCallsBefore[k]==={maps}) && "
                f"result.mixedCallsAfter.copyTextureToBuffer-result.mixedCallsBefore.copyTextureToBuffer==={2 if completed else 0} && "
                "result.mixedCallsAfter.createComputePipeline===result.mixedCallsBefore.createComputePipeline && "
                "result.mixedDispatches.length===3 && new Set(result.mixedDispatches.map(d=>d.pipeline)).size===3 && "
                "result.mixedDispatches.every(d=>d.pipeline>0) && "
                "JSON.stringify(result.mixedDispatches.map(d=>d.groups))==="
                f"JSON.stringify([[1,1,1],[1,1,1],[{3 if args.mixed_fault else 2},1,1]])")
        html = html.replace("NATIVE_EXPECTATION", native_expectation)
        (directory / entry).write_text(html, encoding="utf-8")
        started = time.perf_counter()
        result = BrowserContractRunner(args.browser, directory, entry,
            args.output / profile.lower(), args.timeout_seconds, capture_progress=True).run()
        elapsed = time.perf_counter() - started
        if native_requested and result.get("success"):
            continuation = result["asyncifyContinuation"]
            assert continuation["unwinds"] > 0
            assert 0 < continuation["peakBytes"] <= continuation["capacityBytes"]
            layouts = result.get("nativeLayouts", [])
            expected_stage = 4 if profile == "Compute" else 2
            mixed_entries = 0
            for entries in layouts:
                slots = [e["binding"] for e in entries]
                if mixed and (slots == [2,3,6,7] or slots == [3,4,5,6,7]
                        or (slots == [0,1,6,7] and entries[0].get("texture"))):
                    mixed_entries += 1
                    assert all(e["visibility"] == 4 and e["bindingArraySize"] == 0 for e in entries)
                    for entry in entries:
                        binding = entry["binding"]
                        if binding in (0,3,4):
                            assert entry["texture"] == {"sampleType":"depth" if binding == 0 else "float",
                                "viewDimension":"2d","multisampled":False}
                        elif binding in (1,2):
                            assert entry["storageTexture"] == {"access":"write-only","format":"rgba16float","viewDimension":"2d"}
                        else:
                            assert entry["buffer"] == {"type":"read-only-storage" if binding == 6 else "storage",
                                "hasDynamicOffset":False,"minBindingSize":0}
                elif mixed and entries[0].get("storageTexture"):
                    # Authenticated storeMain omits inactive Sources/Results;
                    # readMain omits inactive Pyramids. Retain both exact layouts.
                    assert [e["binding"] for e in entries] == [0, 1, 6, 7]
                    assert all(e["visibility"] == 4 and e["bindingArraySize"] == 0 for e in entries)
                    assert all(e["storageTexture"] == {"access":"write-only","format":"rgba16float","viewDimension":"2d"} for e in entries[:2])
                    assert [e["buffer"]["type"] for e in entries[2:]] == ["read-only-storage", "storage"]
                elif mixed and entries[0].get("texture", {}).get("sampleType") == "float":
                    assert [e["binding"] for e in entries] == [2, 3, 4, 5, 6, 7]
                    assert all(e["visibility"] == 4 and e["bindingArraySize"] == 0 for e in entries)
                    assert all(e["texture"] == {"sampleType":"float","viewDimension":"2d","multisampled":False} for e in entries[:2])
                    assert [e["buffer"]["type"] for e in entries[2:]] == ["storage", "storage", "read-only-storage", "storage"]
                elif args.completed_depth_only:
                    assert len(entries) == 8 and [e["binding"] for e in entries] == list(range(8))
                    assert all(e["visibility"] == 4 and e["bindingArraySize"] == 0 for e in entries)
                    assert all(e["texture"] == {"sampleType": "depth", "viewDimension": "2d", "multisampled": False}
                               for e in entries[:2])
                    assert all(e["sampler"] == {"type": "non-filtering"} for e in entries[2:4])
                    assert [e["buffer"]["type"] for e in entries[4:]] == ["storage", "storage", "read-only-storage", "storage"]
                elif len(entries) == 3:
                    assert [e["binding"] for e in entries] == [0, 1, 2]
                    assert all(e["visibility"] == expected_stage and e["bindingArraySize"] == 0 and
                        e["texture"] == {"sampleType": "depth", "viewDimension": "2d", "multisampled": False} for e in entries)
                else:
                    assert profile == "Compute" and len(entries) == 1 and entries[0]["binding"] == 0
                    assert entries[0]["buffer"] == {"type": "storage", "hasDynamicOffset": False, "minBindingSize": 0}
            assert args.completed_depth_only or any(len(entries) == 3 for entries in layouts)
            if mixed:
                assert mixed_entries == (6 if args.mixed_execution else 3)
                assert [len(e) for e in layouts].count(4) == (5 if args.mixed_execution else 3)
                assert [len(e) for e in layouts].count(5) == (2 if args.mixed_execution else 1)
                assert [len(e) for e in layouts].count(6) == 1
        if not result.get("success"):
            raise RuntimeError("ORIGINAL-TABLE-PREFLIGHT-BROWSER-FAILED: " + str(result.get("errors", [])) + "; report=" + str(args.output / profile.lower() / "report.json"))
        print(profile + (f" actual public mixed execution12 terminal{mixed_terminal} fault{int(args.mixed_fault)}, genuine callback drain and retirement; scene gate open=false" if args.mixed_execution else
            f" actual public depth execution5 terminal {args.completed_depth_terminal}, unpublished output, genuine callback drain and quotas retired; scene execution remains closed" if depth_terminal is not None else
            " mixed Content16 CPU planning, genuine completed D32 cells2, storage cells3, one canonical graph/journal, exact-less/quota/hot rollback; no mixed GPU execution" if args.mixed_content_plan else
            " actual public depth execution5, Closure8/Program3/Lifecycle13, same Content11 journal, Load/integer-mip/query and fault numeric readback; cold/recorded cancel and reversed genuine callbacks; quotas retired; scene execution remains closed" if args.completed_depth_only else
            " actual same-original map-pending physical loss, retained foreign callback, authentic drain and unpublished terminal output passed; encoder1/copy1/submit1; public closed" if args.native_map_loss and profile == "Graphics" else
            " actual same-original submitted physical loss, retained scopes, authentic drain and unpublished terminal output passed; encoder1/copy1/submit1; public closed" if args.native_submitted_loss and profile == "Graphics" else
            f" actual same-original31 {lifecycle}, unpublished output, authentic callback drain and quota retirement passed; public closed" if args.geometry_only and args.native_completion and lifecycle else
            " actual indexed vertex/instance geometry and table selection share the same original root, numeric RGBA16Float readback, real completion and quota retirement; public closed" if args.geometry_only and args.native_completion else
            " actual same-original table numeric readback, native map/unmap, sealed journal commit and independent snapshot publication passed; encoder1/copy1/submit1; public closed" if args.native_completion and profile == "Graphics" else
            " actual typed mixed Result storage and original map reservation passed; callbacks12, Result512, encoder0/submit0; mapping/publication/completion closed" if args.native_results and profile == "Graphics" else
            " actual same-root mixed table/render/readback recording and scope drain passed; encoder1/submit0; map-result-completion closed" if args.native_recording and profile == "Graphics" else
            " actual Callback3/Issuer3 preparation and cancellation on same mixed original root passed; encoder0/submit0; mixed output-copy/map/readback closed; Execution2 covered by native-execution smoke" if args.native_issuers and profile == "Graphics" else
            " actual Registry9 depth clear/update/public descriptor, read-only proof lifecycle and original table completion passed; encoder4/submit4; depth shader execution and output readback unproven" if args.native_execution and profile == "Graphics" else
            " actual original table publication, usage and physical loss passed; command/queue calls0" if args.native_publication else
            " actual original table registration and physical loss passed; command/queue calls0" if args.native_registration else
            " actual original actor and accepted wrapper passed; queue calls0" if args.native_creation else
            " actual native emission and scope receipts passed; queue calls0" if args.native_emission else
            " actual device query passed; native creation/queue calls0") + f"; elapsed={elapsed:.2f}s", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
