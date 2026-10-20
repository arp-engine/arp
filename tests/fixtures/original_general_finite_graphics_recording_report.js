/* Recording-only report validation; never an execution authority. */
(function(root) {
  'use strict';
  const prefixes=['BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK',
    'BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_OK','BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK',
    'BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK','BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_OK',
    'BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_OK'];
  const issuer='BINDLESS_RHI_GENERAL_GRAPHICS_ISSUER8_PREPARE_OK Callback8 Command8 Cold8 Lifecycle5 ContentPlan6 explicit-owner-add3 original-root-recapture exact-less-exhausted partial-rollback sticky-claim native-receipts7 no-record-submit-output';
  const rollback='BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_RECORDING_ROLLBACK_OK stale-observations-refused original-Inputs-recaptured generation-one-exhausted transaction0 quota0 no-submit-or-completion';
  const retire='BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_RETIRE_OK original-reservations0 quota-baseline no-output no-public-authority';
  const scenario='BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_SCENARIO_OK original-resource-backed-root no-submit-or-output public-closed';
  const projection='BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero no-submit-or-output';
  function inspect(progress,search,completed,diagnostics,nativeErrors) {
    const p=new URLSearchParams(search);
    const submission=p.has('general-graphics-native-submission');
    const physicalTerminal=p.get('general-graphics-physical-loss');
    const physical=physicalTerminal!==null;
    const physicalTerminals=['queue-uncertain','recording-shutdown','partial-push','recording-exception'];
    const readback=p.has('general-graphics-output-readback');
    const outputTerminal=p.get('general-graphics-output-terminal');
    const outputTerminals=['cancel','native-error','read-error','scope-native-error','throw-before','throw-after',
      'scope-registration','unmap-throw','pending-loss','reverse-scopes','published-loss','drop'];
    const outputPublishes=outputTerminal===null || ['reverse-scopes','published-loss'].includes(outputTerminal);
    const outputLost=['throw-before','throw-after','scope-registration','unmap-throw','pending-loss','published-loss'].includes(outputTerminal);
    const outputSuffix=readback?'readback3':'no-readback';
    const fault=p.has('general-graphics-submission-terminal');
    const submissionTerminal=p.get('general-graphics-submission-terminal');
    const kind=p.get('general-graphics-recording-terminal')||'cancel';
    const recordingLifetime=['retired','shutdown'].includes(kind);
    const allowedTerminals=['cancel','retired','shutdown','throw-before','throw-after','native-error','reverse-scopes','partial-scope'];
    const lifetimeTerminal=recordingLifetime?kind:submissionTerminal;
    const logicalLifetime=recordingLifetime || ['retired','shutdown'].includes(submissionTerminal);
    const failed=recordingLifetime || physical || (fault && submissionTerminal!=='reverse-scopes');
    const early=['cancel','throw-before','throw-after','partial-scope'].includes(submissionTerminal);
    const rollback=recordingLifetime?'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_RECORDING_LIFETIME_RETIRE_OK original-root-Abandoned transaction0 quota0 no-replay no-submit no-readback'
      :failed?'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SUBMISSION_FAILED_RETIRE_OK original-root-invalidated transaction0 quota0 no-replay no-readback'
      :submission?'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SUBMISSION_RETIRE_OK original-root-committed transaction0 quota0 no-replay '+outputSuffix
      :'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_RECORDING_ROLLBACK_OK stale-observations-refused original-Inputs-recaptured generation-one-exhausted transaction0 quota0 no-submit-or-completion';
    const retire=submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_RETIRE_OK original-reservations0 quota-baseline '+outputSuffix+' no-public-authority'
      :'BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_RETIRE_OK original-reservations0 quota-baseline no-output no-public-authority';
    const scenario=physical?'BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_SCENARIO_OK original-resource-backed-root actual-device-loss no-completion-or-output public-closed'
      :fault?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_SCENARIO_OK original-resource-backed-root genuine-terminal-drain no-readback public-closed'
      :submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_SCENARIO_OK original-resource-backed-root genuine-queue-completion '+outputSuffix+' public-closed'
      :'BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_SCENARIO_OK original-resource-backed-root no-submit-or-output public-closed';
    const projection=submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero '+outputSuffix
      :'BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING_PROJECTION_RETIRE_OK original-attachment graph-quotas-zero no-submit-or-output';
    const terminal=['cancel','drop','native-error','reverse-scopes','retired','shutdown'].indexOf(kind)+1;
    const whole=p.has('general-source-whole'),dynamic=p.has('general-content-plan-dynamic');
    const bufferType=p.get('general-content-buffer-type')||'uniform';
    const rangeKind=whole?'whole':dynamic?'dynamic':'static';
    const flags=['shader-content','native-custody','dependency-freeze','native-payload',
      'native-cold','native-command','native-callback','native-issuer','native-recording'];
    const held=physical?'BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_HELD_OK '+physicalTerminal+' pending-record1 original-loss-armed physical-loss-undelivered owners-retained no-completion no-output'
      :fault?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_HELD_OK '+submissionTerminal
      +' submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished '
      +(early?'root-Invalidated journal-empty':'root-Pending journal-retained')
      :submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_HELD_OK original-recorded-version3 queue-invocation1 scopes3 Pending caller-drop-retained unpublished'
      :'BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_HELD_OK terminal'+terminal+' actual-scopes3 Pending caller-drop-retained no-submit no-output';
    const drained=physical?'BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_TERMINAL_OK '+physicalTerminal+' actual-device-destroy original-loss-delivered genuine-local-drain root-Invalidated journal-empty no-completion unpublished no-retry public-closed'
      :fault?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_TERMINAL_OK '+submissionTerminal
      +' scopes-queue-drained loss-suppressed physical-loss-undelivered no-retry '
      +(failed?'root-Invalidated output-unpublished':'root-Committed attachment-completed no-readback')+' public-closed'
      :submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_COMPLETE_OK genuine-queue-scopes-drained loss-detached Committed journal-empty receipts-retired '+(readback?'readback3':'no-output-readback')+' public-closed'
      :'BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_TERMINAL_OK terminal'+terminal+' Abandoned journal-empty native-scopes-drained receipts-retired no-submit no-output public-closed';
    const one=[issuer,held,drained,retire,rollback,projection,scenario,
      physical?'BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_PREPARED_OK authentic-snapshot exact-attachment completion-refused'
        :'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_PENDING_OK sampled-completed-stamp-current attachment-private-proposal original-journal no-native-completion',
      'BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK '+bufferType+' '+rangeKind+' original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw',
      'BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw',
      failed?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_FAILED_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-invalidated no-readback'
        :outputTerminal!==null?'BINDLESS_RHI_GENERAL_GRAPHICS_READBACK_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero terminal-revalidated readback3'
        :submission?'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-completed '+outputSuffix
        :'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-never-completed no-root-draw-output',
      'BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_RETIRE_OK original-attachment-state-stream-quotas-zero native-closed',
      'BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root',
      'BINDLESS_RHI_GENERAL_GRAPHICS_SOURCE5_INPUT_RETIRE_OK original-module-child-custody artifact-input-quotas-zero '+(readback?'private-output3-readback':'draw-readback-unvalidated')];
    const receipts=physical?'BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_RECEIPTS_OK original-native-receipts7-retired no-output no-retry public-closed'
      :'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_RECEIPTS_OK original-native-receipts7-retired no-readback public-closed';
    if(fault || physical)one.push(receipts);
    const rejectedCandidate='BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_FAILED_CANDIDATE_RETIRE_OK original-snapshot-invalidated issuer-owner-released no-readback no-retry';
    if(failed)one.push(rejectedCandidate);
    const family=m=>m.split(' ',1)[0];
    const exactSingles=one.every(message=>progress.filter(m=>family(m)===family(message)).length===1
      && progress.includes(message));
    // Only the already-authored two preparation chains may precede Issuer8.
    const prepIndex=progress.indexOf(issuer);
    const patterns=[
      /^BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK sources10 resources8 pipeline1 descriptor1 custody2 complete-backing[1-9][0-9]* closureBytes[1-9][0-9]* retainedBytes[1-9][0-9]* owners[1-9][0-9]* exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed$/,
      /^BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_OK Closure10 ContentPlan6 commands14 dependencies6 initial2 final4 endpoints14 cells7 passes24 visits168 pairs21 freezeBytes[1-9][0-9]* owners[1-9][0-9]* exact-less-exhausted hostile-clauses-refused same-Prepared-root hot0-query0 payload-native-closed$/,
      new RegExp('^BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK Freeze8 Closure10 ContentPlan6 commands14 calls9 render1 compute0 pipeline1 group1 draw1 dispatch0 dependencies6 implicit8 viewport1 scissor1 attachment1 dynamicOffsets'+(whole?'0':'1')+' owners[1-9][0-9]* exact-less-exhausted original-pass-group-attachment-mutated same-Prepared-root hot0-query0 cold-record-execution-closed$'),
      /^BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-graphics10 commands14 calls9 render1 compute0 draws1 dispatches0 dependencies6 colors1 bytes[1-9][0-9]* original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed$/,
      /^BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_OK Payload8 Lifecycle5 ContentPlan6 commands14 calls9 render1 compute0 callbacks8 owners[1-9][0-9]* coldBytes[1-9][0-9]* exact-less-exhausted forged-reservation-native-mutated legacy-refused same-Prepared-root sealed-CPU-refused hot0-query0 no-encoder-or-execution$/,
      /^BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_OK Cold8 Payload8 Lifecycle5 ContentPlan6 commands14 calls9 command1 owners[1-9][0-9]* owner-add3 legacy-refused short-exhausted-recover duplicate-peer-forged-native-mutated-refused sealed-CPU-refused hot0-query0 no-encoder-callback-or-execution$/];
    const stageFamilies=prefixes.map(name=>progress.map((m,i)=>({m,i})).filter(x=>family(x.m)===name));
    const upstreamCounts=stageFamilies.every((rows,index)=>rows.length===2
      && rows.every(x=>x.i<prepIndex && patterns[index].test(x.m)));
    const callback=progress.filter(m=>family(m)==='BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_OK');
    const callbackPattern=/^BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_OK Command8 Cold8 Lifecycle5 ContentPlan6 callbacks8 native7 local1 shared1 owners[1-9][0-9]* owner-add11 legacy-short-enlarged-exhausted-forged-native-mutated-refused original-subscription-no-new-registration hot0-query0 no-issuer-record-execution$/;
    const callbackMatch=callback.length===1 && callbackPattern.test(callback[0]) && progress.indexOf(callback[0])<prepIndex;
    const order=[issuer,held,drained,...(fault || physical?[receipts]:[]),retire,rollback,projection,scenario].map(m=>progress.indexOf(m));
    const pendingIndex=progress.indexOf(one[7]),initializedIndex=progress.indexOf(one[8]);
    const ordered=order.every((n,i)=>n>=0 && (!i || n>order[i-1]))
      && pendingIndex>prepIndex && pendingIndex<order[1] && initializedIndex>=0 && initializedIndex<prepIndex;
    const oldCpuFamilies=['BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_RETIRE_OK','BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROLLBACK_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_CANDIDATE_RETIRE_OK','BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_SCENARIO_OK'];
    const cpuBaselineAbsent=!progress.some(m=>oldCpuFamilies.includes(family(m)));
    const committed='BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_COMMITTED_OK sampled-stamp-current attachment-'+(readback?'CopySrc':'ColorAttachment')+' Completed generation1 stale-initial-refused '+outputSuffix;
    const invalidated='BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_FAILED_OK '+(logicalLifetime || physical?'sampled-lifetime-refused':'sampled-stamp-current')+' attachment-content-state-invalidated no-completion no-readback';
    const readbackTerminalContent='BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_READBACK_TERMINAL_OK '+outputTerminal+' '
      +(outputLost?'sampled-device-loss-refused attachment-content-state-invalidated '
        :'sampled-stamp-current attachment-state-invalidated-content-retained ')
      +(outputPublishes?'cpu-published-before-loss':'unpublished-no-retry')+' readback3';
    const expectedContent=failed?invalidated:outputTerminal!==null && (outputLost || !outputPublishes)?readbackTerminalContent:committed;
    const contentRows=progress.map((m,i)=>({m,i})).filter(x=>family(x.m)===family(expectedContent));
    const forbiddenContentFamilies=[committed,invalidated,readbackTerminalContent].map(family).filter(name=>name!==family(expectedContent));
    const completionMatch=(!submission && !recordingLifetime) || ((terminal===1 || recordingLifetime) && contentRows.length===2
      && contentRows.every(x=>x.m===expectedContent) && contentRows[0].i>progress.indexOf(retire)
      && contentRows[0].i<progress.indexOf(rollback) && contentRows[1].i>progress.indexOf(rollback)
      && contentRows[1].i<progress.indexOf(one[10]) && !progress.some(m=>forbiddenContentFamilies.includes(family(m))));
    const terminalSelection=(!fault || (submission && allowedTerminals.includes(submissionTerminal)))
      && (!physical || (submission && !fault && !readback && physicalTerminals.includes(physicalTerminal)))
      && (!submission || terminal===1)
      && (!recordingLifetime || (!submission && !physical && !fault && !readback))
      && (outputTerminal===null || (readback && submission && !fault && outputTerminals.includes(outputTerminal)));
    const terminalFamilies=['BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_HELD_OK','BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_COMPLETE_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_HELD_OK','BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_TERMINAL_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_RECEIPTS_OK','BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_HELD_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_TERMINAL_OK','BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_RECEIPTS_OK'];
    const terminalModeMatch=progress.every(m=>!terminalFamilies.includes(family(m))
      || [held,drained,...(fault || physical?[receipts]:[])].includes(m));
    const expectedLifetimeDiagnostic=lifetimeTerminal==='retired'
      ?'RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED':'RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN';
    const lifetimeDescriptor='BINDLESS_RHI_GENERAL_SOURCE5_TERMINAL_OK '+lifetimeTerminal+' '+expectedLifetimeDiagnostic+' retained-original invalid-no-execution';
    const lifetimeSource='BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCE5_SUBMISSION_FAILED_TERMINAL_OK descriptor-lifetime-refused sampled-lifetime-refused attachment-invalidated';
    const logicalLifetimeMatch=!logicalLifetime || (progress.filter(m=>family(m)===family(lifetimeDescriptor)).length===1
      && progress.includes(lifetimeDescriptor) && progress.filter(m=>family(m)===family(lifetimeSource)).length===1
      && progress.includes(lifetimeSource) && progress.indexOf(lifetimeSource)>progress.indexOf(rollback)
      && progress.indexOf(lifetimeDescriptor)>progress.indexOf(lifetimeSource));
    const rejectedCandidateMatch=recordingLifetime
      ?progress.indexOf(rejectedCandidate)>progress.indexOf(held) && progress.indexOf(rejectedCandidate)<progress.indexOf(drained)
      :failed?progress.indexOf(rejectedCandidate)>progress.indexOf(drained) && progress.indexOf(rejectedCandidate)<progress.indexOf(receipts)
      :!progress.some(m=>family(m)===family(rejectedCandidate));
    const physicalDescriptor='BINDLESS_RHI_GENERAL_SOURCE5_TERMINAL_OK device-loss '
      +(physicalTerminal==='recording-shutdown'?'RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN':'RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST')
      +' retained-original invalid-no-execution';
    const physicalLifecycleMatch=!physical || (progress.filter(m=>family(m)===family(physicalDescriptor)).length===1
      && progress.includes(physicalDescriptor) && progress.includes(lifetimeSource)
      && progress.indexOf(lifetimeSource)>progress.indexOf(rollback)
      && progress.indexOf(physicalDescriptor)>progress.indexOf(lifetimeSource));
    const readbackFamilies=['BINDLESS_RHI_GENERAL_OUTPUT_READBACK_INPUTS4_OK',
      'BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK','BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_READBACK_OK',
      'BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_TERMINAL_OK','BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK'];
    const readbackRows=readbackFamilies.map(name=>progress.map((m,i)=>({m,i})).filter(x=>family(x.m)===name));
    const associated='BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_COMPLETED_OK authentic-queue-drain immutable-attachment strict-ColorAttachment forged-budget-refused readback3-ready';
    const prepared='BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_PREPARED_OK authentic-snapshot exact-attachment completion-refused';
    const numericPattern=/^BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK original-copy1 map1 unmap-drain committed-published rgba ([0-9]+\.[0-9]{8}) ([0-9]+\.[0-9]{8}) ([0-9]+\.[0-9]{8}) ([0-9]+\.[0-9]{8}) owners([1-9][0-9]*) hot0-query0 public-closed$/;
    const numeric=readbackRows[1].length===1?readbackRows[1][0].m.match(numericPattern):null;
    // Exact half-float reference for the authored 2x2 RGBA8 input and uint4/float4
    // buffer. Dynamic selects words5..8; static/whole selects words1..4.
    const expected=dynamic?[5.02734375,6.03125,7.03515625,8.0390625]:[1.02734375,2.03125,3.03515625,4.0390625];
    const numericMatch=!!numeric && expected.every((value,i)=>Number(numeric[i+1])===value);
    const outputTerminalMarker='BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_TERMINAL_OK '+outputTerminal
      +' same-snapshot canonical-copy1-map1 '+(outputPublishes?'published-half4':'invalidated-unpublished-no-retry')
      +' '+(outputLost?'actual-device-loss':'no-device-loss')+' strict-upstream-stale-refused private-only';
    const mapMarker='BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK '+outputTerminal+' original-copy-map genuine-drain '
      +(outputPublishes?'committed-numerical-published':'invalidated-unpublished-no-retry')+' '
      +(outputLost?'actual-device-loss':'no-device-loss')+' result-quota-zero public-closed';
    const expectedReadbackIndices=[0,...(outputPublishes?[1]:[]),...(outputTerminal===null?[2]:[4,3])];
    const readbackOrder=[progress.indexOf(prepared),progress.indexOf(held),progress.indexOf(associated),
      ...expectedReadbackIndices.map(index=>readbackRows[index].length===1?readbackRows[index][0].i:-1),progress.indexOf(drained)];
    const readbackMatch=readback?(submission && !fault && !p.has('general-map-terminal')
      && readbackRows.every((rows,i)=>rows.length===(expectedReadbackIndices.includes(i)?1:0))
      && (outputPublishes?numericMatch:!numericMatch)
      && progress.filter(m=>family(m)===family(associated)).length===1
      && progress.filter(m=>family(m)===family(prepared)).length===1
      && /^BINDLESS_RHI_GENERAL_OUTPUT_READBACK_INPUTS4_OK original-completed-output revision[1-9][0-9]* retained-Source2 correspondence1 ColorAttachment-CopySrc planned-only copies1 padded256 exact-less forged-refused hot0 native-copy-map-publication-closed$/.test(readbackRows[0][0].m)
      && (outputTerminal===null
        ?readbackRows[2][0].m==='BINDLESS_RHI_GENERAL_GRAPHICS_OUTPUT3_READBACK_OK same-snapshot ColorAttachment-CopySrc canonical-copy1-map1 published-half4 strict-upstream-stale-refused private-only'
        :readbackRows[3][0].m===outputTerminalMarker && readbackRows[4][0].m===mapMarker)
      && readbackOrder.every((index,i)=>index>=0 && (!i || index>readbackOrder[i-1])))
      :readbackRows.every(rows=>rows.length===0);
    const ownerHeld='BINDLESS_RHI_GENERAL_OUTPUT_OWNER_DROP_HELD_OK wrappers-expired command-storage-native-retained scopes3 Invalidated unpublished';
    const ownerDrained='BINDLESS_RHI_GENERAL_OUTPUT_OWNER_DROP_DRAINED_OK final-native-scope command-storage-expired unpublished no-retry';
    const ownerRows=progress.filter(m=>m.startsWith('BINDLESS_RHI_GENERAL_OUTPUT_OWNER_DROP_'));
    const ownerDropMatch=outputTerminal==='drop'
      ?readbackMatch && ownerRows.length===2 && ownerRows[0]===ownerHeld && ownerRows[1]===ownerDrained
        && progress.indexOf(ownerHeld)>readbackRows[0][0].i
        && progress.indexOf(ownerDrained)>progress.indexOf(ownerHeld)
        && progress.indexOf(ownerDrained)<readbackRows[4][0].i
      :ownerRows.length===0;
    const success=completed===true && terminal>0 && flags.every(flag=>p.has('general-graphics-'+flag))
      && !(whole&&dynamic) && ['uniform','readonly-storage'].includes(bufferType)
      && exactSingles && upstreamCounts && callbackMatch && ordered && cpuBaselineAbsent && completionMatch
      && terminalSelection && terminalModeMatch && logicalLifetimeMatch && physicalLifecycleMatch && rejectedCandidateMatch && readbackMatch && ownerDropMatch
      && diagnostics.length===0 && nativeErrors.length===0;
    return {success,scenario:physical?'original-general-finite-graphics-physical-loss':submission?'original-general-finite-graphics-native-submission':'original-general-finite-graphics-native-recording',bufferType,rangeKind,
      nativeSubmissionValidated:submission&&(!physical || physicalTerminal==='queue-uncertain')&&success,completionMatch,submissionTerminal,
      privateGraphicsOutput3ReadbackValidated:readback&&outputPublishes&&success,readbackMatch,numericMatch,
      privateGraphicsOutput3LifecycleValidated:readback&&outputTerminal!==null&&success,outputTerminal,
      privateGraphicsOutput3CallerDropValidated:readback&&outputTerminal==='drop'&&success,ownerDropMatch,
      physicalDeviceLossValidated:(physical || (readback&&outputTerminal!==null&&outputLost))&&success,
      physicalTerminal,privateGraphicsPhysicalLifetimeValidated:physical&&success,
      nativeSubmissionCompletionValidated:submission&&!failed&&success,submissionTerminalValidated:fault&&success,
      privateGraphicsLogicalLifetimeValidated:logicalLifetime&&success,
      privateGraphicsRecordingLifetimeValidated:recordingLifetime&&success,
      recordingTerminal:kind,nativeRecordingValidated:success,upstreamCounts,callbackMatch,ordered,cpuBaselineAbsent,
      diagnostics:diagnostics.slice(),nativeErrors:nativeErrors.slice(),progress:progress.slice(),
      nativeDrawReadbackValidated:false,finiteGraphicsValidated:false,fullFiniteExecutionValidated:false,
      finiteLifecycle4Validated:false,publicBindlessExecutionAvailable:false,nativeSyncPromoted:false,
      woccValidated:false,platformValidated:false,productionReady:false};
  }
  const api={inspect};
  if(typeof module!=='undefined' && module.exports)module.exports=api;
  else root.OriginalGraphicsRecordingReport=api;
})(typeof globalThis!=='undefined'?globalThis:this);
