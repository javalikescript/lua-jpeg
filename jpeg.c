//#define JLS_LUA_MOD_TRACE 1
#include "lua-compat/luamod.h"

#include <jpeglib.h>

#include <math.h>
#include <string.h>

#if LUA_VERSION_NUM < 503
#include "lua-compat/compat.h"
#endif

/*
The libjpeg supports 8-bit to 12-bit data precision, but this is a compile-time choice, here 8-bit only.

Pixels are stored by scanlines, with each scanline running from left to right.
The component values for each pixel are adjacent in the row; for example, R,G,B,R,G,B,R,G,B,... for 24-bit RGB color.

The JPEG standard itself is "color blind" and doesn't specify any particular color space.
*/


/*
********************************************************************************
* Lua reference structure and functions
********************************************************************************
*/

typedef struct LuaReferenceStruct {
	lua_State *state;
	int ref;
} LuaReference;

#define CLEAR_LUA_REFERENCE(_LR) \
	(_LR)->state = NULL; \
	(_LR)->ref = LUA_NOREF

#define TEST_LUA_REFERENCE(_LR) \
	(((_LR)->state != NULL) && ((_LR)->ref != LUA_NOREF))

static void initLuaReference(LuaReference *r) {
	if (r != NULL) {
		CLEAR_LUA_REFERENCE(r);
	}
}

static int isRegisteredLuaReference(LuaReference *r) {
	return (r != NULL) && TEST_LUA_REFERENCE(r);
}

static void registerLuaReference(LuaReference *r, lua_State *l) {
	if (r != NULL) {
		if (TEST_LUA_REFERENCE(r)) {
			luaL_unref(r->state, LUA_REGISTRYINDEX, r->ref);
		}
		if (l != NULL) {
			r->state = l;
			r->ref = luaL_ref(l, LUA_REGISTRYINDEX);
		} else {
			CLEAR_LUA_REFERENCE(r);
		}
	}
}

static void unregisterLuaReference(LuaReference *r) {
	if ((r != NULL) && TEST_LUA_REFERENCE(r)) {
		luaL_unref(r->state, LUA_REGISTRYINDEX, r->ref);
		CLEAR_LUA_REFERENCE(r);
	}
}


/*
********************************************************************************
* JPEG Structures
********************************************************************************
*/

typedef struct JpegCompressStruct {
	LuaReference destFn;
	LuaReference buffer;
	unsigned long bytesPerRow;
	struct jpeg_error_mgr errormgr;
	struct jpeg_compress_struct cinfo;
	struct jpeg_destination_mgr destmgr;
} JpegCompress;

typedef struct JpegDecompressStruct {
	LuaReference srcFn;
	LuaReference buffer;
	int runStep;
	unsigned long bytesPerRow;
	struct jpeg_error_mgr errormgr;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_source_mgr srcmgr;
} JpegDecompress;

#define JPEG_COMPRESS_PTR(_cp) \
	((JpegCompress *) ((char *) (_cp) - offsetof(JpegCompress, cinfo)))

#define JPEG_DECOMPRESS_PTR(_cp) \
	((JpegDecompress *) ((char *) (_cp) - offsetof(JpegDecompress, cinfo)))

static const char *JCS_OPTIONS[] = { "UNKNOWN", "RGB", "sRGB", "YUV", "YCbCr", "GRAYSCALE", NULL };
static const int JCS_VALUES[] = { JCS_UNKNOWN, JCS_RGB, JCS_RGB, JCS_YCbCr, JCS_YCbCr, JCS_GRAYSCALE };


/*
********************************************************************************
* Image Structure
********************************************************************************
*/

// The PixmapInfo structure contains all information to store an image into a buffer
typedef struct PixmapInfoStruct {
	unsigned long width;
	unsigned long height;
	int components; // number of components per pixel
	//int bitsPerComponent; // default to 8
	//int bitsPerPixel; // default to 24
	//int pixelBitAlignment;
	unsigned long bytesPerRow; // default to components * width
	//int rowByteAlignment; // In the BMP format, every scanline is DWORD-aligned, 4 bytes, 32-bit.
	//int colorSpace;
	//int orientation;
} PixmapInfo;


#define MAX_PIXEL_COMPONENTS 5

#define MAX_SQUARE_COMPONENTS 25

#define FIX_BYTE(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))


/*
********************************************************************************
* Lua helper functions
********************************************************************************
*/

/*static int getBooleanField(lua_State *l, int i, const char *k, int def) {
	int v;
	lua_getfield(l, i, k);
	if (lua_isboolean(l, -1)) {
		v = (int) lua_toboolean(l, -1);
	} else {
		v = def;
	}
	lua_pop(l, 1);
	return v;
}*/

/*static int getLength(lua_State *l, int i) {
	int v;
	lua_len(l, i);
	if (lua_isinteger(l, -1)) {
		v = (int) lua_tointeger(l, -1);
	} else {
		v = 0;
	}
	lua_pop(l, 1);
	return v;
}*/

static int getIntegerField(lua_State *l, int i, const char *k, int def) {
	int v;
	lua_getfield(l, i, k);
	if (lua_isinteger(l, -1)) {
		v = (int) lua_tointeger(l, -1);
	} else {
		v = def;
	}
	lua_pop(l, 1);
	return v;
}

static long getLongField(lua_State *l, int i, const char *k, long def) {
	long v;
	lua_getfield(l, i, k);
	if (lua_isinteger(l, -1)) {
		v = (long) lua_tointeger(l, -1);
	} else {
		v = def;
	}
	lua_pop(l, 1);
	return v;
}

static int checkOptionField(lua_State *l, int i, const char *k, const char *def, const char *const *options, const int *values) {
	int value;
	lua_getfield(l, i, k);
	if (lua_isinteger(l, -1)) {
		value = luaL_checkinteger(l, -1);
		// TODO check if the value is ok or returns default value
	} else {
		value = values[luaL_checkoption(l, -1, def, options)];
	}
	lua_pop(l, 1);
	trace("checkOptionField() => %d\n", value);
	return value;
}

