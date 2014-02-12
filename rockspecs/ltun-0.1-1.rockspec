package = "ltun"
version = "0.1-1"
source = {
    url = "git://github.com/montag451/ltun"
}
description = {
    summary = "Linux TUN/TAP module for Lua.",
    detailed = [[
        ltun is a Lua module which let you create TUN/TAP device very easily.
    ]],
    homepage = "http://github.com/montag451/ltun",
    license = "MIT"
}
dependencies = {
    "lua >= 5.1"
}
build = {
    type = "builtin",
    modules = {
        ltun = "ltun.c"
    }
}

