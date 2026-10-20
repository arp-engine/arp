"""Finite offline package publication; no device or execution authorization."""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile

import yaml
import compile_shaders as compiler
from bindless_codegen import (BindlessContractError, compile_bindless_package, generate_bindless,
                              identifier, integer, read_bindless_publication, reject)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative_path(name):
    if (not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_./-]+', name)
            or pathlib.PurePosixPath(name).is_absolute()
            or any(part in ('', '.', '..') for part in name.split('/'))):
        reject('FAMILY-PATH')
    return pathlib.PurePosixPath(name)


def owned_path(root, name):
    relative_path(name)
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        reject('FAMILY-PATH')
    return path


def validate_authoring(document):
    if (not isinstance(document, dict) or document.get('type') != 'BindlessShaderFamily'
            or set(document) != {'type', 'contractVersion', 'name', 'maxVariants', 'variants'}):
        reject('FAMILY-SCHEMA')
    if integer(document.get('contractVersion'), 'family-version', 1) != 1:
        reject('FAMILY-VERSION')
    identifier(document.get('name'))
    maximum = integer(document.get('maxVariants'), 'family-budget', 1)
    variants = document.get('variants')
    if (not isinstance(variants, list) or not variants or len(variants) > maximum
            or any(not isinstance(name, str) for name in variants)
            or len(set(variants)) != len(variants)):
        reject('FAMILY-VARIANTS')
    for reference in variants:
        relative_path(reference)
    return variants


def interface(document):
    # The existing generator validates all schema fields first. Compare exact
    # logical declarations; capacities and physical mappings belong to each
    # independent package and are deliberately not interchangeable ABI values.
    omitted = {'name', 'maxBindings', 'maxSwitchCases', 'snapshotBytes',
               'snapshotMetadataBinding', 'faultLatchBinding', 'tables'}
    signature = {key: value for key, value in document.items() if key not in omitted}
    signature['faultReporting'] = 'faultLatchBinding' in document
    signature['tables'] = [{key: value for key, value in table.items()
                            if key not in ('binding', 'capacity')} for table in document['tables']]
    return signature


def verify_input(root, stamp):
    path = owned_path(root, stamp['path'])
    if digest(path.read_bytes()) != stamp['sha256']:
        reject('FAMILY-INPUT-HASH')
    return path


def verify_member(root, member, source_hash, entries, mode, implementations):
    variant_path = verify_input(root, member['variant'])
    document = yaml.safe_load(variant_path.read_text(encoding='utf-8'))
    generated = generate_bindless(document, mode)
    if member['name'] != document['name']:
        reject('FAMILY-MEMBER-NAME')
    expected_path = 'members/' + document['name'] + '/' + document['name'] + '.' + mode.lower() + '.bindless.yaml'
    if member['publication'] != expected_path:
        reject('FAMILY-MEMBER-PATH')
    publication = owned_path(root, member['publication'])
    package = read_bindless_publication(publication)
    lowering = yaml.safe_dump(generated.reflection, sort_keys=False)
    if (package['shaderPackageIdentity'] != member['shaderPackageIdentity']
            or package['validationStatus'] != 'TargetReflectionValidated'
            or package['execution'] != mode or package['matrixLayout'] != 'row-major'
            or package['authoredSourceSha256'] != source_hash
            or [module['entry'] for module in package['modules']] != entries
            or package['compilerHelperSha256'] != implementations['compile_shaders.py']
            or package['generatorSha256'] != implementations['bindless_codegen.py']
            or package['loweringYaml'] != lowering
            or package['loweringSha256'] != digest(lowering.encode('utf-8'))):
        reject('FAMILY-MEMBER-MISMATCH')
    for name, expected in package['artifacts'].items():
        if digest(owned_path(publication.parent, name).read_bytes()) != expected:
            reject('FAMILY-ARTIFACT-HASH')
    return document