static const char * getOptionField(int value, int def, const char *const *options, const int *values) {
	int i = 0;
	const char *d = NULL;
	for (;;) {
		if (options[i] == NULL) {
			break;
		}
		if (value == values[i]) {
			return options[i];
		}
		if ((d == NULL) && (def == values[i])) {
			d = options[i];
		}
		i++;
	}
	if (d == NULL) {
		d = options[0];
	}
	return d;
}

#define SET_OPT_OPTION_FIELD(_LS, _IDX, _VAR, _NAME, _OPTIONS, _VALUES) \
	lua_getfield(_LS, _IDX, _NAME); \
	if (lua_isinteger(_LS, -1)) { \
		_VAR = (int) lua_tointeger(_LS, -1); \
	} else if (lua_isstring(_LS, -1)) { \
		_VAR = _VALUES[luaL_checkoption(_LS, -1, NULL, _OPTIONS)]; \
	} \
	lua_pop(_LS, 1)

static void getPixmapInfoFromTableField(lua_State *l, int i, PixmapInfo *pi) {
	pi->width = getLongField(l, i, "width", 0);
	pi->height = getLongField(l, i, "height", 0);
	pi->components = getIntegerField(l, i, "components", 3); // # of color components per pixel, 1 or 3
	unsigned long bytesPerRowMin = pi->components * pi->width;
	pi->bytesPerRow = getIntegerField(l, i, "bytesPerRow", bytesPerRowMin);
	if (pi->bytesPerRow < bytesPerRowMin) {
		pi->bytesPerRow = bytesPerRowMin;
	}
}


/*
********************************************************************************
* libjpeg functions
********************************************************************************
*/

static void luajpeg_flush_buffer(JpegCompress *jc, size_t freeInBuffer, int updateDest) {
	trace("luajpeg_flush_buffer() freeInBuffer: %d\n", freeInBuffer);
	lua_State *l = jc->buffer.state;
	lua_rawgeti(l, LUA_REGISTRYINDEX, jc->buffer.ref);
	char *bufferData = (char *)lua_touserdata(l, -1);
	size_t bufferSize = lua_rawlen(l, -1);
	lua_pop(l, 1);
	if (freeInBuffer > bufferSize) {
		freeInBuffer = bufferSize;
	}
	size_t count = bufferSize - freeInBuffer;
	l = jc->destFn.state;
	lua_rawgeti(l, LUA_REGISTRYINDEX, jc->destFn.ref);
	lua_pushlstring(l, bufferData, count);
	if (lua_pcall(l, 1, 0, 0) != 0) {
		trace("luajpeg_flush_buffer(#%d) => Failed\n", count); // TODO
	}
	if (updateDest) {
		jc->cinfo.dest->next_output_byte = (JOCTET *) bufferData;
		jc->cinfo.dest->free_in_buffer = bufferSize;
	} else {
		jc->cinfo.dest->next_output_byte = NULL;
		jc->cinfo.dest->free_in_buffer = 0;
	}
}

static void luajpeg_set_source_buffer(JpegDecompress *jd, lua_State *l) {
	const char *bufferData = NULL;
	size_t bufferSize = 0;
	trace("luajpeg_set_source_buffer()\n");
	if (lua_isstring(l, -1)) {
		bufferData = lua_tolstring(l, -1, &bufferSize);
		registerLuaReference(&jd->buffer, l);
	}
	jd->cinfo.src->next_input_byte = (const JOCTET *)bufferData;
	jd->cinfo.src->bytes_in_buffer = bufferSize;
}


METHODDEF(void)
luajpeg_error_exit (j_common_ptr cinfo)
{
	trace("luajpeg_error_exit()\n");
	//cinfo->is_decompressor
	(*cinfo->err->output_message) (cinfo);
}


METHODDEF(void)
luajpeg_init_destination (j_compress_ptr cinfo)
{
	trace("luajpeg_init_destination()\n");
	JpegCompress *jc = JPEG_COMPRESS_PTR(cinfo);
	lua_State *l = jc->buffer.state;
	lua_rawgeti(l, LUA_REGISTRYINDEX, jc->buffer.ref);
	cinfo->dest->next_output_byte = (JOCTET *)lua_touserdata(l, -1);
	cinfo->dest->free_in_buffer = lua_rawlen(l, -1);
	lua_pop(l, 1);
}

METHODDEF(boolean)
luajpeg_empty_output_buffer (j_compress_ptr cinfo)
{
	trace("luajpeg_empty_output_buffer()\n");
	JpegCompress *jc = JPEG_COMPRESS_PTR(cinfo);
	// ? use the saved start address and buffer length
	luajpeg_flush_buffer(jc, 0, TRUE);
	return TRUE;
}

METHODDEF(void)
luajpeg_term_destination (j_compress_ptr cinfo)
{
	trace("luajpeg_term_destination()\n");
	JpegCompress *jc = JPEG_COMPRESS_PTR(cinfo);
	luajpeg_flush_buffer(jc, cinfo->dest->free_in_buffer, FALSE);
}


METHODDEF(void)
luajpeg_source_no_operation (j_decompress_ptr cinfo)
{
	trace("luajpeg_source_no_operation()\n");
}


METHODDEF(boolean)
luajpeg_fill_input_buffer (j_decompress_ptr cinfo)
{
	trace("luajpeg_fill_input_buffer()\n");
	JpegDecompress *jd = JPEG_DECOMPRESS_PTR(cinfo);
	if ((jd->srcFn.state == NULL) || (jd->srcFn.ref == LUA_NOREF)) {
		return FALSE; // to indicate I/O suspension
	}
	lua_State *l = jd->srcFn.state;
	lua_rawgeti(l, LUA_REGISTRYINDEX, jd->srcFn.ref);
	if (lua_pcall(l, 0, 1, 0) != 0) {
		trace("fillBuffer() => Failed\n"); // TODO
	} else {
		luajpeg_set_source_buffer(jd, l);
	}
	return TRUE;
}

