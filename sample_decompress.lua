local bufferLib = require('buffer')
local jpegLib = require('jpeg')

local function printTable(t, i, p)
    i = i or '  '
    p = p or i
    for name, value in pairs(t) do
        if (type(value) == 'table') then
            print(p..tostring(name)..':')
            printTable(value, p..i)
        else
            print(p..tostring(name)..' = '..tostring(value))
        end
    end
end

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

print('image header infos:')
printTable(info)

jpegLib.decompress(cinfo);

info = jpegLib.getInfosDecompress(cinfo);

print('image infos:')
printTable(info)

--jpegLib.configureDecompress(cinfo, {scaleNum = 8, scaleDenom = 8});

local image = bufferLib.new(info.output.components * info.output.width * info.output.height);
jpegLib.decompress(cinfo, image);

fd:close()

print('image decompressed')