def _read_bindless_family(path):
    """Verify a relocatable offline inventory; does not issue runtime plans."""
    path = pathlib.Path(path).resolve()
    pointer = yaml.safe_load(path.read_text(encoding='utf-8'))
    if (not isinstance(pointer, dict) or pointer.get('type') != 'BindlessFamilyPublication'
            or type(pointer.get('contractVersion')) is not int or pointer['contractVersion'] != 1
            or pointer.get('execution') not in ('Native', 'Emulated')
            or not isinstance(pointer.get('familyIdentity'), str)
            or not re.fullmatch(r'[0-9a-f]{64}', pointer['familyIdentity'])
            or pointer.get('payload') != pointer['familyIdentity'] + '/family.payload.yaml'):
        reject('FAMILY-PUBLICATION')
    payload_path = owned_path(path.parent, pointer['payload'])
    payload_bytes = payload_path.read_bytes()
    if digest(payload_bytes) != pointer['familyIdentity']:
        reject('FAMILY-HASH')
    payload = yaml.safe_load(payload_bytes)
    if (not isinstance(payload, dict) or payload.get('type') != 'BindlessShaderFamilyArtifact'
            or type(payload.get('contractVersion')) is not int or payload['contractVersion'] != 1
            or payload.get('execution') != pointer['execution']):
        reject('FAMILY-PAYLOAD')
    root = payload_path.parent
    family_path = verify_input(root, payload['inputs']['family'])
    authored = yaml.safe_load(family_path.read_text(encoding='utf-8'))
    variants = validate_authoring(authored)
    source_path = verify_input(root, payload['inputs']['source'])
    source_hash = digest(source_path.read_text(encoding='utf-8').encode('utf-8'))
    implementations = {}
    for stamp in payload['inputs']['implementations']:
        file = verify_input(root, stamp)
        if file.name in implementations:
            reject('FAMILY-IMPLEMENTATION')
        implementations[file.name] = stamp['sha256']
    if set(implementations) != set(BindlessShaderFamilyCompiler.IMPLEMENTATIONS):
        reject('FAMILY-IMPLEMENTATION')
    entries = payload['entries']
    if not isinstance(entries, list) or not entries or len(set(entries)) != len(entries):
        reject('FAMILY-ENTRIES')
    for entry in entries:
        identifier(entry)
    members = payload['members']
    if not isinstance(members, list) or len(members) != len(variants):
        reject('FAMILY-MEMBERS')
    names, signatures = set(), []
    for index, member in enumerate(members):
        if member['authoringReference'] != variants[index]:
            reject('FAMILY-MEMBER-REFERENCE')
        document = verify_member(root, member, source_hash, entries, payload['execution'], implementations)
        if document['name'] in names:
            reject('FAMILY-MEMBER-NAME')
        names.add(document['name'])
        signatures.append(interface(document))
        member['document'] = document
    if any(signature != signatures[0] for signature in signatures[1:]):
        reject('FAMILY-INTERFACE-MISMATCH')
    payload.update(authoring=authored, familyIdentity=pointer['familyIdentity'],
                   directory=pointer['familyIdentity'])
    return payload


def read_bindless_family(path):
    """Stable refusal for malformed/missing offline family data."""
    try:
        return _read_bindless_family(path)
    except BindlessContractError:
        raise
    except (OSError, KeyError, TypeError, yaml.YAMLError):
        reject('FAMILY-DATA-UNAVAILABLE')


