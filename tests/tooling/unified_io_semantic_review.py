"""Source-pinned annotations for the IO inventory, never call-graph authority."""
from collections import Counter
import hashlib
from pathlib import PurePosixPath
import re

import yaml


class ReviewDocumentError(ValueError):
    pass


class _ReviewLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        keys = set()
        for key, _ in node.value:
            if not isinstance(key, yaml.ScalarNode) or key.tag != 'tag:yaml.org,2002:str':
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-KEY')
            if key.value in keys:
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-DUPLICATE-KEY')
            keys.add(key.value)
        return super().construct_mapping(node, deep)


class SemanticIoReviews:
    relative_path = 'docs/unified-io-editor-callsite-reviews.yaml'
    source_policy = 'Utf8CrLfToLf/1'
    statuses = frozenset({'ReviewedNonFileReference', 'ReviewedNativeHostBoundary',
                          'ReviewedValidationFixture', 'ReviewedProtocolTransport',
                          'ReviewedPartialMigration', 'ReviewedSourceMigration'})
    entry_keys = frozenset({'path', 'sha256', 'owner', 'batch', 'context', 'status',
                            'evidence', 'references'})

    def __init__(self, source):
        try:
            document = yaml.load(source, Loader=_ReviewLoader)
        except (yaml.YAMLError, RecursionError) as error:
            raise ReviewDocumentError('IO-INVENTORY-REVIEW-YAML') from error
        if (not isinstance(document, dict)
                or set(document) != {'type', 'contractVersion', 'scope', 'sourcePolicy', 'entries'}
                or document.get('type') != 'UnifiedIoSemanticReviews'
                or type(document.get('contractVersion')) is not int
                or document['contractVersion'] != 1
                or document.get('scope') != 'ExactSourceAndListedReferencesOnly'
                or document.get('sourcePolicy') != self.source_policy
                or not isinstance(document.get('entries'), list)):
            raise ReviewDocumentError('IO-INVENTORY-REVIEW-CONTRACT')
        self.entries = {}
        for entry in document['entries']:
            if not isinstance(entry, dict) or set(entry) != self.entry_keys:
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-ENTRY')
            path = entry['path']
            if (not isinstance(path, str) or not path.startswith('editor/')
                    or '\\' in path or ':' in path or '..' in PurePosixPath(path).parts
                    or str(PurePosixPath(path)) != path or path in self.entries):
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-PATH')
            if (not isinstance(entry['sha256'], str)
                    or not re.fullmatch('[0-9a-f]{64}', entry['sha256'])
                    or not isinstance(entry['status'], str) or entry['status'] not in self.statuses
                    or not all(isinstance(entry[key], str) and entry[key].strip()
                               for key in ('owner', 'batch', 'context', 'evidence'))):
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-CLASSIFICATION')
            refs = entry['references']
            if not isinstance(refs, list) or not refs:
                raise ReviewDocumentError('IO-INVENTORY-REVIEW-REFERENCES')
            for ref in refs:
                if (not isinstance(ref, dict) or set(ref) != {'line', 'symbol', 'operation'}
                        or type(ref['line']) is not int or ref['line'] < 1
                        or not isinstance(ref['symbol'], str) or not ref['symbol']
                        or not isinstance(ref['operation'], str)
                        or ref['operation'] not in {'read', 'metadata', 'write', 'transport', 'receipt'}):
                    raise ReviewDocumentError('IO-INVENTORY-REVIEW-REFERENCES')
            self.entries[path] = entry

    @staticmethod
    def signature(references):
        return Counter((ref['line'], ref['symbol'], ref['operation']) for ref in references)

    @staticmethod
    def source_digest(raw):
        # Explicit source-review policy only; resource payloads remain byte exact.
        raw.decode('utf-8')
        return hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()

    def check(self, path, raw, references):
        entry = self.entries.get(path)
        if entry is None:
            return None, None
        try:
            digest = self.source_digest(raw)
        except UnicodeError:
            return None, 'IO-INVENTORY-REVIEW-SOURCE-ENCODING'
        if digest != entry['sha256']:
            return None, 'IO-INVENTORY-REVIEW-SOURCE-CHANGED'
        if self.signature(references) != self.signature(entry['references']):
            return None, 'IO-INVENTORY-REVIEW-REFERENCES-CHANGED'
        return entry, None
