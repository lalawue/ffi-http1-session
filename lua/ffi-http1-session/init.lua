local FFI = require("ffi")
local ZlibStream = require("ffi-http1-session.zlib-stream")
FFI.cdef([[
    typedef struct s_mssn_data {
        int length;
        struct s_mssn_data *next;
        uint8_t *data;
    } mssn_data_t;

    typedef struct s_mssn_header {
        const char *key;
        const char *value;
        struct s_mssn_header *next;
    } mssn_header_t;

    typedef enum {
        HTTP_FRAME_FIN = 1,   // HTTP BODY finished
        HTTP_FRAME_CONS = 2,  // HTTP BODY chunked data
        WS_FRAME_PING = 3,    // WS PING frame
        WS_FRAME_PONG = 4,    // WS PONG frame
        WS_FRAME_FIN = 5,     // WS fin data frame
        WS_FRAME_CONS = 6     // WS contines data frame
    } mssn_frame_type;

    typedef struct s_mssn_frame {
        mssn_frame_type ftype;      // frame type
        mssn_data_t *data;          // frame data
        mssn_data_t *data_last;     // frame data last
        struct s_mssn_frame *next;  // next frame
    } mssn_frame_t;

    typedef enum {
        MSSN_STATE_INIT = 0,
        MSSN_STATE_BEGIN,  // has begin process data
        MSSN_STATE_HEADER, // header complete
        MSSN_STATE_BODY,   // begin body/frame data
        MSSN_STATE_FINISH, // HTTP body complete, WEBSOCKET frame closed
        MSSN_STATE_ERROR,  // parsing error, you should close context
    } mssn_state_t;

    typedef struct {
        mssn_state_t state;     // state for last processing
        const char *method;     // method
        const char *path;       // path
        int status;             // http status code
        int upgrade;            // upgrade to websocket
        mssn_header_t *headers; // header
        mssn_frame_t *frames;   // frames for last processing
        char *error_msg;        // error message
        void *opaque;           // internal use
    } mssn_t;

    /// @brief create context
    /// @return context
    mssn_t* mssn_create(int);

    /// @brief close context
    void mssn_close(mssn_t *ctx);

    /// @brief return data consumed
    // - return < 0, encounter underlying connection error
    // - return = 0, need more data
    // - return > 0, has consume data bytes, and complete frames in context
    int mssn_process(mssn_t *, const uint8_t *data, int data_length);

    /// @brief build websocket binary frame data, data will be fragment but control frame
    /// @param ctx context
    /// @param ftype websocket frame type
    /// @param rsv_bits rsv 3 bits
    /// @param frame_size max frame size including websocket header
    /// @param buf data to be send
    /// @param buf_len data length
    /// @return every mssn_data_t is a compact websocket frame
    mssn_data_t *mssn_build(mssn_t *ctx,
                            mssn_frame_type ftype,
                            int rsv_bits,
                            size_t frame_size,
                            const uint8_t *buf,
                            size_t buf_len);

    /// @brief reclaim frames, headers, datas if needed
    /// @param ctx context
    /// @param data_build data from mssn_build
    void mssn_reclaim(mssn_t *ctx, mssn_data_t *data_build);

    void mssn_sha1(const uint8_t *data, int data_len, uint8_t *digest);
]])
local ret, mlib = nil, nil
do
	local suffix = (jit.os == "Windows") and "dll" or "so"
	for cpath in package.cpath:gmatch("[^;]+") do
		local path = cpath:sub(1, cpath:len() - 2 - suffix:len()) .. "http1_session." .. suffix
		ret, mlib = pcall(FFI.load, path)
		if ret then
			goto SUCCESS_LOAD_LABEL
		end
	end
	error(mlib)
	::SUCCESS_LOAD_LABEL::