class BindlessShaderFamilyCompiler:
    IMPLEMENTATIONS = ('bindless_family.py', 'bindless_codegen.py', 'compile_shaders.py',
                       'shader_bindings_codegen.py', 'slang_resource_switch.py')

    def compile(self, family_path, source_path, entries, mode, output, slangc, reflect):
        if mode not in ('Native', 'Emulated'):
            reject('FAMILY-MODE')
        output = pathlib.Path(output).resolve()
        output.mkdir(parents=True, exist_ok=True)
        publication = output / ('bindless-family.' + mode.lower() + '.yaml')
        publication.unlink(missing_ok=True)
        authority = pathlib.Path(__file__).resolve().parent
        if (pathlib.Path(compiler.__file__).resolve() != authority / 'compile_shaders.py'
                or pathlib.Path(compiler._bindings_codegen.__file__).resolve()
                    != authority / 'shader_bindings_codegen.py'):
            reject('FAMILY-AUTHORITY-IMPORT')
        family_path, source_path = pathlib.Path(family_path).resolve(), pathlib.Path(source_path).resolve()
        family_bytes, source_bytes = family_path.read_bytes(), source_path.read_bytes()
        authoring = yaml.safe_load(family_bytes)
        variants = validate_authoring(authoring)
        if not entries or len(set(entries)) != len(entries):
            reject('FAMILY-ENTRIES')
        for entry in entries:
            identifier(entry)
        tracked = {family_path: digest(family_bytes), source_path: digest(source_bytes)}
        documents, names, signatures = [], set(), []
        for reference in variants:
            variant_path = owned_path(family_path.parent, reference)
            data = variant_path.read_bytes()
            document = yaml.safe_load(data)
            generate_bindless(document, mode)
            if document['name'] in names:
                reject('FAMILY-MEMBER-NAME')
            names.add(document['name'])
            signatures.append(interface(document))
            documents.append((reference, document, data))
            tracked[variant_path] = digest(data)
        if any(signature != signatures[0] for signature in signatures[1:]):
            reject('FAMILY-INTERFACE-MISMATCH')
        implementations = []
        for name in self.IMPLEMENTATIONS:
            path = pathlib.Path(__file__).parent / name
            data = path.read_bytes()
            tracked[path] = digest(data)
            implementations.append((name, data))
        with tempfile.TemporaryDirectory(prefix='.bindless-family-', dir=output) as directory:
            candidate = pathlib.Path(directory)
            def snapshot(name, data):
                target = candidate / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                return dict(path=name, sha256=digest(data))
            inputs = dict(family=snapshot('inputs/family.yaml', family_bytes),
                source=snapshot('inputs/source.slang', source_bytes), implementations=[
                    snapshot('inputs/implementation/' + name, data) for name, data in implementations])
            members = []
            for index, (reference, document, data) in enumerate(documents):
                member_dir = candidate / 'members' / document['name']
                package_path = compile_bindless_package(document, candidate / inputs['source']['path'],
                    entries, mode, member_dir, slangc, reflect)
                package = read_bindless_publication(package_path)
                members.append(dict(name=document['name'], authoringReference=reference,
                    variant=snapshot('inputs/variant-' + str(index) + '.yaml', data),
                    publication=package_path.relative_to(candidate).as_posix(),
                    shaderPackageIdentity=package['shaderPackageIdentity']))
            if any(digest(path.read_bytes()) != stamp for path, stamp in tracked.items()):
                reject('FAMILY-INPUT-CHANGED')
            payload = dict(type='BindlessShaderFamilyArtifact', contractVersion=1,
                execution=mode, entries=entries, inputs=inputs, members=members)
            payload_bytes = yaml.safe_dump(payload, sort_keys=False).encode('utf-8')
            identity = digest(payload_bytes)
            (candidate / 'family.payload.yaml').write_bytes(payload_bytes)
            pointer = dict(type='BindlessFamilyPublication', contractVersion=1, execution=mode,
                familyIdentity=identity, payload=identity + '/family.payload.yaml')
            pending = output / ('bindless-family.' + mode.lower() + '.yaml.pending')
            try:
                final = owned_path(output, identity)
                if final.exists():
                    if (final / 'family.payload.yaml').read_bytes() != payload_bytes:
                        reject('FAMILY-IDENTITY-COLLISION')
                else:
                    candidate.rename(final)
                pending.write_bytes(yaml.safe_dump(pointer, sort_keys=False).encode('utf-8'))
                read_bindless_family(pending)
                pending.replace(publication)
            finally:
                pending.unlink(missing_ok=True)
        return publication


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bindless-family', type=pathlib.Path, required=True)
    parser.add_argument('--bindless-source', type=pathlib.Path, required=True)
    parser.add_argument('--bindless-entry', action='append', required=True)
    parser.add_argument('--bindless-mode', choices=('Native', 'Emulated'), required=True)
    parser.add_argument('--out-dir', type=pathlib.Path, required=True)
    parser.add_argument('--slangc')
    parser.add_argument('--vulkan-sdk')
    parser.add_argument('--spirv-reflect')
    args = parser.parse_args()
    try:
        # Discovery can fail before compile(); revoke the selected publication
        # for that failure too, without touching any immutable member directory.
        publication = args.out_dir / ('bindless-family.' + args.bindless_mode.lower() + '.yaml')
        publication.unlink(missing_ok=True)
        slangc = compiler.find_slangc(args)
        reflect = compiler.find_spirv_reflect(compiler.find_vulkan_sdk(args), args.spirv_reflect)
        if not slangc or not reflect:
            reject('FAMILY-COMPILER-TOOLS-MISSING')
        publication = BindlessShaderFamilyCompiler().compile(args.bindless_family,
            args.bindless_source, args.bindless_entry, args.bindless_mode, args.out_dir, slangc, reflect)
        print('[shaders] Bindless family:', publication)
    except subprocess.CalledProcessError as error:
        print('RHI-BINDLESS-FAMILY-COMPILER-FAILED:', error, file=sys.stderr)
        return 1
    except (ValueError, OSError, KeyError, TypeError, yaml.YAMLError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
