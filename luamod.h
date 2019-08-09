#ifndef JLS_LUA_MOD_H
#define JLS_LUA_MOD_H

#include <lua.h>
#include <lauxlib.h>

//#define JLS_LUA_MOD_TRACE 1

#ifdef JLS_LUA_MOD_TRACE
#include <stdio.h>
#define trace(...) printf(__VA_ARGS__)
#else
#define trace(...) ((void)0)
#endif

/*
 * All the API functions return nil in case of error following by an optional error message string.
 * It implies that functions must return at least one value and this value cannot be false or nil.
 */

/*
lua_pushboolean(l, 1); // true
lua_pushboolean(l, 0); // false
// to be used with assert
lua_pushnil(l);
lua_pushstring(l, "some error");
or
lua_pushfstring (l, "%s", strerror(errno));
*/

#define RETURN_SUCCESS(_LS) lua_pushboolean(_LS, 1); \
	return 1

#define RETURN_ERROR(_LS, _MSG) lua_pushnil(_LS); \
	lua_pushstring(_LS, _MSG); \
	return 2

#define SET_TABLE_KEY_STRING(_LS, _KEY, _VALUE) \
	lua_pushstring(_LS, _KEY); \
	lua_pushstring(_LS, _VALUE); \
	lua_rawset(_LS, -3)

#define SET_TABLE_KEY_INTEGER(_LS, _KEY, _VALUE) \
	lua_pushstring(_LS, _KEY); \
	lua_pushinteger(_LS, (lua_Integer) (_VALUE)); \
	lua_rawset(_LS, -3)

#define SET_TABLE_KEY_NUMBER(_LS, _KEY, _VALUE) \
	lua_pushstring(_LS, _KEY); \
	lua_pushnumber(_LS, (lua_Number) (_VALUE)); \
	lua_rawset(_LS, -3)

#define SET_TABLE_INDEX_STRING(_LS, _INDEX, _VALUE) \
	lua_pushstring(_LS, _VALUE); \
	lua_rawseti(_LS, -2, (lua_Integer) (_INDEX))

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

#define b2s(b) ((b) ? "true" : "false")

#endif /* JLS_LUA_MOD_H */