end
local type = type
local pairs = pairs
local assert = assert
local tonumber = tonumber
local setmetatable = setmetatable
local tbl_insert = table.insert
local sfmt = string.format
local math_min = math.min
local ffi_str = FFI.string
local ffi_copy = FFI.copy
local sha1_buf = FFI.new("uint8_t[?]", 20)
local Http1Session = { __tn = 'Http1Session', __tk = 'class', __st = nil }
do
	local __st = nil
	local __ct = Http1Session
	__ct.__ct = __ct
	__ct.isKindOf = function(c, a) return a and c and ((c.__ct == a) or (c.__st and c.__st:isKindOf(a))) or false end
	-- declare class var and methods
	__ct.STATE_INIT = 0
	__ct.STATE_BEGIN = 1
	__ct.STATE_HEADER = 2
	__ct.STATE_BODY = 3
	__ct.STATE_FINISH = 4
	__ct.STATE_ERROR = 5
	function __ct:init(server, compress)
		self._lib = mlib.mssn_create(server and 1 or 0)
		self._data = ""
		self._tbl = {  }
		self._upgrade = false
		self._state = Http1Session.STATE_INIT
		if compress then
			self._zstream = ZlibStream()
		else 
			self._zstream = nil
		end
		self._sec_key_raw = ""
	end
	function __ct:deinit()
		self:closeSession()
	end
	function __ct:closeSession()
		self:reclaim(true)
		self._upgrade = false
		if self._lib ~= nil then
			mlib.mssn_close(self._lib)
			self._lib = nil
		end
		if self._zstream ~= nil then
			self._zstream:destroy()
			self._zstream = nil
		end
	end
	function __ct:isUpgrade()
		return self._upgrade == true
	end
	function __ct:state()
		return self._state
	end
	function __ct:basicSecAcceptHeader(base64_sec_key)
		if not (self._upgrade and type(base64_sec_key) == "string") then
			return 
		end
		local http_resp = "HTTP/1.1 101 Web Socket Protocol Handshake" .. "\r\n"
		if self._zstream ~= nil then
			http_resp = http_resp .. "Sec-Websocket-Extensions: permessage-deflate; client_max_window_bits=15" .. "\r\n"
		end
		http_resp = http_resp .. "Sec-Websocket-Accept: " .. tostring(base64_sec_key) .. "\r\n" .. "Upgrade: websocket" .. "\r\n" .. "Connection: Upgrade" .. "\r\n"
		return http_resp
	end
	function __ct:process(data)
		if not ((type(data) == "string") and (data:len() > 0) and (self._lib ~= nil)) then
			return -1, "[HSSN] Invalid params"
		end
		local _lib = self._lib
		local nread, ret = 0, 0
		data = self._data .. data
		repeat
			ret = tonumber(mlib.mssn_process(_lib, data, data:len()))
			if ret > 0 then
				nread = nread + ret
				data = (data:len() > ret) and data:sub(ret + 1) or ""
			end
		until (ret <= 0) or (data:len() <= 0)
		self._data = data
		local _tbl = self._tbl
		if _lib.state >= self.STATE_HEADER and _tbl.headers == nil then
			if _lib.method == nil and _lib.path == nil then
				_tbl.method = nil
				_tbl.path = nil
				_tbl.status = tonumber(_lib.status)
			else 
				_tbl.method = ffi_str(_lib.method)
				_tbl.path = ffi_str(_lib.path)
				_tbl.status = 0
			end
			_tbl.upgrade = tonumber(_lib.upgrade)
			if _lib.headers ~= nil then
				_tbl.headers = {  }
				local hnode = _lib.headers
				repeat
					local hnext = hnode.next
					_tbl.headers[ffi_str(hnode.key)] = ffi_str(hnode.value)
					hnode = hnext
				until hnode == nil
			else 
				_tbl.headers = nil
			end
		end
		if not self._upgrade and _tbl.upgrade then
			self._upgrade = true
			self:_initWebSocket(_tbl)
		end
		if _lib.state >= self.STATE_BODY and _lib.frames ~= nil then
			local fr_tbl = {  }
			local fnode = _lib.frames
			repeat
				local f = { ftype = self:_ftypeNumberToString(fnode.ftype), data = "" }
				local dnode = fnode.data
				repeat
					f.data = f.data .. ffi_str(dnode.data, dnode.length)
					dnode = dnode.next
				until dnode == nil
				if self._zstream ~= nil and f.data:len() > 0 then
					local zret, zdata = self._zstream:inflate(f.data)
					if zret then
						f.data = zdata
					else 
						f.data = nil
					end
				end
				tbl_insert(fr_tbl, f)
				fnode = fnode.next
			until fnode == nil
			_tbl.frames = fr_tbl
		else 
			_tbl.frames = nil
		end
		if _lib.frames ~= nil then
			mlib.mssn_reclaim(self._lib, nil)
		end
		self._state = _lib.state
		return nread, _tbl
	end
	function __ct:secWebSocketKeyRaw()
		return self._sec_key_raw
	end
	function __ct:_lowerValue(htbl, key)
		local value = htbl.headers[key]
		if type(value) == "string" then
			return value:lower()
		end
		return ""
	end
	function __ct:_initWebSocket(htbl)
		if not (self:_lowerValue(htbl, "Connection") == "upgrade" and self:_lowerValue(htbl, "Upgrade") == "websocket") then
			return 
		end
		if self._sec_key_raw:len() <= 0 then
			local skey = htbl.headers['Sec-WebSocket-Key']
			if type(skey) == "string" then
				self._sec_key_raw = self:sha1(skey .. "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
			end
		end
		if self._zstream == nil then
			local encoding = self:_lowerValue(htbl, "Accept-Encoding")
			if encoding:find("deflate") ~= nil or encoding:find("gzip") ~= nil then
				self._zstream = ZlibStream()
			end
		end
	end
	function __ct:_ftypeNumberToString(ftype)
		local __s = ftype
		if __s == 1 then
			return "BODY"
		elseif __s == 2 then
			return "PING"
		elseif __s == 3 then
			return "PONG"
		elseif __s == 4 then
			return "CLOSE"
		elseif __s == 5 then
			return "TEXT"
		elseif __s == 6 then
			return "BINARY"
		else
			return nil
		end
	end
	function __ct:_ftypeStringToNumber(ftypeString)
		local __s = ftypeString
		if __s == "BODY" then
			return 1
		elseif __s == "PING" then
			return 2
		elseif __s == "PONG" then
			return 3
		elseif __s == "CLOSE" then
			return 4
		elseif __s == "TEXT" then
			return 5
		elseif __s == "BINARY" then
			return 6
		else
			return nil
		end
	end
	function __ct:build(ftype, fsize, data, rsv_bits)
		if not (self._lib ~= nil and type(ftype) == "string" and type(fsize) == "number" and type(data) == "string" and fsize > 0) then
			return false, "[HSSN] Invalid params"
		end
		rsv_bits = rsv_bits or 0
		ftype = self:_ftypeStringToNumber(ftype)
		if not (ftype) then
			return false, "[HSSN] Invalid frame type"
		end
		if self._zstream ~= nil and data:len() > 0 then
			local zret, zdata = self._zstream:deflate(data)
			if zret then
				data = zdata
				rsv_bits = rsv_bits + 4
			end
		end
		local head = mlib.mssn_build(self._lib, ftype, rsv_bits, fsize, data, data:len())
		if not (head ~= nil) then
			return false, ffi_str(self._lib.error_msg)
		end
		local ftbl = {  }
		local it = head
		repeat
			local nit = it.next
			tbl_insert(ftbl, ffi_str(it.data, it.length))
			it = nit
		until it == nil
		mlib.mssn_reclaim(self._lib, head)
		return true, ftbl
	end
	function __ct:reclaim(force)
		if force or self._tbl.upgrade == 0 then
			self._tbl.status = 0
			self._tbl.method = nil
			self._tbl.path = nil
			self._tbl.headers = nil
		end
		self._tbl.frames = nil
		self._data = ""
	end
	function __ct:sha1(data)
		mlib.mssn_sha1(data, data:len(), sha1_buf)
		return ffi_str(sha1_buf, 20)
	end
	-- declare end
	local __imt = {
		__tostring = function(t) return "<class Http1Session" .. t.__ins_name .. ">" end,
		__index = function(t, k)
			local v = __ct[k]
			if v ~= nil then rawset(t, k, v) end
			return v
		end,
		__gc = function(t) t:deinit() end,
	}
	setmetatable(__ct, {
		__tostring = function() return "<class Http1Session>" end,
		__index = function(t, k)
			local v = __st and __st[k]
			if v ~= nil then rawset(t, k, v) end
			return v
		end,
		__call = function(_, ...)
			local t = {}; t.__ins_name = tostring(t):sub(6)
			local ins = setmetatable(t, __imt)
			if type(rawget(__ct,'init')) == 'function' and __ct.init(ins, ...) == false then return nil end
			if _VERSION == "Lua 5.1" then
				rawset(ins, '__gc_proxy', newproxy(true))
				getmetatable(ins.__gc_proxy).__gc = function() ins:deinit() end
			end
			return ins
		end,
	})
end
return Http1Session