METHODDEF(void)
luajpeg_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	trace("luajpeg_skip_input_data(%d)\n", num_bytes);
	if (num_bytes > 0) {
		while (num_bytes > (long) cinfo->src->bytes_in_buffer) {
			num_bytes -= (long) cinfo->src->bytes_in_buffer;
			if (!luajpeg_fill_input_buffer(cinfo)) {
				// TODO record the additional skip distance somewhere else
				cinfo->src->bytes_in_buffer = 0;
				return;
			}
		}
		cinfo->src->next_input_byte += (size_t) num_bytes;
		cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
	}
}


/*
********************************************************************************
* JPEG Decompress functions
********************************************************************************
*/

static int luajpeg_decompress_new(lua_State *l) {
	JpegDecompress *jd = (JpegDecompress *)lua_newuserdata(l, sizeof(JpegDecompress));

	jd->cinfo.err = jpeg_std_error(&jd->errormgr);
	jd->cinfo.err->error_exit = luajpeg_error_exit;

	trace("jpeg_create_decompress()\n");
	jpeg_create_decompress(&jd->cinfo);

	// set source manager
	jd->cinfo.src = &jd->srcmgr;
	jd->srcmgr.init_source = luajpeg_source_no_operation;
	jd->srcmgr.fill_input_buffer = luajpeg_fill_input_buffer;
	jd->srcmgr.skip_input_data = luajpeg_skip_input_data;
	jd->srcmgr.resync_to_restart = jpeg_resync_to_restart; /* use default method */
	jd->srcmgr.term_source = luajpeg_source_no_operation;

	jd->srcmgr.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
	jd->srcmgr.next_input_byte = NULL; /* until buffer loaded */

	initLuaReference(&jd->buffer);
	initLuaReference(&jd->srcFn);

	jd->runStep = 0;

	luaL_getmetatable(l, "jpeg_decompress");
	lua_setmetatable(l, -2);
	return 1;
}

static int luajpeg_decompress_fill_source(lua_State *l) {
	trace("luajpeg_decompress_fill_source()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");
	if (lua_isstring(l, 2)) {
		lua_pushvalue(l, 2);
		luajpeg_set_source_buffer(jd, l);
	} else if (lua_isfunction(l, 2)) {
		lua_pushvalue(l, 2);
		registerLuaReference(&jd->srcFn, l);
	} else {
		unregisterLuaReference(&jd->buffer);
		unregisterLuaReference(&jd->srcFn);
	}
	return 0;
}

static int luajpeg_decompress_read_header(lua_State *l) {
	trace("luajpeg_decompress_read_header()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");

	if (jd->runStep == 0) {
		jd->runStep++;
	}
	trace("jpeg_read_header()\n");
	if (jpeg_read_header(&jd->cinfo, TRUE) == JPEG_SUSPENDED) {
		lua_pushnil(l);
		lua_pushstring(l, "suspended");
		return 2;
	}
	jd->runStep++;

	trace("width: %d\n", jd->cinfo.image_width);
	trace("height: %d\n", jd->cinfo.image_height);
	trace("colorSpace: %d\n", jd->cinfo.jpeg_color_space);

	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.image_width);
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.image_height);
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.jpeg_color_space, JCS_UNKNOWN, JCS_OPTIONS, JCS_VALUES));
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.num_components);

	return 1;
}

static int luajpeg_decompress_configure(lua_State *l) {
	trace("luajpeg_decompress_configure()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");

	if (lua_istable(l, 2)) {
		if (jd->runStep == 2) {
			/*
			* Scale the image by the fraction scale_num/scale_denom.
			* Currently, the supported scaling ratios are M/N with all M from 1 to 16,
			* where N is the source DCT size, which is 8 for baseline JPEG.
			*/
			SET_OPT_INTEGER_FIELD(l, 2, jd->cinfo.scale_num, "scaleNum");
			SET_OPT_INTEGER_FIELD(l, 2, jd->cinfo.scale_denom, "scaleDenom");

			/*
			* Output color space. jpeg_read_header() sets an appropriate default
			* based on jpeg_color_space; typically it will be RGB or grayscale.
			* The application can change this field to request output in a different
			* colorspace.
			*/
			SET_OPT_OPTION_FIELD(l, 2, jd->cinfo.out_color_space, "colorSpace", JCS_OPTIONS, JCS_VALUES);

			SET_OPT_NUMBER_FIELD(l, 2, jd->cinfo.output_gamma, "gamma");
		}

		SET_OPT_INTEGER_FIELD(l, 2, jd->bytesPerRow, "bytesPerRow");
	}
	return 0;
}

static int luajpeg_decompress_start(lua_State *l) {
	trace("luajpeg_decompress_start()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");

	if (jd->runStep == 2) {
		jd->runStep++;
	}
	trace("jpeg_start_decompress()\n");
	if (! jpeg_start_decompress(&jd->cinfo)) {
		lua_pushnil(l);
		lua_pushstring(l, "suspended");
		return 2;
	}
	jd->runStep++;

	// After this call, the final output image dimensions, including any requested scaling, are available in the JPEG object
	trace("width: %d\n", jd->cinfo.output_width);
	trace("height: %d\n", jd->cinfo.output_height);
	trace("color_space: %d\n", jd->cinfo.out_color_space);
	trace("components: %d\n", jd->cinfo.output_components);
	trace("gamma: %f\n", jd->cinfo.output_gamma);

	jd->bytesPerRow = jd->cinfo.output_width * jd->cinfo.output_components;
}

static int luajpeg_decompress_get_infos(lua_State *l) {
	trace("luajpeg_decompress_get_infos()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");

	lua_newtable(l);

	lua_pushstring(l, "image");
	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.image_width);
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.image_height);
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.jpeg_color_space, JCS_UNKNOWN, JCS_OPTIONS, JCS_VALUES));
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.num_components);
	lua_rawset(l, -3);

	lua_pushstring(l, "output");
	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.output_width);
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.output_height);
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.out_color_space, JCS_UNKNOWN, JCS_OPTIONS, JCS_VALUES));
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.output_components);
	SET_TABLE_KEY_NUMBER(l, "gamma", jd->cinfo.output_gamma);

	SET_TABLE_KEY_INTEGER(l, "scaleNum", jd->cinfo.scale_num);
	SET_TABLE_KEY_INTEGER(l, "scaleDenom", jd->cinfo.scale_denom);

	SET_TABLE_KEY_INTEGER(l, "bytesPerRow", jd->bytesPerRow);
	lua_rawset(l, -3);

	return 1;
}

