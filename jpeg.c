#include "luamod.h"

#include <jpeglib.h>

/*
The libjpeg supports 8-bit to 12-bit data precision, but this is a compile-time choice, here 8-bit only.
Pixels are stored by scanlines, with each scanline running from left to right.
The component values for each pixel are adjacent in the row; for example, R,G,B,R,G,B,R,G,B,... for 24-bit RGB color.
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

static void initLuaReference(LuaReference *r) {
	if (r != NULL) {
		r->state = NULL;
		r->ref = LUA_NOREF;
	}
}

static void registerLuaReference(LuaReference *r, lua_State *l) {
	if ((r != NULL) && (l != NULL)) {
		if ((r->state != NULL) && (r->ref != LUA_NOREF)) {
			luaL_unref(r->state, LUA_REGISTRYINDEX, r->ref);
		}
		r->state = l;
		r->ref = luaL_ref(l, LUA_REGISTRYINDEX);
	}
}

static void unregisterLuaReference(LuaReference *r) {
	if ((r != NULL) && (r->state != NULL) && (r->ref != LUA_NOREF)) {
		luaL_unref(r->state, LUA_REGISTRYINDEX, r->ref);
		r->state = NULL;
		r->ref = LUA_NOREF;
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
	struct jpeg_error_mgr errormgr;
	struct jpeg_compress_struct cinfo;
	struct jpeg_destination_mgr destmgr;
} JpegCompress;

typedef struct JpegDecompressStruct {
	LuaReference srcFn;
	LuaReference buffer;
	int runStep;
	int bytesPerRow; // TODO replace by row padding which gracefully default to 0
	struct jpeg_error_mgr errormgr;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_source_mgr srcmgr;
} JpegDecompress;

#define JPEG_COMPRESS_PTR(_cp) \
	((JpegCompress *) ((char *) (_cp) - offsetof(JpegCompress, cinfo)))

#define JPEG_DECOMPRESS_PTR(_cp) \
	((JpegDecompress *) ((char *) (_cp) - offsetof(JpegDecompress, cinfo)))

static const char *CS_OPTIONS[] = { "UNKNOWN", "RGB", "GRAYSCALE", NULL };
static const int CS_VALUES[] = { JCS_UNKNOWN, JCS_RGB, JCS_GRAYSCALE };

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

#define SET_OPT_INTEGER_FIELD(_LS, _IDX, _VAR, _NAME) \
	lua_getfield(_LS, _IDX, _NAME); \
	if (lua_isinteger(_LS, -1)) { \
		_VAR = (int) lua_tointeger(_LS, -1); \
	} \
	lua_pop(_LS, 1)

#define SET_OPT_NUMBER_FIELD(_LS, _IDX, _VAR, _NAME) \
	lua_getfield(_LS, _IDX, _NAME); \
	if (lua_isnumber(_LS, -1)) { \
		_VAR = (double) lua_tonumber(_LS, -1); \
	} \
	lua_pop(_LS, 1)

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
		// TODO check if the value is ok
	} else {
		value = values[luaL_checkoption(l, -1, def, options)];
	}
	lua_pop(l, 1);
	trace("checkOptionField() => %d\n", value);
	return value;
}

static const char * getOptionField(int value, int def, const char *const *options, const int *values) {
	int i = 0;
	const char *d = options[0];
	for (;;) {
		if (options[i] == NULL) {
			break;
		}
		if (value == values[i]) {
			return options[i];
		}
		if (def == values[i]) {
			d = options[i];
		}
		i++;
	}
	return d;
}


/*
********************************************************************************
* libjpeg functions
********************************************************************************
*/

static void luajpeg_flush_buffer(JpegCompress *jc, size_t freeInBuffer, int updateDest) {
	trace("luajpeg_flush_buffer(%d)", count);
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
		trace("luajpeg_flush_buffer(#%d) => Failed", count); // TODO
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
	trace("luajpeg_set_source_buffer()");
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
	trace("luajpeg_error_exit()");
	//cinfo->is_decompressor
	(*cinfo->err->output_message) (cinfo);
}


