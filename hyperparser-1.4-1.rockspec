package = "hyperparser"
version = "1.4-1"
source = {
   url = "git+https://github.com/lalawue/hyperparser.git"
}
description = {
   summary = "Socket utilities",
   detailed = "Lua bindings to http parser library",
   homepage = "https://github.com/lalawue/hyperparser",
   license = "MIT/X11"
}
dependencies = {
    "lua >= 5.1"
}
supported_platforms = {
   "macosx", "freebsd", "linux"
}
build = {
   type = "builtin",
   modules = {
      hyperparser = {
         ffi_hyperparser = {
            "ffi_hyperparser.lua"
         },
         sources = {
            "src/http_parser.c",
            "src/pull_style_api.c"
         }
      }
   }
}