static int luajpeg_decompress_run(lua_State *l) {
	trace("luajpeg_decompress_run()\n");
	JpegDecompress *jd = (JpegDecompress *)luaL_checkudata(l, 1, "jpeg_decompress");

	trace("step: %d\n", jd->runStep);
	if (jd->runStep == 4) {
		jd->runStep++;
	}
	if (jd->runStep == 5) {
		// we may want to allocate a buffer and return it as a string or userdata
		luaL_checktype(l, 2, LUA_TUSERDATA);
		size_t imageLength = lua_rawlen(l, 2);
		char *imageData = (char *)lua_touserdata(l, 2);
		trace("bytesPerRow: %d\n", jd->bytesPerRow);
		size_t output_size = (size_t) (jd->bytesPerRow * jd->cinfo.output_height);
		if (imageLength < output_size) {
			lua_pushnil(l);
			lua_pushstring(l, "image buffer too small");
			return 2;
		}
		JSAMPROW row_pointer[1];
		while (jd->cinfo.output_scanline < jd->cinfo.output_height) {
			row_pointer[0] = (JSAMPROW) (imageData + jd->cinfo.output_scanline * jd->bytesPerRow);
			if (jpeg_read_scanlines(&jd->cinfo, row_pointer, 1) == 0) {
				lua_pushnil(l);
				lua_pushstring(l, "suspended");
				return 2;
			}
		}
		jd->runStep++;
	}
	if (jd->runStep == 6) {
		trace("jpeg_finish_decompress()\n");
		if (! jpeg_finish_decompress(&jd->cinfo)) {
			lua_pushnil(l);
			lua_pushstring(l, "suspended");
			return 2;
		}
		jd->runStep = 0;
	}
	return 0;
}

static int luajpeg_decompress_gc(lua_State *l) {
	JpegDecompress *jd = (JpegDecompress *)luaL_testudata(l, 1, "jpeg_decompress");
	if (jd != NULL) {
		trace("luajpeg_destroy_decompress()\n");
		jpeg_destroy_decompress(&jd->cinfo);
		unregisterLuaReference(&jd->buffer);
		unregisterLuaReference(&jd->srcFn);
	}
	return 0;
}


/*
********************************************************************************
* JPEG Compress functions
********************************************************************************
*/

static int luajpeg_compress_new(lua_State *l) {
	JpegCompress *jc = (JpegCompress *)lua_newuserdata(l, sizeof(JpegCompress));

	jc->cinfo.err = jpeg_std_error(&jc->errormgr);
	jc->cinfo.err->error_exit = luajpeg_error_exit;

	trace("jpeg_create_compress()\n");
	jpeg_create_compress(&jc->cinfo);

	// set destination manager
	jc->cinfo.dest = &jc->destmgr;
	jc->destmgr.init_destination = luajpeg_init_destination;
	jc->destmgr.empty_output_buffer = luajpeg_empty_output_buffer;
	jc->destmgr.term_destination = luajpeg_term_destination;

	initLuaReference(&jc->buffer);
	initLuaReference(&jc->destFn);

	luaL_getmetatable(l, "jpeg_compress");
	lua_setmetatable(l, -2);
	return 1;
}

static int luajpeg_compress_start(lua_State *l) {
	trace("luajpeg_compress_start()\n");
	JpegCompress *jc = (JpegCompress *)luaL_checkudata(l, 1, "jpeg_compress");

	PixmapInfo pi;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &pi);

	jc->cinfo.image_width = pi.width;
	jc->cinfo.image_height = pi.height;
	jc->cinfo.input_components = pi.components;
	// Color space of source image
	jc->cinfo.in_color_space = checkOptionField(l, 2, "colorSpace", "RGB", JCS_OPTIONS, JCS_VALUES);

	jc->bytesPerRow = pi.bytesPerRow;

	// quality 0-100, default 75, should use 50-95
	int quality = getIntegerField(l, 2, "quality", 75);

	luaL_checktype(l, 3, LUA_TFUNCTION);
	lua_pushvalue(l, 3);
	registerLuaReference(&jc->destFn, l);

	if (lua_isuserdata(l, 4) && !lua_islightuserdata(l, 4)) {
		lua_pushvalue(l, 4);
		registerLuaReference(&jc->buffer, l);
	} else {
		size_t bufferSize = (size_t) luaL_optinteger(l, 4, 0);
		if (bufferSize < 2048) {
			bufferSize = 2048;
		}
		(void) lua_newuserdata(l, bufferSize);
		registerLuaReference(&jc->buffer, l);
	}

	trace("jpeg_set_defaults()\n");
	jpeg_set_defaults(&jc->cinfo);

	//jpeg_set_colorspace(&jc->cinfo, JCS_RGB);

	trace("jpeg_set_quality()\n");
	jpeg_set_quality(&jc->cinfo, quality, TRUE);

	trace("jpeg_start_compress()\n");
	jpeg_start_compress(&jc->cinfo, TRUE);

	return 0;
}

static int luajpeg_compress_writeMarker(lua_State *l) {
	trace("luajpeg_compress_writeMarker()\n");
	JpegCompress *jc = (JpegCompress *)luaL_checkudata(l, 1, "jpeg_compress");

	size_t markerLength = 0;
	const JOCTET *markerData = NULL;

	if (!isRegisteredLuaReference(&jc->destFn)) {
		lua_pushnil(l);
		lua_pushstring(l, "compress not started");
		return 2;
	}

	unsigned long marker = luaL_checkinteger(l, 2);

	if (lua_isstring(l, 3)) {
		markerData = (const JOCTET *)luaL_checklstring(l, 3, &markerLength);
	} else {
		luaL_checktype(l, 3, LUA_TUSERDATA);
		markerLength = lua_rawlen(l, 3);
		markerData = (const JOCTET *)lua_touserdata(l, 3);
	}
	jpeg_write_marker(&jc->cinfo, marker, markerData, markerLength);

	return 0;
}

