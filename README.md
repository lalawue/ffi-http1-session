
HTTP1_SESSION was

- HTTP Parser (based on [http-parser](https://github.com/nodejs/http-parser))
- WebSocket Parser

It runs on *BSD and Linux. Windows was not tested.

support LuaJIT's pull-style api, require luarocks to make.

source code are written in [moocscript](https://github.com/lalawue/mooncake), then `./compile.sh` to generate Lua source.

## Install or Compilation

```sh
$ luarocks install ffi-http1-session [--local]
```

Needs luarocks to compile and install:

```
$ [sudo] luarock make
```

## Usage

Please refers to

- tests/test_wired.mooc
- tests/test_mnet.mooc

and run test as

```sh
$ ./tests/test.sh tests/test_wired.mooc
$ ./tests/test.sh tests/test_mnet.mooc
```

## Reference 

- https://github.com/armatys/hyperparser
- https://github.com/tatsuhiro-t/wslay
- https://github.com/mortzdk/Websocket


## Used in

- [Cincau](https://github.com/lalawue/cincau)
<del>- [rpc_framework](https://github.com/lalawue/rpc_framework)</del>
