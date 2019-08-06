local jpegLib = require('jpeg')

local function rgbToYuv(imageUserdata, imageInfoTable)
    jpegLib.componentMatrix(imageUserdata, imageInfoTable, {
        0.29900, 0.58700, 0.11400,
        -0.16874, -0.33126, 0.50000,
        0.50000, -0.41869, -0.08131
    }, {0.5, 127.5, 127.5});
end

local function yuvToRgb(imageUserdata, imageInfoTable)
    jpegLib.componentMatrix(imageUserdata, imageInfoTable, {
        1, 0, 1.402,
        1, -0.34414, -0.71414,
        1, 1.772, 0
    }, {-179.456 + 0.5, 44.04992 + 91.40992 - 0.5, -226.816 + 0.5});
end

local function rotate(imageUserdata, imageInfoTable, rotateMode)
    local info = {
        width = imageInfoTable.height,
        height = imageInfoTable.width,
        components = imageInfoTable.components
    };
    local image = jpegLib.newBuffer(info.components * info.width * info.height);
    local _, err = jpegLib.rotate(imageUserdata, imageInfoTable, image, info, rotateMode or 1);
    if err then
        print('rotate failed due to '..tostring(err))
        return imageUserdata, imageInfoTable
    end
    return image, info
end

local function subsampleBilinear(imageUserdata, imageInfoTable, dividor)
    local info = {
        width = math.floor(imageInfoTable.width / dividor),
        height = math.floor(imageInfoTable.height / dividor),
        components = imageInfoTable.components
    };
    local image = jpegLib.newBuffer(info.components * info.width * info.height);
    local buffer = jpegLib.newBuffer(imageInfoTable.components * imageInfoTable.width * 2 * 8)
    local _, err = jpegLib.subsampleBilinear(imageUserdata, imageInfoTable, image, info, buffer);
    if err then
        print('subsampleBilinear failed due to '..tostring(err))
        return imageUserdata, imageInfoTable
    end
    return image, info
end

local cinfo = jpegLib.newDecompress();

local modifications = {
    rotate = true
}

if #arg > 0 then
    modifications = {}
    for _, v in ipairs(arg) do
        modifications[v] = true
    end
end

local modificationList = ''
for k in pairs(modifications) do
    modificationList = modificationList..' '..k
end
print('modifications:'..modificationList)

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

if modifications['scale'] then
    jpegLib.configureDecompress(cinfo, {scaleNum = 4, scaleDenom = 8});
end

jpegLib.startDecompress(cinfo);

info = jpegLib.getInfosDecompress(cinfo);

local image = jpegLib.newBuffer(info.output.components * info.output.width * info.output.height);
jpegLib.decompress(cinfo, image);

fd:close()

print('image decompressed')
print('image is '..tostring(info.output.width)..'x'..tostring(info.output.height)..'x'..tostring(info.output.components))

if modifications['rgb2yuv'] then
    rgbToYuv(image, info.output)
end
if modifications['yuv2rgb'] then
    yuvToRgb(image, info.output)
end
if modifications['rgb2bgr'] then
    jpegLib.componentSwap(image, info.output, {2, 1, 0});
end
if modifications['rotate'] then
    image, info.output = rotate(image, info.output)
end
if modifications['subsample'] then
    image, info.output = subsampleBilinear(image, info.output, 2)
end

local filename = 'tmp_transform.jpg'
fd = io.open(filename, 'wb')

cinfo = jpegLib.newCompress()

jpegLib.startCompress(cinfo, info.output, function(data)
    --print('write('..tostring(#data)..')')
    fd:write(data)
end)

--jpegLib.writeMarker(cinfo, 0xe1, buffer)

jpegLib.compress(cinfo, image);

fd:close()

print('image compressed in '..filename)