static int luajpeg_compress_run(lua_State *l) {
	trace("luajpeg_compress_run()\n");
	JpegCompress *jc = (JpegCompress *)luaL_checkudata(l, 1, "jpeg_compress");

	size_t imageLength = 0;
	const char *imageData = NULL;

	if (!isRegisteredLuaReference(&jc->destFn)) {
		lua_pushnil(l);
		lua_pushstring(l, "compress not started");
		return 2;
	}

	if (lua_isstring(l, 2)) {
		imageData = luaL_checklstring(l, 2, &imageLength);
	} else {
		luaL_checktype(l, 2, LUA_TUSERDATA);
		imageLength = lua_rawlen(l, 2);
		imageData = (char *)lua_touserdata(l, 2);
	}
	
	size_t minImageLength = (size_t) (jc->bytesPerRow * jc->cinfo.image_height);
	if (imageLength < minImageLength) {
		lua_pushnil(l);
		lua_pushstring(l, "image buffer too small");
		return 2;
	}

	JSAMPROW row_pointer[1];
	trace("bytesPerRow: %d\n", jc->bytesPerRow);
	while (jc->cinfo.next_scanline < jc->cinfo.image_height) {
		row_pointer[0] = (JSAMPROW) (imageData + jc->cinfo.next_scanline * jc->bytesPerRow);
		(void) jpeg_write_scanlines(&jc->cinfo, row_pointer, 1);
	}

	trace("jpeg_finish_compress()\n");
	jpeg_finish_compress(&jc->cinfo);

	unregisterLuaReference(&jc->destFn);
	unregisterLuaReference(&jc->buffer);
	return 0;
}

static int luajpeg_compress_gc(lua_State *l) {
	JpegCompress *jc = (JpegCompress *)luaL_testudata(l, 1, "jpeg_compress");
	if (jc != NULL) {
		trace("jpeg_destroy_compress()\n");
		jpeg_destroy_compress(&jc->cinfo);
		unregisterLuaReference(&jc->buffer);
		unregisterLuaReference(&jc->destFn);
	}
	return 0;
}


/*
********************************************************************************
* Image manipulation functions
********************************************************************************
*/

static int luajpeg_componentMatrix(lua_State *l) {
	trace("luajpeg_componentMatrix()\n");
	luaL_checktype(l, 1, LUA_TUSERDATA);
	size_t imageLength = lua_rawlen(l, 1);
	unsigned char *imageData = (unsigned char *)lua_touserdata(l, 1);
	
	PixmapInfo pi;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &pi);
	if (pi.components > MAX_PIXEL_COMPONENTS) {
		lua_pushnil(l);
		lua_pushstring(l, "too much components");
		return 2;
	}
	size_t image_size = (size_t) (pi.bytesPerRow * pi.height);
	if (imageLength < image_size) {
		lua_pushnil(l);
		lua_pushstring(l, "image buffer too small");
		return 2;
	}
	double work[MAX_PIXEL_COMPONENTS];
	double matrix[MAX_SQUARE_COMPONENTS];
	double delta[MAX_SQUARE_COMPONENTS];
	int matrixLength = pi.components * pi.components;
	int i;
	for (i = 0; i < matrixLength; i++) {
		matrix[i] = 1.0;
	}
	for (i = 0; i < pi.components; i++) {
		delta[i] = 0.0;
	}
	if (lua_istable(l, 3)) {
		for (i = 0; i < matrixLength; i++) {
			if (lua_geti(l, 3, i) == LUA_TNUMBER) {
				matrix[i] = (int) lua_tonumber(l, -1);
			}
			lua_pop(l, 1);
		}
	}
	if (lua_istable(l, 4)) {
		for (i = 0; i < pi.components; i++) {
			if (lua_geti(l, 4, i) == LUA_TNUMBER) {
				delta[i] = (int) lua_tonumber(l, -1);
			}
			lua_pop(l, 1);
		}
	}
	int j, x, y, xoffset, yoffset;
    for (y = 0; y < pi.height; y++) {
        yoffset = y * pi.bytesPerRow;
        for (x = 0; x < pi.width; x++) {
            xoffset = yoffset + x * pi.components;
        	for (i = 0; i < pi.components; i++) {
        		work[i] = delta[i];
            	for (j = 0; j < pi.components; j++) {
            		work[i] += imageData[xoffset + j] * matrix[i * pi.components + j];
                }
            }
        	for (i = 0; i < pi.components; i++) {
    			imageData[xoffset + i] = FIX_BYTE(work[i]);
            }
        }
    }

	return 0;
}

static int luajpeg_componentSwap(lua_State *l) {
	trace("luajpeg_componentSwap()\n");
	luaL_checktype(l, 1, LUA_TUSERDATA);
	size_t imageLength = lua_rawlen(l, 1);
	unsigned char *imageData = (unsigned char *)lua_touserdata(l, 1);
	
	PixmapInfo pi;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &pi);
	if (pi.components > MAX_PIXEL_COMPONENTS) {
		lua_pushnil(l);
		lua_pushstring(l, "too much components");
		return 2;
	}
	size_t image_size = (size_t) (pi.bytesPerRow * pi.height);
	if (imageLength < image_size) {
		lua_pushnil(l);
		lua_pushstring(l, "image buffer too small");
		return 2;
	}
	unsigned char work[MAX_PIXEL_COMPONENTS];
	int indices[MAX_PIXEL_COMPONENTS];
	int i;
	for (i = 0; i < pi.components; i++) {
		indices[i] = pi.components - 1 - i;
	}
	if (lua_istable(l, 3)) {
		for (i = 0; i < pi.components; i++) {
			lua_geti(l, 3, i);
			if (lua_isinteger(l, -1)) {
				indices[i] = (int) lua_tointeger(l, -1);
			}
			lua_pop(l, 1);
		}
	}
    int x, y, xoffset, yoffset;
    for (y = 0; y < pi.height; y++) {
        yoffset = y * pi.bytesPerRow;
        for (x = 0; x < pi.width; x++) {
            xoffset = yoffset + x * pi.components;
        	for (i = 0; i < pi.components; i++) {
				work[i] = imageData[xoffset + indices[i]];
            }
        	for (i = 0; i < pi.components; i++) {
                imageData[xoffset + i] = work[i];
            }
        }
    }
	return 0;
}

