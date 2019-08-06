local jpegLib = require('jpeg')

local cinfo = jpegLib.newDecompress();

local filename = 'libjpeg/testimg.jpg'

print('reading '..filename)
local fd = io.open(filename, 'rb')

jpegLib.fillSource(cinfo, function()
    local data = fd:read(2048)
    --print('read '..tostring(#data))
    return data
end);

local info, err = jpegLib.readHeader(cinfo);

if err or (type(info) ~= 'table') then
    fd:close()
    error('Cannot read header');
end

jpegLib.configureDecompress(cinfo, {scaleNum = 4, scaleDenom = 8});

jpegLib.startDecompress(cinfo);

info = jpegLib.getInfosDecompress(cinfo);

local image = jpegLib.newBuffer(info.output.components * info.output.width * info.output.height);

jpegLib.decompress(cinfo, image);

fd:close()

print('image decompressed')

local filename = 'tmp_scale.jpg'
fd = io.open(filename, 'wb')

local cinfo = jpegLib.newCompress()

jpegLib.startCompress(cinfo, info.output, function(data)
    --print('write('..tostring(#data)..')')
    fd:write(data)
end)

--jpegLib.writeMarker(cinfo, 0xe1, buffer)

jpegLib.compress(cinfo, image);

fd:close()

print('image compressed in '..filename)

