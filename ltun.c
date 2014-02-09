#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>

#include <lua.h>
#include <lauxlib.h>

#if LUA_VERSION_NUM == 501

#define LUA_OK 0

static void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup)
{
    luaL_checkstack(L, nup + 1, "too many upvalues");
    for (; l->name != NULL; l++) {
        int i;
        lua_pushstring(L, l->name);
        for (i = 0; i < nup; i++) {
            lua_pushvalue(L, -(nup + 1));
        }
        lua_pushcclosure(L, l->func, nup);
        lua_settable(L, -(nup + 3));
    }
    lua_pop(L, nup);
}

static void luaL_setmetatable(lua_State *L, const char *tname)
{
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

#define luaL_newlibtable(L,l) lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)
#define luaL_newlib(L,l) (luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

#endif

#define LTUN_TUNTAP_TNAME "ltun.TunTapDevice"

typedef struct {
    int fd;
    char name[IFNAMSIZ];
} tuntap_t;

static int throw_from_errno(lua_State* L, const char* s)
{
    luaL_Buffer err;
    char buf[256];
#if LUA_VERSION_NUM == 501
    char* errstr;
#endif
    int len;

    luaL_buffinit(L, &err);
    luaL_addstring(&err, s);
    luaL_addstring(&err, ": ");
    len = snprintf(buf, sizeof(buf), "%m");
    if (len < 0) {
        lua_pushfstring(L, "%d", errno);
        luaL_addvalue(&err);
        luaL_pushresult(&err);
    } else if (len >= (int)sizeof(buf)) {
#if LUA_VERSION_NUM == 501
        if (len + 1 <= LUAL_BUFFERSIZE) {
            snprintf(luaL_prepbuffer(&err), len + 1, "%m");
            luaL_addsize(&err, len + 1);
            luaL_pushresult(&err);
        } else {
            luaL_pushresult(&err);
            errstr = lua_newuserdata(L, len + 1);
            snprintf(errstr, len + 1, "%m");
            lua_pushlstring(L, errstr, len);
            lua_remove(L, -2);
            lua_concat(L, 2);
        }
#else
        snprintf(luaL_prepbuffsize(&err, len + 1), len + 1, "%m");
        luaL_pushresultsize(&err, len);
#endif
    } else {
        luaL_addstring(&err, buf);
        luaL_pushresult(&err);
    }
    luaL_where(L, 1);
    lua_insert(L, -2);
    lua_concat(L, 2);
    return lua_error(L);
}

static int if_ioctl(int cmd, struct ifreq* req)
{
    int ret;
    int sock;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    ret = ioctl(sock, cmd, req);
    close(sock);
    return ret;
}

static int tuntap_read(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    lua_Number n;
    size_t len;
    ssize_t rdlen;
    char* buf;
    char tmp[4096];

    n = luaL_checknumber(L, 2);
    if (n == 0) {
        lua_pushnil(L);
        return 1;
    }
    else if (n < 0) {
        return luaL_error(L, "invalid size: %f", n);
    }
#if LUA_VERSION_NUM == 501
    len = lua_tointeger(L, 2);
#else
    len = lua_tounsigned(L, 2);
#endif
    if (len > sizeof(tmp)) {
        buf = lua_newuserdata(L, len);
    } else {
        buf = tmp;
    }
    rdlen = read(tuntap->fd, buf, len);
    if (rdlen < 0) {
        return throw_from_errno(L, "failed to read packet");
    }
    lua_pushlstring(L, buf, rdlen);
    return 1;
}

static int tuntap_write(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    const char* buf;
    size_t len;
    ssize_t written;

    buf = luaL_checklstring(L, 2, &len);
    written = write(tuntap->fd, buf, len);
    if (written < 0) {
        return throw_from_errno(L, "failed to write packet");
    }
    lua_pushinteger(L, written);
    return 1;
}

static int tuntap_fileno(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    lua_pushinteger(L, tuntap->fd);
    return 1;
}

static int tuntap_close(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    if (tuntap->fd >= 0) {
        close(tuntap->fd);
        tuntap->fd = -1;
    }
    return 0;
}

static int tuntap_up(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    struct ifreq req;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFFLAGS, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface flags");
    }
    if (!(req.ifr_flags & IFF_UP)) {
        req.ifr_flags |= IFF_UP;
        if (if_ioctl(SIOCSIFFLAGS, &req) < 0) {
            return throw_from_errno(L, "failed to set the interface flags");
        }
    }
    return 0;
}

static int tuntap_down(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    struct ifreq req;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFFLAGS, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface flags");
    }
    if (req.ifr_flags & IFF_UP) {
        req.ifr_flags &= ~IFF_UP;
        if (if_ioctl(SIOCSIFFLAGS, &req) < 0) {
            return throw_from_errno(L, "failed to set the interface flags");
        }
    }
    return 0;
}