static int luajpeg_convolve(lua_State *l) {
	trace("luajpeg_convolve()\n");
	luaL_checktype(l, 1, LUA_TUSERDATA);
	//size_t imageLength = lua_rawlen(l, 1);
	unsigned char *imageData = (unsigned char *)lua_touserdata(l, 1);
	
	PixmapInfo pi;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &pi);

	luaL_checktype(l, 3, LUA_TTABLE);
	int kernelLength = lua_rawlen(l, 3);

	luaL_checktype(l, 4, LUA_TUSERDATA);
	size_t bufferLength = lua_rawlen(l, 4);
	char *bufferData = (char *)lua_touserdata(l, 4);

	int componentStart = 0;
	int componentStop = pi.components - 1;
	int kernelWidth = -1;
	int kernelHeight = -1;
	int kernelX = -1;
	int kernelY = -1;

	if (lua_istable(l, 5)) {
		componentStart = getIntegerField(l, 5, "componentStart", componentStart);
		componentStop = getIntegerField(l, 5, "componentStop", componentStop);

		kernelWidth = getIntegerField(l, 5, "kernelWidth", kernelWidth);
		kernelHeight = getIntegerField(l, 5, "kernelHeight", kernelHeight);
		kernelX = getIntegerField(l, 5, "kernelX", kernelX);
		kernelY = getIntegerField(l, 5, "kernelY", kernelY);
	}
	if ((kernelWidth < 0) || (kernelHeight < 0)) {
		int s = sqrt(kernelLength);
		kernelWidth = kernelHeight = s;
	}
	if (kernelWidth * kernelHeight != kernelLength) {
		lua_pushnil(l);
		lua_pushstring(l, "invalid kernel argument");
		return 2;
	}
	if ((kernelX < 0) || (kernelY < 0)) {
		kernelX = kernelWidth / 2;
		kernelY = kernelHeight / 2;
	}

    int workSize = kernelY + 1;
    int sizeOfWork = workSize * pi.bytesPerRow * sizeof(unsigned char);
	int sizeOfKernel = kernelHeight * sizeof(double *) + kernelHeight * kernelWidth * sizeof(double);
	unsigned char *work = (unsigned char *)bufferData;
    double **kernel = (double **) (bufferData + sizeOfWork);

	trace("bufferLength: %d, min: %d\n", bufferLength, sizeOfWork + sizeOfKernel);
	if (bufferLength < sizeOfWork + sizeOfKernel) {
		lua_pushnil(l);
		lua_pushstring(l, "buffer too small");
		return 2;
	}
	trace("componentStart - componentStop: %d-%d\n", componentStart, componentStop);
	trace("kernelWidth x kernelHeight: %dx%d\n", kernelWidth, kernelHeight);
	trace("kernelX, kernelY: %d, %d\n", kernelX, kernelY);

	double kernelSum = 0;
    int i, j, k;
    //trace("kernel initialization");
    for (j = 0; j < kernelHeight; j++) {
    	kernel[j] = (double *)(((unsigned char *)kernel) + kernelHeight * sizeof(double *) + j * kernelWidth * sizeof(double));
        for (i = 0; i < kernelWidth; i++) {
			double d = 0.0;
			if (lua_geti(l, 3, 1 + j * kernelWidth + i) == LUA_TNUMBER) {
				d = (double) lua_tonumber(l, -1);
			}
			lua_pop(l, 1);
			kernelSum += kernel[j][i] = d;
            trace("kernel[%d][%d] = %f\n", j, i, kernel[j][i]);
        }
	}
    //trace("kernel initialized, sum = %f", kernelSum);
    unsigned char *pbits = imageData;
    int x, y;
    for (y = 0; y < pi.height; y++) {
        if (y >= workSize) {
        	int wy = y - workSize;
        	//trace("starting row %d, flushing row %d [%d]", y, wy, wy % workSize);
        	memcpy(pbits + wy * pi.bytesPerRow, work + (wy % workSize) * pi.bytesPerRow, pi.bytesPerRow);
        }
        for (x = 0; x < pi.width; x++) {
        	//int debug = (x == info.width / 2) && (y == info.height / 2);
            for (k = 0; k < pi.components; k++) {
            	if ((k < componentStart) || (k > componentStop)) {
            		work[(y % workSize) * pi.bytesPerRow + x * pi.components + k] = pbits[y * pi.bytesPerRow + x * pi.components + k];
            		continue;
            	} // else
            	double sum = 0;
            	double div = kernelSum;
            	for (j = 0; j < kernelHeight; j++) {
            		for (i = 0; i < kernelWidth; i++) {
            			int kx = x - kernelX + i;
            			int ky = y - kernelY + j;
            			if ((kx < 0) || (ky < 0) || (ky >= pi.height) || (kx >= pi.width)) {
            				div -= kernel[j][i];
            			} else {
            				/*if (debug)
            					trace("%d x %f(kernel[%d][%d]) = %f", pbits[ky * pi.bytesPerRow + kx * pi.components + k],
            							kernel[j][i], j, i, pbits[ky * pi.bytesPerRow + kx * pi.components + k] * kernel[j][i]);*/
            				sum += pbits[ky * pi.bytesPerRow + kx * pi.components + k] * kernel[j][i];
            			}
            		}
            	}
            	int res;
            	if (div != 0.0) {
            		res = sum / div;
            	} else {
            		res = 255;
            		//trace("div = %f", div);
            	}
            	if (res < 0) {
            		res = 0;
            	} else if (res > 255) {
            		res = 255;
            	}
            	work[(y % workSize) * pi.bytesPerRow + x * pi.components + k] = res;
            	/*if (debug) {
            		trace("%f / %f = %f => %d", sum, div, sum / div, res);
            	}*/
            }
        }
    }
    for (j = 0; j < workSize; j++) {
    	int wy = y - workSize;
		//trace("flushing row %d [%d]", wy, wy % workSize);
    	memcpy(pbits + wy * pi.bytesPerRow, work + (wy % workSize) * pi.bytesPerRow, pi.bytesPerRow);
    	y++;
    }

	return 0;
}

