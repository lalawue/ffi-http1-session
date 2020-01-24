mkdir build
gcc -O2 -Isrc src\http_parser.c src\pull_style_api.c -shared -fPIC -o build\libhyperparser.dll
