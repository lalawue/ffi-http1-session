local HP = require("ffi-hyperparser")

print("version: ", HP.version())
  
do
  local req = "GET /index/?key=val&key2=val2 HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 12\r\n\r\nHello world!"
  local parser = HP.createParser("REQUEST")
  local nread, state, htbl = parser:process(req)
  print(string.format("\n---- request, readed:%d, state:%d", nread, state))
  for k, v in pairs(htbl) do
    print(k .. ': ' .. tostring(v))
    if type(v) == "table" then
      for m, n in pairs(v) do
        print("\t", m .. ": " .. n)
      end
    end
  end
end

do
  local res = [[HTTP/1.1 403 Forbidden
Date: Tue, 29 Jun 2021 15:12:36 GMT
Content-Type: text/html
Content-Length: 236
Connection: keep-alive
Server: web cache
Expires: Tue, 29 Jun 2021 15:12:36 GMT
X-Ser: BC22_dx-guangdong-zhuhai-16-cache-5
Cache-Control: no-cache,no-store,private
cdn-user-ip: 218.18.163.7
cdn-ip: 125.89.76.22
X-Cache-Remote: HIT
cdn-source: baishan

<html><head><title>ERROR: ACCESS DENIED</title></head><body><center><h1>ERROR: ACCESS DENIED</h1></center><hr>
<center>Tue, 29 Jun 2021 15:12:36 GMT (taikoo/BC22_dx-guangdong-zhuhai-16-cache-5)</center></BODY></HTML>
<!-- web cache -->
]]
local parser = HP.createParser("RESPONSE")
local nread, state, htbl = parser:process(res)
print(string.format("\n---- response readed:%d, state:%d", nread, state))
for k, v in pairs(htbl) do
  print(k, v)
end
end

-- parse url
-- local url = "http://www.example.com:80/main?name=123#part"
-- local uparser = HP.parseurl(url)
-- print("\nurl parsed:", uparser.schema, uparser.host, uparser.port, uparser.path, uparser.query, uparser.fragment, uparser.userinfo)