METHODDEF(void)
luajpeg_init_destination (j_compress_ptr cinfo)
{
	trace("luajpeg_init_destination()");
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
	trace("luajpeg_empty_output_buffer()");
	JpegCompress *jc = JPEG_COMPRESS_PTR(cinfo);
	// ? use the saved start address and buffer length
	luajpeg_flush_buffer(jc, 0, TRUE);
	return TRUE;
}

METHODDEF(void)
luajpeg_term_destination (j_compress_ptr cinfo)
{
	trace("luajpeg_term_destination()");
	JpegCompress *jc = JPEG_COMPRESS_PTR(cinfo);
	luajpeg_flush_buffer(jc, cinfo->dest->free_in_buffer, FALSE);
}


METHODDEF(void)
luajpeg_source_no_operation (j_decompress_ptr cinfo)
{
	trace("luajpeg_source_no_operation()");
}


METHODDEF(boolean)
luajpeg_fill_input_buffer (j_decompress_ptr cinfo)
{
	trace("luajpeg_fill_input_buffer()");
	JpegDecompress *jd = JPEG_DECOMPRESS_PTR(cinfo);
	if ((jd->srcFn.state == NULL) || (jd->srcFn.ref == LUA_NOREF)) {
		return FALSE; // to indicate I/O suspension
	}
	lua_State *l = jd->srcFn.state;
	lua_rawgeti(l, LUA_REGISTRYINDEX, jd->srcFn.ref);
	if (lua_pcall(l, 0, 1, 0) != 0) {
		trace("fillBuffer() => Failed"); // TODO
	} else {
		luajpeg_set_source_buffer(jd, l);
	}
	return TRUE;
}

METHODDEF(void)
luajpeg_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	trace("luajpeg_skip_input_data(%d)", num_bytes);
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
* decompress functions
********************************************************************************
*/

static int luajpeg_decompress_new(lua_State *l) {
	JpegDecompress *jd = (JpegDecompress *)lua_newuserdata(l, sizeof(JpegDecompress));

	jd->cinfo.err = jpeg_std_error(&jd->errormgr);
	jd->cinfo.err->error_exit = luajpeg_error_exit;

	trace("jpeg_create_decompress()");
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
	trace("luajpeg_decompress_fill_source()");
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);
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
	trace("luajpeg_decompress_read_header()");
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);

	trace("jpeg_read_header()");
	if (jpeg_read_header(&jd->cinfo, TRUE) == JPEG_SUSPENDED) {
		lua_pushnil(l);
		lua_pushstring(l, "suspended");
		return 2;
	}

	trace("width: %d", jd->cinfo.image_width);
	trace("height: %d", jd->cinfo.image_height);
	trace("colorSpace: %d", jd->cinfo.jpeg_color_space);

	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.image_width)
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.image_height)
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.jpeg_color_space, JCS_UNKNOWN, CS_OPTIONS, CS_VALUES))
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.num_components)

	return 1;
}

static int luajpeg_decompress_configure(lua_State *l) {
	trace("luajpeg_decompress_configure()");
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);

	if (lua_istable(l, 2)) {
		if (jd->runStep == 0) {
			/*
			* Scale the image by the fraction scale_num/scale_denom.
			* Currently, the supported scaling ratios are M/N with all M from 1 to 16,
			* where N is the source DCT size, which is 8 for baseline JPEG.
			*/
			SET_OPT_INTEGER_FIELD(l, 2, jd->cinfo.scale_num, "scaleNum");
			SET_OPT_INTEGER_FIELD(l, 2, jd->cinfo.scale_denom, "scaleDenom");
			SET_OPT_NUMBER_FIELD(l, 2, jd->cinfo.output_gamma, "gamma");
		}
		SET_OPT_INTEGER_FIELD(l, 2, jd->bytesPerRow, "bytesPerRow");
	}
	return 0;
}

