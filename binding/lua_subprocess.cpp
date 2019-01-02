#include <bee/subprocess.h>
#include <bee/utility/unicode.h>
#include <bee/lua/binding.h>
#include <bee/lua/file.h>
#include <bee/error.h>
#include <lua.hpp>
#include <optional>
#include <errno.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#define ENABLE_FILESYSTEM
#endif

namespace bee::lua_socket {
    net::socket::fd_t checksocket(lua_State* L, int idx);
    void pushsocket(lua_State* L, net::socket::fd_t fd);
}

namespace bee::lua_subprocess {
#ifdef ENABLE_FILESYSTEM
    static fs::path& topath(lua_State* L, int idx) {
        return *(fs::path*)getObject(L, idx, "filesystem");
    }
#endif

    namespace process {
        static subprocess::process& to(lua_State* L, int idx) {
            return *(subprocess::process*)getObject(L, idx, "subprocess");
        }

        static int destructor(lua_State* L) {
            subprocess::process& self = to(L, 1);
            self.~process();
            return 0;
        }

        static int wait(lua_State* L) {
            subprocess::process& self = to(L, 1);
            lua_pushinteger(L, (lua_Integer)self.wait());
            return 1;
        }

        static int kill(lua_State* L) {
            subprocess::process& self = to(L, 1);
            bool ok = self.kill((int)luaL_optinteger(L, 2, 15));
            lua_pushboolean(L, ok);
            return 1;
        }

        static int get_id(lua_State* L) {
            subprocess::process& self = to(L, 1);
            lua_pushinteger(L, (lua_Integer)self.get_id());
            return 1;
        }

        static int is_running(lua_State* L) {
            subprocess::process& self = to(L, 1);
            lua_pushboolean(L, self.is_running());
            return 1;
        }

        static int resume(lua_State* L) {
            subprocess::process& self = to(L, 1);
            lua_pushboolean(L, self.resume());
            return 1;
        }

        static int native_handle(lua_State* L) {
            subprocess::process& self = to(L, 1);
            lua_pushinteger(L, self.native_handle());
            return 1;
        }

        static int index(lua_State* L) {
            lua_pushvalue(L, 2);
            if (LUA_TNIL != lua_rawget(L, lua_upvalueindex(1))) {
                return 1;
            }
            if (LUA_TTABLE == lua_getuservalue(L, 1)) {
                lua_pushvalue(L, 2);
                if (LUA_TNIL != lua_rawget(L, -2)) {
                    return 1;
                }
            }
            return 0;
        }

        static int newindex(lua_State* L) {
            if (LUA_TTABLE != lua_getuservalue(L, 1)) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
#if LUA_VERSION_NUM >= 504
                if (!lua_setuservalue(L, 1)) {
                    return 0;
                }
#else
                lua_setuservalue(L, 1);
#endif
            }
            lua_insert(L, -3);
            lua_rawset(L, -3);
            return 0;
        }

