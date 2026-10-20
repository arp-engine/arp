"""Reject forged parity, altered uploads, missing Root output and LOD substitutes."""
import argparse,copy,hashlib,json,pathlib,shutil,sys,tempfile,unittest
import yaml
from terrain27_implicit_sampling_reference import canonical_inputs,load_terrain27_reference_contract,validate_terrain27_reference_result

class Terrain27ReferenceTests(unittest.TestCase):
 def evidence(self):
  contract=load_terrain27_reference_contract(RUNNER);inputs=canonical_inputs()
  reference=dict(contract,success=True,realImplicitDerivatives=True,mipZeroSubstitutionRejected=True,arrayLayersValidated=True,publicExecutionGranted=False,nativeErrors=[],cleanRootReceipts=2,cleanPixelReceipts=2,values=inputs['expected'],pixels=inputs['pixels'])
  messages=[]
  for kind,values in [('VALUES',reference['values']),('PIXELS',reference['pixels'])]:
   receipt='BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_PARITY_'+kind+' fault0 '+' '.join(map(str,values))
   messages.extend([dict(message=receipt),dict(message=receipt)])
  return contract,dict(terrain27ImplicitSamplingReference=reference,progress=messages)
 def test_preserved_cross_target_implicit_samples(self):
  contract,result=self.evidence()
  self.assertTrue(validate_terrain27_reference_result(result,contract))
  inputs=canonical_inputs();self.assertEqual(inputs['textureLayers'],[1]*25+[2]*2)
  self.assertEqual([len(x) for x in inputs['ordinary']],[96,16])
 def test_missing_extra_fault_and_changed_receipts_refuse(self):
  contract,result=self.evidence()
  for mutate in [lambda r:r['progress'].pop(),lambda r:r['progress'].append(r['progress'][0]),lambda r:r['progress'][0].update(message=r['progress'][0]['message']+' 0'),lambda r:r['progress'][0].update(message=r['progress'][0]['message'].replace('fault0','fault1'))]:
   bad=copy.deepcopy(result);mutate(bad);self.assertFalse(validate_terrain27_reference_result(bad,contract))
 def test_wrong_hash_flags_nonfinite_and_agreed_wrong_outputs_refuse(self):
  contract,result=self.evidence()
  for key,value in [('shaderSha256','0'*64),('inputSha256','0'*64),('nativeErrors',['validation']),('publicExecutionGranted',True),('realImplicitDerivatives',False),('mipZeroSubstitutionRejected',False),('arrayLayersValidated',False)]:
   bad=copy.deepcopy(result);bad['terrain27ImplicitSamplingReference'][key]=value
   self.assertFalse(validate_terrain27_reference_result(bad,contract),key)
  for kind,lane,value in [('values',0,float('nan')),('values',64,3),('pixels',0,0)]:
   bad=copy.deepcopy(result);values=bad['terrain27ImplicitSamplingReference'][kind];values[lane]=value
   prefix='BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_PARITY_'+('VALUES' if kind=='values' else 'PIXELS')+' fault0 '
   for receipt in bad['progress']:
    if receipt['message'].startswith(prefix):receipt['message']=prefix+' '.join(map(str,values))
   self.assertFalse(validate_terrain27_reference_result(bad,contract))
 def test_resealed_gradient_shader_and_mutated_inputs_refuse(self):
  for mutation in ['input','gradient','suppression']:
   with tempfile.TemporaryDirectory() as d:
    dest=pathlib.Path(d);folder=dest/'bindless-implicit-reference/ordinary_terrain27'
    shutil.copytree(RUNNER/'bindless-implicit-reference/ordinary_terrain27',folder)
    if mutation=='input':(folder/'inputs.json').write_text('{}')
    else:
     path=folder/'ordinary_terrain27.graphics.module.yaml';publication=yaml.safe_load(path.read_text())
     wgsl=folder/publication['files']['wgsl']['path'];code=wgsl.read_text()
     code=code.replace('textureSample(', 'textureSampleGrad(',1) if mutation=='gradient' else 'diagnostic(off, derivative_uniformity);\n'+code
     wgsl.write_text(code);publication['files']['wgsl']['sha256']=hashlib.sha256(wgsl.read_bytes()).hexdigest()
     path.write_text(yaml.safe_dump(publication))
    with self.assertRaisesRegex(ValueError,'IMPLICIT-REFERENCE|TERRAIN27-REFERENCE'):load_terrain27_reference_contract(dest)

if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--runner',required=True,type=pathlib.Path)
 args,rest=parser.parse_known_args();RUNNER=args.runner
 unittest.main(argv=[sys.argv[0]]+rest)