static int luajpeg_decompress_get_infos(lua_State *l) {
	trace("luajpeg_decompress_get_infos()");
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);

	lua_newtable(l);

	lua_pushstring(l, "image");
	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.image_width)
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.image_height)
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.jpeg_color_space, JCS_UNKNOWN, CS_OPTIONS, CS_VALUES))
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.num_components)
	lua_rawset(l, -3);

	lua_pushstring(l, "output");
	lua_newtable(l);
	SET_TABLE_KEY_INTEGER(l, "width", jd->cinfo.output_width)
	SET_TABLE_KEY_INTEGER(l, "height", jd->cinfo.output_height)
	SET_TABLE_KEY_STRING(l, "colorSpace", getOptionField(jd->cinfo.out_color_space, JCS_UNKNOWN, CS_OPTIONS, CS_VALUES))
	SET_TABLE_KEY_INTEGER(l, "components", jd->cinfo.output_components)
	SET_TABLE_KEY_NUMBER(l, "gamma", jd->cinfo.output_gamma)

	SET_TABLE_KEY_INTEGER(l, "scaleNum", jd->cinfo.scale_num)
	SET_TABLE_KEY_INTEGER(l, "scaleDenom", jd->cinfo.scale_denom)

	SET_TABLE_KEY_INTEGER(l, "bytesPerRow", jd->bytesPerRow)
	lua_rawset(l, -3);

	return 1;
}

