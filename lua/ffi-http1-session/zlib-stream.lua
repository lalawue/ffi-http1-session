local FFI = require("ffi")
local Z = require("ffi-http1-session.zlib")
local zlib = Z.zlib
local zlib_err = Z.zlib_err
local math_min = math.min
local ffi_copy = FFI.copy
local ffi_str = FFI.string
local str_char = string.char
local _str_suffix = str_char(0, 0, 0xff, 0xff)
local ZlibStream = { __tn = 'ZlibStream', __tk = 'class', __st = nil }
do
	local __st = nil
	local __ct = ZlibStream
	__ct.__ct = __ct
	__ct.isKindOf = function(c, a) return a and c and ((c.__ct == a) or (c.__st and c.__st:isKindOf(a))) or false end
	-- declare class var and methods
	__ct._in = {  }
	__ct._out = {  }
	__ct._buf_size = 16384
	__ct._window_bits = -15
	function __ct:init(buf_size, window_bits)
		self._buf_size = buf_size or self._buf_size
		self._window_bits = window_bits or self._window_bits
		self._in, self._out = {  }, {  }
		self:resetStream()
	end
	function __ct:destroy()
		self:resetStream(true)
	end
	function __ct:resetStream(no_create)
		local _in = self._in
		local _out = self._out
		if _in.stream then
			zlib.inflateEnd(_in.stream)
			_in.stream = nil
		end
		if _out.stream then
			zlib.deflateEnd(_out.stream)
			_out.stream = nil
		end
		if no_create then
			return 
		end
		_in.stream, _in.inbuf, _in.outbuf = Z.createStream(self._buf_size)
		_out.stream, _out.inbuf, _out.outbuf = Z.createStream(self._buf_size)
		if zlib.Z_OK ~= Z.initInflate(_in.stream, self._window_bits) then
			zlib.inflateEnd(_in.stream)
			return nil
		end
		if zlib.Z_OK ~= Z.initDeflate(_out.stream, { windowBits = self._window_bits }) then
			zlib.inflateEnd(_in.stream)
			zlib.deflateEnd(_out.stream)
			return nil
		end
	end
	function __ct:inflate(in_data)
		if not (type(in_data) == "string" and in_data:len() > 0) then
			return false, "ZLibStream: Invalid params"
		end
		local _inflate = zlib.inflate
		local _in = self._in
		in_data = in_data .. _str_suffix
		local out_data = ""
		repeat
			_in.stream.avail_in = math_min(self._buf_size, #in_data)
			_in.stream.next_in = _in.inbuf
			ffi_copy(_in.inbuf, in_data, _in.stream.avail_in)
			if _in.stream.avail_in >= #in_data then
				in_data = ""
			else 
				in_data = in_data:sub(1 + _in.stream.avail_in)
			end
			_in.stream.avail_out = self._buf_size
			_in.stream.next_out = _in.outbuf
			local err = _inflate(_in.stream, 2)
			local __s = err
			if __s == 0 or __s == 1 or __s == -5 then
				out_data = out_data .. ffi_str(_in.outbuf, (self._buf_size - _in.stream.avail_out))
			else
				return false, "ZLibStream: " .. tostring(zlib_err(err))
			end
		until in_data:len() <= 0
		return true, out_data
	end
	function __ct:deflate(in_data)
		if not (type(in_data) == "string" and in_data:len() > 0) then
			return false, "ZLibStream: Invalid params"
		end
		local _in_size = self._buf_size / 2
		local _deflate = zlib.deflate
		local _out = self._out
		local out_data = ""
		local out_len = 0
		repeat
			_out.stream.avail_in = math_min(_in_size, #in_data)
			_out.stream.next_in = _out.inbuf
			ffi_copy(_out.inbuf, in_data, _out.stream.avail_in)
			if _out.stream.avail_in >= #in_data then
				in_data = ""
			else 
				in_data = in_data:sub(1 + _out.stream.avail_in)
			end
			_out.stream.avail_out = self._buf_size
			_out.stream.next_out = _out.outbuf
			local err = _deflate(_out.stream, 2)
			local __s = err
			if __s == 0 or __s == 1 or __s == -5 then
				out_data = out_data .. ffi_str(_out.outbuf, (self._buf_size - _out.stream.avail_out))
			else
				return false, "ZlibStream: " .. tostring(zlib_err(err))
			end
			out_len = out_len + _out.stream.avail_out
		until in_data:len() <= 0
		if out_len < 5 or out_data:sub(-4) ~= _str_suffix then
			out_data = out_data .. str_char(0x0)
		else 
			out_data = out_data:sub(1, -5)
		end
		return true, out_data
	end
	-- declare end
	local __imt = {
		__tostring = function(t) return "<class ZlibStream" .. t.__ins_name .. ">" end,
		__index = function(t, k)
			local v = __ct[k]
			if v ~= nil then rawset(t, k, v) end
			return v
		end,
	}
	setmetatable(__ct, {
		__tostring = function() return "<class ZlibStream>" end,
		__index = function(t, k)
			local v = __st and __st[k]
			if v ~= nil then rawset(t, k, v) end
			return v
		end,
		__call = function(_, ...)
			local t = {}; t.__ins_name = tostring(t):sub(6)
			local ins = setmetatable(t, __imt)
			if type(rawget(__ct,'init')) == 'function' and __ct.init(ins, ...) == false then return nil end
			return ins
		end,
	})
end
return ZlibStream
