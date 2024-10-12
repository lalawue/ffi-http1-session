#
# install mooncake(moocscript) first

export LUA_PATH="./lua/?.lua;./lua/?/init.lua;./lib/?.lua;"
export LUA_CPATH="./?.so"
echo "> rm http1_session.so"
rm -f http1_session.so
echo "> gcc -Wall -fPIC -shared -o http1_session.so -I./src src/*.c"
gcc -Wall -fPIC -shared -o http1_session.so -I./src src/*.c
moocscript $*