static int luajpeg_decompress_run(lua_State *l) {
	trace("luajpeg_decompress_run()");
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);

	trace("step: %d", jd->runStep);
	if (jd->runStep == 0) {
		trace("jpeg_start_decompress()");
		if (! jpeg_start_decompress(&jd->cinfo)) {
			lua_pushnil(l);
			lua_pushstring(l, "suspended");
			return 2;
		}
		jd->runStep++;

		// After this call, the final output image dimensions, including any requested scaling, are available in the JPEG object
		trace("width: %d", jd->cinfo.output_width);
		trace("height: %d", jd->cinfo.output_height);
		trace("color_space: %d", jd->cinfo.out_color_space);
		trace("components: %d", jd->cinfo.output_components);
		trace("gamma: %f", jd->cinfo.output_gamma);

		jd->bytesPerRow = jd->cinfo.output_width * jd->cinfo.output_components;

		if (!lua_isuserdata(l, 2)) {
			return 0; // it's ok to stop after jpeg_start_decompress() to access the output infos
		}
	}
	if (jd->runStep == 1) {
		// we may want to allocate a buffer and return it as a string or userdata
		luaL_checktype(l, 2, LUA_TUSERDATA);
		size_t imageLength = lua_rawlen(l, 2);
		char *imageData = (char *)lua_touserdata(l, 2);
		trace("bytesPerRow: %d", jd->bytesPerRow);
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
	if (jd->runStep == 2) {
		trace("jpeg_finish_decompress()");
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
	JpegDecompress *jd = (JpegDecompress *)lua_touserdata(l, 1);
	trace("luajpeg_destroy_decompress()");
	jpeg_destroy_decompress(&jd->cinfo);
	unregisterLuaReference(&jd->buffer);
	unregisterLuaReference(&jd->srcFn);
	return 0;
}


/*
********************************************************************************
* compress functions
********************************************************************************
*/

static int luajpeg_compress_new(lua_State *l) {
	JpegCompress *jc = (JpegCompress *)lua_newuserdata(l, sizeof(JpegCompress));

	jc->cinfo.err = jpeg_std_error(&jc->errormgr);
	jc->cinfo.err->error_exit = luajpeg_error_exit;

	trace("jpeg_create_compress()");
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

static int luajpeg_compress_run(lua_State *l) {
	trace("luajpeg_compress_run()");
	JpegCompress *jc = (JpegCompress *)lua_touserdata(l, 1);
	size_t imageLength = 0;
	const char *imageData = NULL;

	if (lua_isstring(l, 2)) {
		imageData = luaL_checklstring(l, 2, &imageLength);
	} else {
		luaL_checktype(l, 2, LUA_TUSERDATA);
		imageLength = lua_rawlen(l, 2);
		imageData = (char *)lua_touserdata(l, 2);
	}
	
	luaL_checktype(l, 3, LUA_TTABLE);
	jc->cinfo.image_width = getLongField(l, 3, "width", 0);
	jc->cinfo.image_height = getLongField(l, 3, "height", 0);
	// # of color components per pixel, 1 or 3
	jc->cinfo.input_components = getIntegerField(l, 3, "components", 3);
	jc->cinfo.in_color_space = checkOptionField(l, 3, "colorSpace", "RGB", CS_OPTIONS, CS_VALUES);
	// physical row width in buffer
	// TODO replace by row padding which gracefully default to 0
	int bytesPerRow = getLongField(l, 3, "bytesPerRow", jc->cinfo.image_width * jc->cinfo.input_components);
	// quality 0-100, default 75, should use 50-95
	int quality = getIntegerField(l, 3, "quality", 75);

	luaL_checktype(l, 4, LUA_TFUNCTION);
	lua_pushvalue(l, 4);
	registerLuaReference(&jc->destFn, l);

	if (lua_isuserdata(l, 5) && !lua_islightuserdata(l, 5)) {
		lua_pushvalue(l, 5);
		registerLuaReference(&jc->buffer, l);
	} else {
		size_t bufferSize = (size_t) luaL_optinteger(l, 5, 0);
		if (bufferSize < 2048) {
			bufferSize = 2048;
		}
		(void) lua_newuserdata(l, bufferSize);
		registerLuaReference(&jc->buffer, l);
	}

	trace("jpeg_set_defaults()");
	jpeg_set_defaults(&jc->cinfo);

	jpeg_set_colorspace(&jc->cinfo, JCS_RGB);

	trace("jpeg_set_quality()");
	jpeg_set_quality(&jc->cinfo, quality, TRUE);

	trace("jpeg_start_compress()");
	jpeg_start_compress(&jc->cinfo, TRUE);

	size_t image_size = (size_t) (bytesPerRow * jc->cinfo.image_height);
	if (imageLength < image_size) {
		lua_pushnil(l);
		lua_pushstring(l, "image buffer too small");
		return 2;
	}

	JSAMPROW row_pointer[1];
	trace("bytesPerRow: %d", bytesPerRow);
	while (jc->cinfo.next_scanline < jc->cinfo.image_height) {
		row_pointer[0] = (JSAMPROW) (imageData + jc->cinfo.next_scanline * bytesPerRow);
		(void) jpeg_write_scanlines(&jc->cinfo, row_pointer, 1);
	}

	trace("jpeg_finish_compress()");
	jpeg_finish_compress(&jc->cinfo);

	unregisterLuaReference(&jc->destFn);
	unregisterLuaReference(&jc->buffer);
	return 0;
}

static int luajpeg_compress_gc(lua_State *l) {
	JpegCompress *jc = (JpegCompress *)lua_touserdata(l, 1);
	trace("jpeg_destroy_compress()");
	jpeg_destroy_compress(&jc->cinfo);
	unregisterLuaReference(&jc->buffer);
	unregisterLuaReference(&jc->destFn);
	return 0;
}


/*
********************************************************************************
* Module open function
********************************************************************************
*/

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
		{ "newCompress", luajpeg_compress_new },
		{ "compress", luajpeg_compress_run },
		{ "newDecompress", luajpeg_decompress_new },
		{ "fillSource", luajpeg_decompress_fill_source },
		{ "readHeader", luajpeg_decompress_read_header },
		{ "configureDecompress", luajpeg_decompress_configure },
		{ "getInfosDecompress", luajpeg_decompress_get_infos },
		{ "decompress", luajpeg_decompress_run },
		{ NULL, NULL }
	};
	lua_newtable(l);
	luaL_setfuncs(l, reg, 0);
	lua_pushliteral(l, "Lua jpeg");
	lua_setfield(l, -2, "_NAME");
	lua_pushliteral(l, "0.1");
	lua_setfield(l, -2, "_VERSION");
	trace("luaopen_jpeg() done\n");
	return 1;
}
