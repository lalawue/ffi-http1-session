moocscript -v 1> /dev/null 2>&1
RET=$?

if [ $RET -eq 0 ]; then
    mkdir -p lua/ffi-http1-session
    echo "moocscript -s ffi-htt1-session.mooc > ./lua/ffi-http1-session/init.lua"
    moocscript -s ffi-http1-session.mooc > ./lua/ffi-http1-session/init.lua
    echo "moocscript -s ffi-zlib-stream.mooc > ./lua/ffi-http1-session/zlib-stream.lua"
    moocscript -s ffi-zlib-stream.mooc > ./lua/ffi-http1-session/zlib-stream.lua
else
    echo "please install mooncake (moocscript) from luarocks"
fi