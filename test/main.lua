local hyperparser = require "hyperparser"
  
local settings = {
  msgcomplete = function()
    print("Message completed.")
  end,
   
  headerfield = function(a)
    io.write(a)
  end,
  
  headervalue = function(a)
    io.write(" -> " .. a .. "\n")
  end
}

-- parse request
local req = "GET /index/?key=val&key2=val2 HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 12\r\n\r\nHello world!"
local parser = hyperparser.request()
local nread, err = parser:execute(req, settings)
if err then
   print("req error: ", err)
end

-- parse url
local url = "http://www.example.com:80/main?name=123#part"
local uparser = hyperparser.parseurl(url)
print("\nurl parsed:", uparser.schema, uparser.host, uparser.port, uparser.path, uparser.query, uparser.fragment, uparser.userinfo)

-- parse response
local res = [["HTTP/1.1 200 OK
Transfer-Encoding: chunked
Vary: Accept-Encoding,User-Agent,Accept
Connection: keep-alive

 <!DOCTYPE HTML>
"]]
local parser = hyperparser.response()
local nread, err = parser:execute(req, settings)
if err then
   print("\nresponse error: ", err)
end
