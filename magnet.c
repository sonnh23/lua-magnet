#include <sys/stat.h>

#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>
#include <stdio.h>
#include <fcgi_stdio.h>
#include <stdlib.h>

static int magnet_print(lua_State *L) {
	const char *s = luaL_checkstring(L, 1);

	printf("%s", s);

	return 0;
}

static int magnet_cache_script(lua_State *L, const char *fn, time_t mtime) {
	/* 
	 * if func = loadfile("<script>") then return -1
	 *
	 * magnet.cache["<script>"].script = func
	 */

	if (luaL_loadfile(L, fn)) {
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
		lua_pop(L, 1); /* remove the error-msg */

		return -1;
	}

	lua_getfield(L, LUA_GLOBALSINDEX, "magnet"); 
	lua_getfield(L, -1, "cache");

	/* .script = <func> */
	lua_newtable(L); 
	assert(lua_isfunction(L, -4));
	lua_pushvalue(L, -4); 
	lua_setfield(L, -2, "script");

	lua_pushinteger(L, mtime);
	lua_setfield(L, -2, "mtime");

	lua_pushinteger(L, 0);
	lua_setfield(L, -2, "hits");

	/* magnet.cache["<script>"] = { script = <func> } */
	lua_setfield(L, -2, fn);

	lua_pop(L, 2); /* pop magnet and magnet.cache */

	/* on the stack should be the function itself */
	assert(lua_isfunction(L, lua_gettop(L)));

	return 0;
}

static int magnet_get_script(lua_State *L, const char *fn) {
	struct stat st;
	time_t mtime = 0;

	assert(lua_gettop(L) == 0);

	if (-1 == stat(fn, &st)) {
		return -1;
	}

	mtime = st.st_mtime;

	/* if not magnet.cache["<script>"] then */

	lua_getfield(L, LUA_GLOBALSINDEX, "magnet"); 
	assert(lua_istable(L, -1));
	lua_getfield(L, -1, "cache");
	assert(lua_istable(L, -1));

	lua_getfield(L, -1, fn);

	if (!lua_istable(L, -1)) {
		lua_pop(L, 3); /* pop the nil */

		if (magnet_cache_script(L, fn, mtime)) {
			return -1;
		}
	} else {
		lua_getfield(L, -1, "mtime");
		assert(lua_isnumber(L, -1));

		if (mtime == lua_tointeger(L, -1)) {
			lua_Integer hits;

			lua_pop(L, 1);
	
			/* increment the hit counter */	
			lua_getfield(L, -1, "hits");
			hits = lua_tointeger(L, -1);
			lua_pop(L, 1);
			lua_pushinteger(L, hits + 1);
			lua_setfield(L, -2, "hits");

			/* it is in the cache */
			assert(lua_istable(L, -1));
			lua_getfield(L, -1, "script");
			assert(lua_isfunction(L, -1));
			lua_insert(L, -4);
			lua_pop(L, 3);
			assert(lua_isfunction(L, -1));
		} else {
			lua_pop(L, 1 + 3);

			if (magnet_cache_script(L, fn, mtime)) {
				return -1;
			}
		}

		/* this should be the function */
	}
	assert(lua_gettop(L) == 1);

	return 0;
}

int main(int argc, char **argv) {
	lua_State *L; 

	L = luaL_newstate();
	luaL_openlibs(L);

	lua_newtable(L); /* magnet. */
	lua_newtable(L); /* magnet.cache. */
	lua_setfield(L, -2, "cache");
	lua_setfield(L, LUA_GLOBALSINDEX, "magnet");

	while (FCGI_Accept() >= 0) {
		assert(lua_gettop(L) == 0);

		if (magnet_get_script(L, getenv("SCRIPT_FILENAME"))) {
			printf("Status: 404\r\n\r\n");

			assert(lua_gettop(L) == 0);
			continue;
		}
		/**
		 * we want to create empty environment for our script 
		 * 
		 * setmetatable({}, {__index = _G})
		 * 
		 * if a function, symbol is not defined in our env, __index will lookup 
		 * in the global env.
		 *
		 * all variables created in the script-env will be thrown 
		 * away at the end of the script run.
		 */
		lua_newtable(L); /* my empty environment */

		/* we have to overwrite the print function */
		lua_pushcfunction(L, magnet_print);                       /* (sp += 1) */
		lua_setfield(L, -2, "print"); /* -1 is the env we want to set(sp -= 1) */

		lua_newtable(L); /* the meta-table for the new env  */
		lua_pushvalue(L, LUA_GLOBALSINDEX);
		lua_setfield(L, -2, "__index"); /* { __index = _G } */
		lua_setmetatable(L, -2); /* setmetatable({}, {__index = _G}) */
		lua_setfenv(L, -2); /* on the stack should be a modified env */

		if (lua_pcall(L, 0, 1, 0)) {
			fprintf(stderr, "%s\n", lua_tostring(L, -1));
			lua_pop(L, 1); /* remove the error-msg and the function copy from the stack */

			continue;
		}

		lua_pop(L, 1);
		assert(lua_gettop(L) == 0);
	}

	lua_close(L);

	return 0;
}	