#include <lua.hpp>
#include <bee/lua/binding.h>
#include <bee/utility/unicode.h>

namespace bee::lua_unicode {
	static int lu2a(lua_State* L) {
		std::string r = u2a(lua::to_strview(L, 1));
		lua_pushlstring(L, r.data(), r.size());
		return 1;
	}

	static int la2u(lua_State* L) {
		std::string r = a2u(lua::to_strview(L, 1));
		lua_pushlstring(L, r.data(), r.size());
		return 1;
	}

	int luaopen(lua_State* L) {
		luaL_Reg lib[] = {
			{ "u2a", lu2a },
			{ "a2u", la2u },
			{ NULL, NULL }
		};
		luaL_newlib(L, lib);
		return 1;
	}
}

DEFINE_LUAOPEN(unicode)