static int tuntap_persist(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    int persist;

    luaL_checktype(L, 2, LUA_TBOOLEAN);
    persist = lua_toboolean(L, 2);
    if (ioctl(tuntap->fd, TUNSETPERSIST, persist) < 0) {
        return throw_from_errno(L, "failed to make the TUN/TAP device persistent");
    }
    return 0;
}

static int tuntap_gc(lua_State* L)
{
    return tuntap_close(L);
}

static int tuntap_get_name(lua_State* L, tuntap_t* tuntap)
{
    lua_pushstring(L, tuntap->name);
    return 1;
}

static int tuntap_get_addr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* addr;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFADDR, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface address");
    }
    addr = inet_ntoa(((struct sockaddr_in*)&req.ifr_addr)->sin_addr);
    if (addr == NULL) {
        return luaL_error(L, "bad IPv4 address");
    }
    lua_pushstring(L, addr);
    return 1;
}

static int tuntap_set_addr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* addr;
    struct sockaddr_in* sin;

    addr = luaL_checkstring(L, 1);
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    sin = (struct sockaddr_in*)&req.ifr_addr;
    sin->sin_family = AF_INET;
    if (inet_aton(addr, &sin->sin_addr) == 0) {
        return luaL_error(L, "bad IPv4 address");
    }
    if (if_ioctl(SIOCSIFADDR, &req) < 0) {
        return throw_from_errno(L, "failed to set the interface address");
    }
    return 0;
}

static int tuntap_get_dstaddr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* dstaddr;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFDSTADDR, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface address");
    }
    dstaddr = inet_ntoa(((struct sockaddr_in*)&req.ifr_addr)->sin_addr);
    if (dstaddr == NULL) {
        return luaL_error(L, "bad IPv4 address");
    }
    lua_pushstring(L, dstaddr);
    return 1;
}

static int tuntap_set_dstaddr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* dstaddr;
    struct sockaddr_in* sin;

    dstaddr = luaL_checkstring(L, 1);
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    sin = (struct sockaddr_in*)&req.ifr_addr;
    sin->sin_family = AF_INET;
    if (inet_aton(dstaddr, &sin->sin_addr) == 0) {
        return luaL_error(L, "bad IPv4 address");
    }
    if (if_ioctl(SIOCSIFDSTADDR, &req) < 0) {
        return throw_from_errno(L, "failed to set the interface address");
    }
    return 0;
}

static int tuntap_get_hwaddr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFHWADDR, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface hw address");
    }
    lua_pushlstring(L, req.ifr_hwaddr.sa_data, ETH_ALEN);
    return 1;
}

static int tuntap_set_hwaddr(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* hwaddr;
    size_t len;

    hwaddr = luaL_checklstring(L, 1, &len);
    if (len != ETH_ALEN) {
        return luaL_error(L, "bad MAC address");
    }
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    req.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(req.ifr_hwaddr.sa_data, hwaddr, len);
    if (if_ioctl(SIOCSIFHWADDR, &req) < 0) {
        return throw_from_errno(L, "failed to set the interface hw address");
    }
    return 0;
}

static int tuntap_get_netmask(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* netmask;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFNETMASK, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface netmask");
    }
    netmask = inet_ntoa(((struct sockaddr_in*)&req.ifr_netmask)->sin_addr);
    if (netmask == NULL) {
        return luaL_error(L, "bad IPv4 address");
    }
    lua_pushstring(L, netmask);
    return 1;
}

static int tuntap_set_netmask(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    const char* netmask;
    struct sockaddr_in* sin;

    netmask = luaL_checkstring(L, 1);
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    sin = (struct sockaddr_in*)&req.ifr_netmask;
    sin->sin_family = AF_INET;
    if (inet_aton(netmask, &sin->sin_addr) == 0) {
        return luaL_error(L, "bad IPv4 address");
    }
    if (if_ioctl(SIOCSIFNETMASK, &req) < 0) {
        return throw_from_errno(L, "failed to set the interface netmask");
    }
    return 0;
}

static int tuntap_get_mtu(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    if (if_ioctl(SIOCGIFMTU, &req) < 0) {
        return throw_from_errno(L, "failed to get the interface MTU");
    }
    lua_pushinteger(L, req.ifr_mtu);
    return 1;
}

static int tuntap_set_mtu(lua_State* L, tuntap_t* tuntap)
{
    struct ifreq req;
    int mtu;

    mtu = luaL_checkint(L, 1);
    if (mtu <= 0) {
        return luaL_error(L, "bad MTU, should be > 0");
    }
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, tuntap->name);
    req.ifr_mtu = mtu;
    if (if_ioctl(SIOCSIFMTU, &req) < 0) {
        return throw_from_errno(L, "failed to set the interface MTU");
    }
    return 0;
}

