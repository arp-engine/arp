"""Read texture-to-buffer bytes from the captured native copy's explicit layout."""


class RenderDocTextureBufferCopy:
    @staticmethod
    def _fields(node):
        return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}

    @staticmethod
    def _number(node):
        return int(node.data.basic.u)

    @classmethod
    def d3d12_rgba8(cls, controller, action, texture, destination):
        if texture.format.Name() not in ('R8G8B8A8_UNORM', 'B8G8R8A8_UNORM'):
            raise RuntimeError('CAPTURE-COPY-D3D12-RGBA8-FORMAT')
        return cls.d3d12_color(controller, action, texture, destination)

    @classmethod
    def d3d12_color(cls, controller, action, texture, destination):
        chunks = controller.GetStructuredFile().chunks
        original = [chunks[e.chunkIndex] for e in action.events if e.eventId == action.eventId]
        if len(original) != 1 or original[0].name != 'ID3D12GraphicsCommandList::CopyTextureRegion':
            raise RuntimeError('CAPTURE-COPY-D3D12-COMMAND')
        fields = cls._fields(original[0])
        src, dst = cls._fields(fields['src']), cls._fields(fields['dst'])
        if (str(texture.resourceId) != 'ResourceId::' + str(cls._number(src['pResource']))
                or str(destination.resourceId) != 'ResourceId::' + str(cls._number(dst['pResource']))
                or cls._number(src['Type']) != 0 or cls._number(src['SubresourceIndex']) != 0
                or cls._number(dst['Type']) != 1
                or any(cls._number(fields[key]) for key in ('DstX', 'DstY', 'DstZ'))):
            raise RuntimeError('CAPTURE-COPY-D3D12-RESOURCE-IDENTITY')
        placed = cls._fields(dst['PlacedFootprint'])
        footprint = cls._fields(placed['Footprint'])
        width, height = texture.width, texture.height
        box = cls._fields(fields['pSrcBox'])
        expected_box = dict(left=0, top=0, front=0, right=width, bottom=height, back=1)
        if any(cls._number(box[key]) != value for key, value in expected_box.items()):
            raise RuntimeError('CAPTURE-COPY-D3D12-SOURCE-RANGE')
        # DXGI_FORMAT defines R8G8B8A8_UNORM=28 and B8G8R8A8_UNORM=87.
        # DXGI_FORMAT R16G16B16A16_FLOAT=10. The recorded metadata defines
        # both the format and tight bytes per pixel; never infer from output.
        formats = {'R8G8B8A8_UNORM': (28,4), 'B8G8R8A8_UNORM': (87,4),
                   'R16G16B16A16_FLOAT': (10,8)}
        if (texture.format.Name() not in formats
                or cls._number(footprint['Format']) != formats[texture.format.Name()][0]
                or cls._number(footprint['Width']) != width
                or cls._number(footprint['Height']) != height
                or cls._number(footprint['Depth']) != 1):
            raise RuntimeError('CAPTURE-COPY-D3D12-FOOTPRINT')
        offset, pitch = cls._number(placed['Offset']), cls._number(footprint['RowPitch'])
        pixel_bytes = formats[texture.format.Name()][1]
        # D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256; use the actual recorded pitch.
        if pitch < width * pixel_bytes or pitch % 256 or offset + pitch * height > destination.length:
            raise RuntimeError('CAPTURE-COPY-D3D12-BUFFER-RANGE')
        raw = bytes(controller.GetBufferData(destination.resourceId, offset, pitch * height))
        if len(raw) != pitch * height:
            raise RuntimeError('CAPTURE-COPY-D3D12-BUFFER-READ')
        tight = b''.join(raw[y * pitch:y * pitch + width * pixel_bytes] for y in range(height))
        return tight, dict(offset=offset, rowPitch=pitch, width=width, height=height,
                           nativeFormat=texture.format.Name())