        static int constructor(lua_State* L, subprocess::spawn& spawn) {
            void* storage = lua_newuserdata(L, sizeof(subprocess::process));

            if (newObject(L, "subprocess")) {
                static luaL_Reg mt[] = {
                    { "wait", process::wait },
                    { "kill", process::kill },
                    { "get_id", process::get_id },
                    { "is_running", process::is_running },
                    { "resume", process::resume },
                    { "native_handle", process::native_handle },
                    { "__gc", process::destructor },
                    { NULL, NULL }
                };
                luaL_setfuncs(L, mt, 0);

                static luaL_Reg mt2[] = {
                    { "__index", process::index },
                    { "__newindex", process::newindex },
                    { NULL, NULL }
                };
                lua_pushvalue(L, -1);
                luaL_setfuncs(L, mt2, 1);
            }
            lua_setmetatable(L, -2);
            new (storage)subprocess::process(spawn);
            return 1;
        }
    }

    namespace spawn {
        static std::optional<lua::string_type> cast_cwd(lua_State* L) {
            if (LUA_TSTRING == lua_getfield(L, 1, "cwd")) {
                lua::string_type ret(lua::to_string(L, -1));
                lua_pop(L, 1);
                return ret;
            }
#ifdef ENABLE_FILESYSTEM
            else if (LUA_TUSERDATA == lua_type(L, -1)) {
                lua::string_type ret = topath(L, -1).string<lua::string_type::value_type>();
                lua_pop(L, 1);
                return ret;
            }
#endif
            lua_pop(L, 1);
            return std::optional<lua::string_type>();
        }

#if defined(_WIN32)
#   define LOAD_ARGS(L, idx) lua::to_string((L), (idx))
#else
#   define LOAD_ARGS(L, idx) (char*)luaL_checkstring((L), (idx))
#endif
        static void cast_args_array(lua_State* L, int idx, subprocess::args_t& args) {
            args.type = subprocess::args_t::type::array;
            lua_Integer n = luaL_len(L, idx);
            for (lua_Integer i = 1; i <= n; ++i) {
                switch (lua_geti(L, idx, i)) {
                case LUA_TSTRING:
                    args.push_back(LOAD_ARGS(L, -1));
                    break;
#ifdef ENABLE_FILESYSTEM
                case LUA_TUSERDATA:
#if defined(_WIN32)
                    args.push_back(topath(L, -1).wstring());
#else
                    args.push_back((char*)topath(L, -1).c_str());
#endif
                    break;
#endif
                case LUA_TTABLE:
                    cast_args_array(L, lua_absindex(L, -1), args);
                    break;
                default:
                    luaL_error(L, "Unsupported type: %s.", lua_typename(L, lua_type(L, -1)));
                    return;
                }
                lua_pop(L, 1);
            }
        }

        static void cast_args_string(lua_State* L, int idx, subprocess::args_t& args) {
            args.type = subprocess::args_t::type::string;
            for (lua_Integer i = 1; i <= 2; ++i) {
                switch (lua_geti(L, idx, i)) {
                case LUA_TSTRING:
                    args.push_back(LOAD_ARGS(L, -1));
                    break;
#ifdef ENABLE_FILESYSTEM
                case LUA_TUSERDATA:
#if defined(_WIN32)
                    args.push_back(topath(L, -1).wstring());
#else
                    args.push_back((char*)topath(L, -1).c_str());
#endif
                    break;
#endif
                default:
                    luaL_error(L, "Unsupported type: %s.", lua_typename(L, lua_type(L, -1)));
                    return;
                }
                lua_pop(L, 1);
            }
        }

        static subprocess::args_t cast_args(lua_State* L) {
            bool as_string = false;
            if (LUA_TSTRING == lua_getfield(L, 1, "argsStyle")) {
                as_string = (strcmp(lua_tostring(L, -1), "string") == 0);
            }
            lua_pop(L, 1);
            subprocess::args_t args;
            if (as_string) {
                cast_args_string(L, 1, args);
            }
            else {
                cast_args_array(L, 1, args);
            }
            return args;
        }

        static file::handle cast_stdio(lua_State* L, const char* name) {
            switch (lua_getfield(L, 1, name)) {
            case LUA_TUSERDATA: {
                luaL_Stream* p = (luaL_Stream*)luaL_checkudata(L, -1, LUA_FILEHANDLE);
                if (!p->closef) {
                    return 0;
                }
                return file::dup(p->f);
            }
            case LUA_TBOOLEAN: {
                if (!lua_toboolean(L, -1)) {
                    break;
                }
                auto pipe = subprocess::pipe::open();
                if (!pipe) {
                    break;
                }
                lua_pop(L, 1);
                if (strcmp(name, "stdin") == 0) {
                    bee::lua::newfile(L, pipe.open_file(file::mode::eWrite));
                    return pipe.rd;
                }
                else {
                    bee::lua::newfile(L, pipe.open_file(file::mode::eRead));
                    return pipe.wr;
                }
            }
            default:
                break;
            }
            lua_pop(L, 1);
            return 0;
        }

        static file::handle cast_stdio(lua_State* L, subprocess::spawn& self, const char* name, subprocess::stdio type) {
            file::handle f = cast_stdio(L, name);
            if (!f) {
                return 0;
            }
            self.redirect(type, f);
            return f;
        }

        static void cast_env(lua_State* L, subprocess::spawn& self) {
            if (LUA_TTABLE == lua_getfield(L, 1, "env")) {
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    if (LUA_TSTRING == lua_type(L, -1)) {
                        self.env_set(lua::to_string(L, -2), lua::to_string(L, -1));
                    }
                    else {
                        self.env_del(lua::to_string(L, -2));
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }

        static void cast_suspended(lua_State* L, subprocess::spawn& self) {
            if (LUA_TBOOLEAN == lua_getfield(L, 1, "suspended")) {
                if (lua_toboolean(L, -1)) {
                    self.suspended();
                }
            }
            lua_pop(L, 1);
        }

#if defined(_WIN32)
        static void cast_option(lua_State* L, subprocess::spawn& self)
        {
            if (LUA_TSTRING == lua_getfield(L, 1, "console")) {
                std::string console = luaL_checkstring(L, -1);
                if (console == "new") {
                    self.set_console(subprocess::console::eNew);
                }
                else if (console == "disable") {
                    self.set_console(subprocess::console::eDisable);
                }
                else if (console == "inherit") {
                    self.set_console(subprocess::console::eInherit);
                }
            }
            lua_pop(L, 1);

            if (LUA_TBOOLEAN == lua_getfield(L, 1, "hideWindow")) {
                if (lua_toboolean(L, -1)) {
                    self.hide_window();
                }
            }
            lua_pop(L, 1);

            if (LUA_TBOOLEAN == lua_getfield(L, 1, "searchPath")) {
                if (lua_toboolean(L, -1)) {
                    self.search_path();
                }
            }
            lua_pop(L, 1);
        }
#else
        static void cast_option(lua_State*, subprocess::spawn&)
        { }
#endif

        static void cast_sockets(lua_State* L, subprocess::spawn& self) {
            if (LUA_TTABLE != lua_getfield(L, 1, "sockets")) {
                lua_pop(L, 1);
                return;
            }
            lua_Integer n = luaL_len(L, -1);
            for (lua_Integer i = 1; i <= n; ++i) {
                if (LUA_TUSERDATA == lua_rawgeti(L, -1, i)) {
                    self.duplicate(lua_socket::checksocket(L, -1));
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        static int spawn(lua_State* L) {
            luaL_checktype(L, 1, LUA_TTABLE);
            subprocess::spawn spawn;
            subprocess::args_t args = cast_args(L);
            if (args.size() == 0) {
                return 0;
            }

            std::optional<lua::string_type> cwd = cast_cwd(L);
            cast_env(L, spawn);
            cast_suspended(L, spawn);
            cast_option(L, spawn);
            cast_sockets(L, spawn);

            file::handle f_stdin = cast_stdio(L, spawn, "stdin", subprocess::stdio::eInput);
            file::handle f_stdout = cast_stdio(L, spawn, "stdout", subprocess::stdio::eOutput);
            file::handle f_stderr = cast_stdio(L, spawn, "stderr", subprocess::stdio::eError);
            if (!spawn.exec(args, cwd ? cwd->c_str() : 0)) {
                lua_pushnil(L);
                lua_pushstring(L, make_syserror().what());
                return 2;
            }
            process::constructor(L, spawn);
            if (f_stderr) {
                lua_insert(L, -2);
                lua_setfield(L, -2, "stderr");
            }
            if (f_stdout) {
                lua_insert(L, -2);
                lua_setfield(L, -2, "stdout");
            }
            if (f_stdin) {
                lua_insert(L, -2);
                lua_setfield(L, -2, "stdin");
            }
            return 1;
        }
    }

    static int peek(lua_State* L) {
        luaL_Stream* p = (luaL_Stream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
        if (!p->closef) {
            auto ec = std::make_error_code(std::errc::broken_pipe);
            lua_pushnil(L);
            lua_pushfstring(L, "peek: %s (%d)", ec.message().c_str(), ec.value());
            return 2;
        }
        int n = subprocess::pipe::peek(p->f);
        if (n < 0) {
            auto error = make_syserror("peek");
            lua_pushnil(L);
            lua_pushfstring(L, "%s (%d)", error.what(), error.code().value());
            return 2;
        }
        lua_pushinteger(L, n);
        return 1;
    }

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>

    static int filemode(lua_State* L) {
        luaL_Stream* p = (luaL_Stream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
        const char* mode = luaL_checkstring(L, 2);
        if (p && p->closef && p->f) {
            if (mode[0] == 'b') {
                _setmode(_fileno(p->f), _O_BINARY);
            }
            else {
                _setmode(_fileno(p->f), _O_TEXT);
            }
        }
        return 0;
    }
#else
    static int filemode(lua_State*) { return 0; }
#endif

    int luaopen(lua_State* L) {
        net::socket::initialize();
        static luaL_Reg lib[] = {
            { "spawn", spawn::spawn },
            { "peek", peek },
            { "filemode", filemode },
            { NULL, NULL }
        };
        luaL_newlib(L, lib);

        lua_newtable(L);
        lua_Integer n = 1;
        for (auto& fd : subprocess::pipe::sockets) {
            lua_socket::pushsocket(L, fd);
            lua_rawseti(L, -2, n++);
        }
        lua_setfield(L, -2, "sockets");
        return 1;
    }
}

DEFINE_LUAOPEN(subprocess)