static int tuntap_index(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    const char* key = luaL_checkstring(L, 2);

    if (strcmp(key, "name") == 0) {
        return tuntap_get_name(L, tuntap);
    } else if (strcmp(key, "addr") == 0) {
        return tuntap_get_addr(L, tuntap);
    } else if (strcmp(key, "dstaddr") == 0) {
        return tuntap_get_dstaddr(L, tuntap);
    } else if (strcmp(key, "hwaddr") == 0) {
        return tuntap_get_hwaddr(L, tuntap);
    } else if (strcmp(key, "netmask") == 0) {
        return tuntap_get_netmask(L, tuntap);
    } else if (strcmp(key, "mtu") == 0) {
        return tuntap_get_mtu(L, tuntap);
    } else {
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, key);
        return 1;
    }
}

static int tuntap_newindex(lua_State* L)
{
    tuntap_t* tuntap = luaL_checkudata(L, 1, LTUN_TUNTAP_TNAME);
    const char* key = luaL_checkstring(L, 2);

    if (strcmp(key, "addr") == 0) {
        return tuntap_set_addr(L, tuntap);
    } else if (strcmp(key, "dstaddr") == 0) {
        return tuntap_set_dstaddr(L, tuntap);
    } else if (strcmp(key, "hwaddr") == 0) {
        return tuntap_set_hwaddr(L, tuntap);
    } else if (strcmp(key, "netmask") == 0) {
        return tuntap_set_netmask(L, tuntap);
    } else if (strcmp(key, "mtu") == 0) {
        return tuntap_set_mtu(L, tuntap);
    }
    return 0;
}

static const luaL_Reg ltun_tuntap_meth[] = {
    {"read", tuntap_read},
    {"write", tuntap_write},
    {"close", tuntap_close},
    {"fileno", tuntap_fileno},
    {"up", tuntap_up},
    {"down", tuntap_down},
    {"persist", tuntap_persist},
    {"__index", tuntap_index},
    {"__newindex", tuntap_newindex},
    {"__gc", tuntap_gc},
    {NULL, NULL}
};

static void create_tuntap_meta(lua_State* L)
{
    luaL_newmetatable(L, LTUN_TUNTAP_TNAME);
    luaL_setfuncs(L, ltun_tuntap_meth, 0);
    lua_pop(L, 1);
}

static int ltun_create(lua_State* L)
{
    tuntap_t* tuntap;
    int nargs;
    int i;
    const char* name = "";
    size_t name_len = 0;
    int flags = IFF_TUN;
    const char* dev = "/dev/net/tun";
    struct ifreq req;

    nargs = lua_gettop(L);
    if (nargs >= 1) {
        i = 1;
        if (lua_type(L, i) == LUA_TSTRING) {
            name = luaL_checklstring(L, i, &name_len);
            ++i;
        }
        if (i <= nargs) {
            flags = 0;
            for (; i <= nargs; ++i) {
                flags |= luaL_checkint(L, i);
            }
        }
    }
    if (!(flags & (IFF_TUN | IFF_TAP)) || ((flags & IFF_TUN) && (flags & IFF_TAP))) {
        return luaL_error(L, "bad flags: IFF_TUN or IFF_TAP expected");
    }
    if (name_len >= IFNAMSIZ) {
        return luaL_error(L, "interface name too long");
    }
    tuntap = lua_newuserdata(L, sizeof(*tuntap));
    tuntap->fd = open(dev, O_RDWR);
    if (tuntap->fd < 0) {
        lua_pushfstring(L, "failed to open %s", dev);
        return throw_from_errno(L, lua_tostring(L, -1));
    }
    luaL_setmetatable(L, LTUN_TUNTAP_TNAME);
    memset(&req, 0, sizeof(req));
    if (name_len) {
        strcpy(req.ifr_name, name);
    }
    req.ifr_flags = flags;
    if (ioctl(tuntap->fd, TUNSETIFF, &req) < 0) {
        return throw_from_errno(L, "failed to create TUN/TAP device");
    }
    strcpy(tuntap->name, req.ifr_name);
    return 1;
}

static const luaL_Reg ltunlib[] = {
    {"create", ltun_create},
    {NULL, NULL}
};

int luaopen_ltun(lua_State* L)
{
    luaL_newlib(L, ltunlib);
    lua_pushnumber(L, IFF_TUN);
    lua_setfield(L, -2, "IFF_TUN");
    lua_pushnumber(L, IFF_TAP);
    lua_setfield(L, -2, "IFF_TAP");
#ifdef IFF_NO_PI
    lua_pushnumber(L, IFF_NO_PI);
    lua_setfield(L, -2, "IFF_NO_PI");
#endif
#ifdef IFF_ONE_QUEUE
    lua_pushnumber(L, IFF_ONE_QUEUE);
    lua_setfield(L, -2, "IFF_ONE_QUEUE");
#endif
#ifdef IFF_VNET_HDR
    lua_pushnumber(L, IFF_VNET_HDR);
    lua_setfield(L, -2, "IFF_VNET_HDR");
#endif
#ifdef IFF_TUN_EXCL
    lua_pushnumber(L, IFF_TUN_EXCL);
    lua_setfield(L, -2, "IFF_TUN_EXCL");
#endif
    create_tuntap_meta(L);
    return 1;
}

