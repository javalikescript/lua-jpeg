local bufferLib = require('buffer')
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

jpegLib.decompress(cinfo);

info = jpegLib.getInfosDecompress(cinfo);

local image = bufferLib.new(info.output.components * info.output.width * info.output.height);
jpegLib.decompress(cinfo, image);

fd:close()

print('image decompressed')

-- draw a black X
for y = 0, info.output.height - 1  do
    local x = math.floor(y * info.output.width / info.output.height)
    bufferLib.byteset(image, (y * info.output.width + x) * info.output.components, 0, 0, 0);
    bufferLib.byteset(image, ((info.output.height - 1 - y) * info.output.width + x) * info.output.components, 0, 0, 0);
end

local filename = 'tmp_scale.jpg'
fd = io.open(filename, 'wb')

jpegLib.compress(jpegLib.newCompress(), image, info.output, function(data)
    --print('write('..tostring(#data)..')')
    fd:write(data)
end);

fd:close()

print('image compressed in '..filename);

