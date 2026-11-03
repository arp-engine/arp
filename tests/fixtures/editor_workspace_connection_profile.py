"""Explicit metadata for actual Editor source-workspace validation fixtures."""


def connection_profile(origin, root='workspace', file_bytes=65536, preparation_bytes=8*1024*1024):
    return dict(type='arp.editor-workspace-connection', contractVersion=1,
        endpoint=origin + '/workspace/v2', credentialOrigin=origin,
        workspaceId='fixture', projectRoot=root,
        io=dict(maxPathBytes=512, maxFileBytes=file_bytes, maxRequestBytes=2*file_bytes,
            maxResponseBytes=2*file_bytes, preparationBytes=preparation_bytes, maxQueryBytes=8192,
            maxDirectoryEntries=32, maxDirectoryNameBytes=4096, maxHeaderBytes=8192,
            ioChunkBytes=4096, timeoutMs=10000, cancellationPollMs=2, maxDnsAddresses=0),
        editor=dict(inputs=dict(maxFiles=64, maxFileBytes=file_bytes, maxTotalBytes=4*file_bytes),
            directory=dict(maxEntries=32, pageSize=4, maxNameBytes=4096, maxPageBytes=8192),
            maxDecodedBytes=4*file_bytes, maxDiagnosticBytes=4096, maxHistoryEntries=256,
            maxScriptDocuments=64,
            scriptReload=dict(maxHistoryEntries=4096, maxHistoryBytes=16*1024*1024)))


def wocc_connection_profile(origin):
    """Explicit actual-resource capacities, independent of the small fixture defaults."""
    result = connection_profile(origin, file_bytes=32*1024*1024, preparation_bytes=256*1024*1024)
    result['io'].update(maxQueryBytes=128*1024, maxDirectoryEntries=512,
        maxDirectoryNameBytes=64*1024, ioChunkBytes=16*1024, timeoutMs=30000)
    result['editor']['inputs'].update(maxFiles=512, maxTotalBytes=256*1024*1024)
    result['editor']['directory'].update(maxEntries=512, pageSize=64,
        maxNameBytes=64*1024, maxPageBytes=128*1024)
    result['editor']['maxDecodedBytes'] = 512*1024*1024
    return result