static const char *ROTATE_OPTIONS[] = { "right", "180", "left", "flip-horizontal", "flip-vertical", NULL };
static const int ROTATE_VALUES[] = { 1, 2, 3, 4, 5 };

static int luajpeg_rotate(lua_State *l) {
	trace("luajpeg_rotate()\n");

	luaL_checktype(l, 1, LUA_TUSERDATA);
	//size_t srcImageLength = lua_rawlen(l, 1);
	unsigned char *srcImageData = (unsigned char *)lua_touserdata(l, 1);
	
	PixmapInfo srcInfo;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &srcInfo);

	luaL_checktype(l, 3, LUA_TUSERDATA);
	//size_t dstImageLength = lua_rawlen(l, 3);
	unsigned char *dstImageData = (unsigned char *)lua_touserdata(l, 3);
	
	PixmapInfo dstInfo;
	luaL_checktype(l, 4, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 4, &dstInfo);

	if (srcInfo.components != dstInfo.components) {
		lua_pushnil(l);
		lua_pushstring(l, "components differ");
		return 2;
	}

	int rc = 1;
	if (lua_isinteger(l, 5)) {
		rc = lua_tointeger(l, 5);
	} else if (lua_isstring(l, 5)) {
		rc = ROTATE_VALUES[luaL_checkoption(l, 5, NULL, ROTATE_OPTIONS)];
	}

	if ((rc == 1) || (rc == 3)) {
		if ((srcInfo.width != dstInfo.height) || (srcInfo.height != dstInfo.width)) {
			lua_pushnil(l);
			lua_pushstring(l, "incompatible source and destination sizes");
			return 2;
		}
	} else if ((rc == 2) || (rc == 4) || (rc == 5)) {
		if ((srcInfo.width != dstInfo.width) || (srcInfo.height != dstInfo.height)) {
			lua_pushnil(l);
			lua_pushstring(l, "incompatible source and destination sizes");
			return 2;
		}
	} else {
		lua_pushnil(l);
		lua_pushstring(l, "unsupported rotation");
		return 2;
	}

    int y, x, b;
    int xoffset, yoffset, xdoffset, ydoffset;
    for (y = 0; y < srcInfo.height; y++) {
    	yoffset = y * srcInfo.bytesPerRow;
        for (x = 0; x < srcInfo.width; x++) {
        	xoffset = yoffset + x * srcInfo.components;
        	switch (rc) {
        	case 1: // rotate right 90
				ydoffset = x * dstInfo.bytesPerRow;
				xdoffset = ydoffset + (srcInfo.height - y - 1) * dstInfo.components;
        		break;
        	case 2: // rotate 180
				ydoffset = (srcInfo.height - y - 1) * dstInfo.bytesPerRow;
				xdoffset = ydoffset + (srcInfo.width - x - 1) * dstInfo.components;
        		break;
        	case 3: // rotate left 90
				ydoffset = (srcInfo.width - x - 1) * dstInfo.bytesPerRow;
				xdoffset = ydoffset + y * dstInfo.components;
        		break;
        	case 4: // flip horizontal mirror
				ydoffset = y * dstInfo.bytesPerRow;
				xdoffset = ydoffset + (srcInfo.width - x - 1) * dstInfo.components;
        		break;
        	case 5: // flip vertical mirror
				ydoffset = (srcInfo.height - y - 1) * dstInfo.bytesPerRow;
				xdoffset = ydoffset + x * dstInfo.components;
        		break;
        	}
            for (b = 0; b < srcInfo.components; b++) {
        		dstImageData[xdoffset + b] = srcImageData[xoffset + b];
            }
        }
    }
	return 0;
}

