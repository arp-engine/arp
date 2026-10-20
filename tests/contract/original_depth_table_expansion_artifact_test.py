"""Native immutable artifact owner and rehashed complete-program substitution."""
import argparse
import hashlib
import pathlib

import yaml
from single_entry_shader_artifact_test import ArtifactContractFixture


class OriginalTableArtifactFixture(ArtifactContractFixture):
    def entry_edit(self, mutate, expected='TARGET-PROVENANCE', role='wgsl'):
        def edit(folder,node):
            entry=next(e for e in node['entries'] if e['stage']!='Vertex')
            file=entry['files'][role];path=folder/file['path'];data=path.read_bytes()
            changed=mutate(data);assert changed!=data
            path.write_bytes(changed);file['sha256']=hashlib.sha256(changed).hexdigest()
            node['artifactBytes']+=len(changed)-len(data)
        self.verify(expected,bytes_edit=edit)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--executable',required=True)
    parser.add_argument('--compute',type=pathlib.Path,required=True)
    parser.add_argument('--graphics',type=pathlib.Path,required=True)
    parser.add_argument('--geometry',type=pathlib.Path,required=True)
    args=parser.parse_args();calls=0
    for profile,publication in [('Compute',args.compute),('Graphics',args.graphics),('GraphicsGeometry',args.geometry)]:
        fixture=OriginalTableArtifactFixture(args.executable,publication,(profile,))
        fixture.verify()
        for field,value in [('type','SingleEntryShaderArtifact'),('contractVersion',2),('matrixLayout','column-major'),
                ('switchCases',2),('physicalBindingCount',1),('generatedSourceSha256','0'*64),
                ('compilerVersion','unapproved-compiler')]:
            fixture.verify('EXPANSION-',lambda n,f=field,v=value:n.update({f:v}))
        fixture.verify('PUBLICATION',lambda n:n.update(extra='unknown'))
        fixture.verify('EXECUTION-PROMOTION',lambda n:n.update(executionAvailable=True))
        fixture.verify('YAML-CONTRACT-FIELD-INVALID',lambda n:n.update(executionAvailable=0))
        fixture.verify('SHAPE',lambda n:n['logicalTable'].update(capacity=1))
        fixture.verify('SHAPE',lambda n:n['logicalTable'].update(stage='Vertex'))
        fixture.verify('LOGICAL-TABLE',lambda n:n['logicalTable'].update(depthTexture=False))
        fixture.verify('YAML-CONTRACT-FIELD-INVALID',lambda n:n['logicalTable'].update(set=False))
        fixture.verify('SHAPE',lambda n:n['elementMap'].reverse())
        fixture.verify('SHAPE',lambda n:n['elementMap'][1].update(binding=0))
        fixture.verify('SHAPE',lambda n:n['entries'][0].update(stage='Fragment'))
        fixture.verify('ENTRY-COUNT',lambda n:n['entries'].pop())
        fixture.verify('SHAPE',lambda n:n['backendDecisions']['WebGPU'].update(mode='Equivalent'))
        # v1's closed historical schema is not the current RHI backend enum.
        fixture.verify('BACKENDS',lambda n:n['backendDecisions'].update(WebGL=dict(n['backendDecisions']['WebGPU'])))
        fixture.verify('BACKENDS',lambda n:n['backendDecisions'].pop('Metal'))
        fixture.verify('YAML-CONTRACT-FIELD-INVALID',lambda n:n['backendDecisions'].update(
            WebGL=n['backendDecisions'].pop('WebGPU')))
        fixture.verify('FILE-ROLE-PATH',lambda n:n['entries'][0]['files']['wgsl'].update(path='../bad.wgsl'))
        fixture.verify('FILE-ROLES',lambda n:n['entries'][0]['files'].update(source=n['generatedSource']))
        def source_edit(folder,node):
            file=node['generatedSource'];path=folder/file['path'];data=path.read_bytes()+b'\n// rehashed substitution\n'
            path.write_bytes(data);file['sha256']=hashlib.sha256(data).hexdigest();node['generatedSourceSha256']=file['sha256']
        fixture.verify('SOURCE-PROVENANCE',bytes_edit=source_edit)
        # Numeric output can be changed without altering any resource declarations.
        fixture.entry_edit(lambda b:b.replace(b'1.0f',b'0.0f'))
        fixture.entry_edit(lambda b:b.replace(b'% u32(3)',b'% u32(2)'))
        fixture.entry_edit(lambda b:b.replace(b'Inputs_1_0',b'Inputs_wrong_0'))
        fixture.entry_edit(lambda b:b.replace(b'depth2d<float',b'texture2d<float'),role='metal')
        fixture.entry_edit(lambda b:b+b'\0\0\0\0',role='spirv')
        fixture.entry_edit(lambda b:b.replace(b'arrayCount: 1',b'arrayCount: 2'),role='reflection')
        if publication==args.geometry:
            def vertex_edit(folder,node):
                entry=next(e for e in node['entries'] if e['stage']=='Vertex')
                file=entry['files']['reflection'];path=folder/file['path'];data=path.read_bytes()
                changed=data.replace(b'format: Float4',b'format: Float3');assert changed!=data
                path.write_bytes(changed);file['sha256']=hashlib.sha256(changed).hexdigest()
                node['artifactBytes']+=len(changed)-len(data)
            fixture.verify('TARGET-PROVENANCE',bytes_edit=vertex_edit)
            fixture.verify('SOURCE-PROVENANCE',lambda n:n['schema'].update(profile='Graphics'))
        calls+=fixture.calls
    print(f'OriginalDepthTableExpansionArtifact: {calls} genuine owner/shape/path/rehashed operation/provenance cases passed')


if __name__=='__main__':main()
