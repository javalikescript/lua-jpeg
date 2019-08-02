local bufferLib = require('buffer')
local jpegLib = require('jpeg')

local info = {
    width = 320,
    height = 200,
    components = 3
};

local image = bufferLib.new(info.components * info.width * info.height);
bufferLib.memset(image, 255);
-- draw a black X
for y = 0, info.height - 1  do
    local x = math.floor(y * info.width / info.height)
    bufferLib.byteset(image, (y * info.width + x) * info.components, 0, 0, 0);
    bufferLib.byteset(image, ((info.height - 1 - y) * info.width + x) * info.components, 0, 0, 0);
end

local filename = 'tmp.jpg'
local fd = io.open(filename, 'wb')

jpegLib.compress(jpegLib.newCompress(), image, info, function(data)
    --print('write('..tostring(#data)..')')
    fd:write(data)
end);

fd:close()

print('image compressed in '..filename);
