Linux TUN/TAP module for Lua
============================

``ltun`` is a Lua module which let you create TUN/TAP device very easily.

License: MIT (see LICENSE)

Dependencies
------------

Lua >= 5.1

Compilation
-----------

To compile, just type the following command in the terminal::

    make

If you are on a debian-like system and you have installed all the required
dependencies, it should work as-is. If you are out of luck, you can tweak the
compilation process using the following variables:

- LUA_VERSION
- LUA_CFLAGS

For example, say that you want to compile ``ltun`` for Lua 5.1 (by default
``ltun`` is compiled for Lua 5.2) you can try::

    make LUA_VERSION=5.1

Or for LuaJIT::

    make LUA_VERSION=jit

If the Lua development headers are not in a common location, you can try::

    make LUA_CFLAGS="-I/path/to/lua/headers"

Documentation
-------------

NOTE: On most distributions you will need to be root to create TUN/TAP devices.

To create a TUN device::

    local ltun = require('ltun')

    local tun = ltun.create()

To create a TAP device::

    local ltun = require('ltun')

    local tap = ltun.create(ltun.IFF_TAP)

To create a TUN/TAP device with a custom name::

    local tun = ltun.create('mytun')

To specify additional flags::

    local tun = ltun.create('mytun', ltun.IFF_TUN, ltun.IFF_NO_PI)

Or just::

    local tun = ltun.create(ltun.IFF_TUN, ltun.IFF_NO_PI)

You can get/set some parameters of the device directly::

    print(tun.name)
    tun.addr = '10.8.0.1'
    tun.dstaddr = '10.8.0.2'
    tun.netmask = '255.255.255.0'
    tun.mtu = 1500

If the device is a TAP you can also get/set its MAC address::

    tap.hwaddr = '\x00\x11\x22\x33\x44\x55'
    print(tap.hwaddr)

To make the device persistent::

    tun:persist(true)

To bring up the device::

    tun:up()

To bring down the device::

    tun:down()

To read/write to the device, use the methods ``read(size)`` and
``write(buf)``::

    buf = tun:read(tun.mtu)
    tun:write(buf)

To close the device::

    tun:close()

You can also get the file descriptor associated with a TUN/TAP device::

    tun:fileno()