static int luajpeg_subsampleBilinear(lua_State *l) {
	trace("luajpeg_subsampleBilinear()\n");

	luaL_checktype(l, 1, LUA_TUSERDATA);
	//size_t srcImageLength = lua_rawlen(l, 1);
	unsigned char *srcImageData = (unsigned char *)lua_touserdata(l, 1);
	
	PixmapInfo srcInfo;
	luaL_checktype(l, 2, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 2, &srcInfo);

	luaL_checktype(l, 3, LUA_TUSERDATA);
	//size_t dstImageLength = lua_rawlen(l, 3);
	unsigned char *dstImageData = (unsigned char *)lua_touserdata(l, 3);
	
	PixmapInfo dstInfo;
	luaL_checktype(l, 4, LUA_TTABLE);
	getPixmapInfoFromTableField(l, 4, &dstInfo);

	luaL_checktype(l, 5, LUA_TUSERDATA);
	size_t bufferLength = lua_rawlen(l, 5);
	char *bufferData = (char *)lua_touserdata(l, 5);
	
	// components per row
	int cpr = srcInfo.width * srcInfo.components;

	if (srcInfo.components != dstInfo.components) {
		lua_pushnil(l);
		lua_pushstring(l, "components differ");
		return 2;
	}
    if ((srcInfo.width <= dstInfo.width) || (srcInfo.height <= dstInfo.height)) {
		lua_pushnil(l);
		lua_pushstring(l, "invalid image sizes for subsampling");
		return 2;
    }
	size_t minBufferLength = cpr * sizeof(unsigned long) * 2;
	if (bufferLength < minBufferLength) {
		lua_pushnil(l);
		lua_pushfstring(l, "buffer too small (%d < %d)", bufferLength, minBufferLength);
		return 2;
	}

	unsigned long *work = (unsigned long *)bufferData;
	unsigned long *divs = work + cpr;

    int i, x, y = 0, xoffset, yoffset;
    int xd = 0, yd = 0, woffset, xdoffset, ydoffset;
    int curr, next = 0, nyd = 0, cp, np, yp, ydd, ypass;
    int xcurr, xnext = 0, nxd = 0, xcp, xnp, xp, xdd, xpass;
    //while (y < srcInfo.height) {
	for (i = 0; i < cpr; i++) {
		work[i] = divs[i] = 0;
    }
    for (y = 0; y < srcInfo.height; y++) {
    	yoffset = y * srcInfo.bytesPerRow;
    	curr = next;
    	next = (y + 1) * dstInfo.height * 100 / srcInfo.height;

        yd = nyd;
        nyd = next / 100;

        if ((nyd != yd) && (y + 1 < srcInfo.height)) {
        	// the destination row is going to change
        	cp = (100 - (curr % 100)) * 100 / (next - curr);
        	//np = (next % 100) * 100 / (next - curr);
        	np = 100 - cp;
        	ypass = 2;
        } else {
        	cp = np = 100;
        	ypass = 1;
        }
		yp = cp;
		ydd = yd;
        while (--ypass >= 0) {
            ydoffset = ydd * dstInfo.bytesPerRow;
            //trace("y %d => %d %d%%", y, ydd, yp);
			xnext = 0;
			nxd = 0;
	        for (x = 0; x < srcInfo.width; x++) {
	        	xoffset = yoffset + x * srcInfo.components;
	        	xcurr = xnext;
	        	xnext = (x + 1) * dstInfo.width * 100 / srcInfo.width;

	            xd = nxd;
	            nxd = xnext / 100;

	            if ((nxd != xd) && (x + 1 < srcInfo.width)) {
	            	// the destination column is going to change
	            	xcp = (100 - (xcurr % 100)) * 100 / (xnext - xcurr);
	            	//np = (next % 100) * 100 / (next - curr);
	            	xnp = 100 - xcp;
	            	xpass = 2;
	            } else {
	            	xcp = xnp = 100;
	            	xpass = 1;
	            }
	    		xp = xcp;
	    		xdd = xd;
	            while (--xpass >= 0) {
		            woffset = xdd * dstInfo.components;
					int percent = xp * yp / 100;
	            	//if ((y == 0) || (yp != 100)) jls_info("x %d => %d %d%% (%d%%)", x, xdd, xp, percent);
            		divs[woffset] += percent;
	            	for (i = 0; i < srcInfo.components; i++) {
	            		work[woffset + i] += srcImageData[xoffset + i] * percent / 100;
	                }
	    			xp = xnp;
	    			xdd = nxd;
	            }
	        }
	        if (nyd != ydd) {
	            for (xd = 0; xd < dstInfo.width; xd++) {
	            	xdoffset = ydoffset + xd * dstInfo.components;
	            	woffset = xd * dstInfo.components;
	            	for (i = 0; i < dstInfo.components; i++) {
	            		dstImageData[xdoffset + i] = work[woffset + i] * 100 / divs[woffset];
	            		work[woffset + i] = 0;
	                }
	            	divs[woffset] = 0;
	            }
	            //trace("row %d completed", yd);
	        }
			yp = np;
			ydd = nyd;
        }
    }
	return 0;
}


/*
********************************************************************************
* Buffer function
********************************************************************************
*/

static int luajpeg_buffer_new(lua_State *l) {
	size_t nbytes = 0;
	unsigned char *buffer = NULL;
	const char *src = NULL;
	if (lua_isinteger(l, 1)) {
		nbytes = luaL_checkinteger(l, 1);
	} else if (lua_isstring(l, 1)) {
		src = lua_tolstring(l, 1, &nbytes);
	} else if (lua_isuserdata(l, 1) && !lua_islightuserdata(l, 1)) {
		nbytes = lua_rawlen(l, 1);
		src = (const char *)lua_touserdata(l, 1);
	}
	trace("luajpeg_buffer_new() %d\n", nbytes);
	if (nbytes > 0) {
		buffer = (unsigned char *)lua_newuserdata(l, nbytes);
		if (src != NULL) {
			memcpy(buffer, src, nbytes);
		}
	} else {
		lua_pushnil(l);
	}
	return 1;
}

/*
********************************************************************************
* Module open function
********************************************************************************
*/

#define LUA_JPEG_VERSION "0.1"

LUALIB_API int luaopen_jpeg(lua_State *l) {
	trace("luaopen_jpeg()\n");

	luaL_newmetatable(l, "jpeg_decompress");
	lua_pushstring(l, "__gc");
	lua_pushcfunction(l, luajpeg_decompress_gc);
	lua_settable(l, -3);

	luaL_newmetatable(l, "jpeg_compress");
	lua_pushstring(l, "__gc");
	lua_pushcfunction(l, luajpeg_compress_gc);
	lua_settable(l, -3);

	luaL_Reg reg[] = {
		// Buffer
		{ "newBuffer", luajpeg_buffer_new },
		// JPEG Compress
		{ "newCompress", luajpeg_compress_new },
		{ "startCompress", luajpeg_compress_start },
		{ "writeMarker", luajpeg_compress_writeMarker },
		{ "compress", luajpeg_compress_run },
		// JPEG Decompress
		{ "newDecompress", luajpeg_decompress_new },
		{ "startDecompress", luajpeg_decompress_start },
		{ "fillSource", luajpeg_decompress_fill_source },
		{ "readHeader", luajpeg_decompress_read_header },
		{ "configureDecompress", luajpeg_decompress_configure },
		{ "getInfosDecompress", luajpeg_decompress_get_infos },
		{ "decompress", luajpeg_decompress_run },
		// Image manipulation
		{ "componentMatrix", luajpeg_componentMatrix },
		{ "componentSwap", luajpeg_componentSwap },
		{ "convolve", luajpeg_convolve },
		{ "rotate", luajpeg_rotate },
		{ "subsampleBilinear", luajpeg_subsampleBilinear },
		{ NULL, NULL }
	};
	lua_newtable(l);
	luaL_setfuncs(l, reg, 0);
	lua_pushliteral(l, "Lua jpeg");
	lua_setfield(l, -2, "_NAME");
	lua_pushfstring (l, "%s libjpeg %d.%d", LUA_JPEG_VERSION, JPEG_LIB_VERSION_MAJOR, JPEG_LIB_VERSION_MINOR);
	lua_setfield(l, -2, "_VERSION");
	trace("luaopen_jpeg() done\n");
	return 1;
}
