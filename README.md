
HTTP1_SESSION was

- HTTP Parser (based on [http-parser](https://github.com/nodejs/http-parser))
- WebSocket Parser

It runs on *BSD and Linux. Windows was not tested.

support LuaJIT's pull-style api, require luarocks to make.

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

## Origin

from https://github.com/lalawue/ffi-hyperparser

## Used in

- [Cincau](https://github.com/lalawue/cincau)
<del>- [rpc_framework](https://github.com/lalawue/rpc_framework)</del>
