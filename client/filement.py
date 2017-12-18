#!/usr/bin/env python

from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import os, time
import re
import httplib, urllib
import json
import Cookie
from getpass import getpass
from Crypto.Cipher import AES

# TODO don't crash on socket.error

production = True
if production:
	HOST_PASSPORT = "filement.com"
	HOST_WEB = "filement.com"
	HOST_WEB_WWW = "www." + HOST_WEB
	HOST_DISTRIBUTE2 = "distribute2.filement.com"
	PORT_DISTRIBUTE2 = 80
else:
	HOST_PASSPORT = "filement.com"
	HOST_WEB = "flmntdev.com"
	HOST_WEB_WWW = "www." + HOST_WEB
	HOST_DISTRIBUTE2 = "distribute.flmntdev.com"
	PORT_DISTRIBUTE2 = 80

ERROR = (
	"success",								# 0
	"no sufficient system resources",		# -1
	"invalid input data",					# -2
	"access denied",						# -3
	"filesystem entry missing",				# -4
	"filesystem entry exists",				# -5
	"internal filesystem error",			# -6
	"try again",							# -7
	"request not suported",					# -8
	"read error",							# -9
	"write error",							# -10
	"request cancelled",					# -11
	"operation in progress",				# -12
	"domain resolve error",					# -13
	"network operation failed",				# -14
	"invalid upstream server response",		# -15
	"invalid session",						# -16
)

headers = {"Content-Type": "application/x-www-form-urlencoded", "Accept": "text/html,text/plain"};

def base64(data):
	digits = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"

	data = bytearray(data)
	length = len(data)

	block = 0;
	result = bytearray(b"")

	index = 2
	while (index < length):
		# Store block of 24 bits in block.
		block = (data[index - 2] << 16) | (data[index - 1] << 8) | data[index];

		result += digits[block >> 18];
		result += digits[(block >> 12) & 0x3f];
		result += digits[(block >> 6) & 0x3f];
		result += digits[block & 0x3f];

		index += 3

	suffix = bytearray(b"....")

	# Encode the remaining bytes.
	block = 0;
	remaining = index - length
	if (remaining == 2): # no more bytes
		return str(result);
	elif (remaining == 0): # 2 bytes remaining
		block |= (data[index - 1] << 8);
		suffix[2] = digits[(block >> 6) & 0x3f];
	else: pass # 1 byte remaining

	block |= (data[index - 2] << 16);
	suffix[0] = digits[block >> 18];
	suffix[1] = digits[(block >> 12) & 0x3f];
	return str(result + suffix)

def query(action, args, uuid = None, session_id=None, auth_id=None, enc=False, security_key=None):
	if session_id:
		if (enc and production):
			cipher = AES.new(security_key.decode("hex"))
			j = json.dumps({"actions": {action: args}, "session_id": session_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}})
			enc = str(cipher.encrypt(j + (16 - len(j) % 16) * "\0"))
			a = base64(enc)
			return ("/" + uuid + "/" if uuid else "/") + session_id + "?" + urllib.quote(json.dumps(base64(enc)))
		else: j = {"actions": {action: args}, "session_id": session_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	elif auth_id: j = {"actions": {action: args}, "auth_id": auth_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	else: j = {"actions": {action: args}, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	return ("/" + uuid + "/?" if uuid else "/?") + urllib.quote(json.dumps(j))

def body(data):
	return json.loads(re.search(r'_\("\d+",(.+)\);', data).group(1))

class Filement():
	def __init__(self, username):
		self.user = username
		self.headers = headers

		self.connected = False

		# Check if there is a recent cookie for the user.
		if os.path.isfile(self.user):
			mtime = os.stat(self.user).st_mtime
			now = time.time()
			if (now - mtime) <= 7200: # 2 hours
				os.utime(self.user, None)

				cookie = os.open(self.user, os.O_RDONLY)
				self.headers["cookie"] = os.read(cookie, os.fstat(cookie).st_size)
				os.close(cookie)

				self.connected = True
				self._prepare()
			else:
				os.unlink(self.user)

	def _prepare(self):
		self.security_key = re.search(r"security_key=([0-9a-f]+);", self.headers["cookie"]).group(1)
		self.session = re.search(r"PHPSESSID=([0-9a-z]+);", self.headers["cookie"]).group(1)

		# Make sure the cookie is set properly.
		c = Cookie.SimpleCookie()
		c.load(self.headers["cookie"])
		skip = len("set-cookie: ")
		self.headers["cookie"] = ", ".join((c["PHPSESSID"].output()[skip:], c["security_key"].output()[skip:]))

	def connect(self, password):
		conn = httplib.HTTPSConnection(HOST_PASSPORT)
		conn.request("POST", "/passport/login.php?redirect=https://" + HOST_WEB_WWW, urllib.urlencode({"username": self.user, "password": password}), self.headers)
		redirect = conn.getresponse()
		redirect = redirect.getheader("location")
		conn.close()

		redirect = re.split(r"\?", redirect[len("https://"):])

		conn = httplib.HTTPSConnection(redirect[0])
		conn.request("GET", "/?" + redirect[1], urllib.urlencode({}), self.headers)
		response = conn.getresponse()
		self.headers["cookie"] = response.getheader("set-cookie")
		conn.close()

		cookie = os.open(self.user, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0600)
		os.write(cookie, self.headers["cookie"])
		os.close(cookie)

		self.connected = True
		self._prepare()

	def devices(self):
		#conn = httplib.HTTPSConnection(HOST_WEB)
		conn = httplib.HTTPConnection(HOST_WEB)
		conn.request("GET", "/", urllib.urlencode({}), self.headers)
		response = conn.getresponse()
		#if (response.status >= 400): init(user, password)
		response = response.read()
		conn.close()

		devices = json.loads(re.search(r"var __GLOBAL_DEVICES = (.*?);</script>", response).group(1))

		# TODO fix event subscription
		"""uuids = [devices[index]["device_code"] for index in devices if devices[index]["type"] == "pc"]
		conn = httplib.HTTPConnection(HOST_DISTRIBUTE2, PORT_DISTRIBUTE2)
		conn.request("GET", query("subscribe_client", uuids[0], security_key=self.security_key), urllib.urlencode({}), self.headers)
		response = conn.getresponse().read()
		conn.close()
		online = json.loads(re.search(r"try\{receive\((.*)\);\}catch\(e\)\{receive\(false\);\}", response).group(1))
		print(online)"""
		# responses:
		# {"receiver":"devices","action":"changeproxy","device_id":"822334bfd6135c33c6b469aad9258206","proxy":"sofia1.proxy.filement.com:80"}
		# {u'action': u'noproxy', u'device_id': u'822334bfd6135c33c6b469aad9258206', u'receiver': u'devices'}

		return devices

	def locations(self, uuids):
		conn = httplib.HTTPConnection(HOST_DISTRIBUTE2, PORT_DISTRIBUTE2)
		conn.request("GET", query("location", {"device": uuids, "ftp": ["1"], "cloud": ["1"]}, security_key=self.security_key), urllib.urlencode({}), self.headers)
		response = conn.getresponse().read()
		conn.close()
		return body(response)["device"]

class Device():
	def __init__(self, filement, uuid, host, port, password):
		self.filement = filement
		self.uuid = uuid
		self.host = host
		self.port = port
		self.password = password

		self.login()

	def login(self):
		conn = httplib.HTTPSConnection(HOST_WEB)
		conn.request("GET", "/private/requests/sessions/generateFsToken.php?session_id=" + self.filement.session + "&security_key=" + self.filement.security_key + "&jsonversion=2", urllib.urlencode({}), headers)
		redirect = conn.getresponse()
		token = re.search(r'"OK ([0-9a-f]+)"', redirect.read()).group(1)
		conn.close()

		cipher = AES.new(self.filement.security_key.decode("hex"), AES.MODE_CBC, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")

		conn = httplib.HTTPConnection(self.host, self.port)
		conn.request("GET", query("session.login", {"token": token}, self.uuid, security_key=self.filement.security_key), urllib.urlencode({}), headers)
		response = conn.getresponse()
		response = body(response.read())

		self.session_id = response["session_id"]
		self.encrypt = ("encryption" in response)
		conn.request("GET", query("session.grant_login", {"hash": cipher.encrypt(self.password + (16 - len(self.password)) * "\0").encode("hex")}, self.uuid, self.session_id, enc=self.encrypt, security_key=self.filement.security_key))
		response = conn.getresponse()
		conn.close()

	def request(self, action, arguments, raw=False):
		request_query = query(action, arguments, self.uuid, self.session_id, enc=self.encrypt, security_key=self.filement.security_key)

		conn = httplib.HTTPConnection(self.host, self.port)
		conn.request("GET", request_query, urllib.urlencode({}), headers)
		response = conn.getresponse()
		if (response.status == 403):
			self.login()
			conn = httplib.HTTPConnection(self.host, self.port)
			conn.request("GET", request_query, urllib.urlencode({}), headers)
			response = conn.getresponse()
			if (response.status >= 400):
				return

		if raw:
			result = (response.status, response.read(), {item[0]: item[1] for item in response.getheaders()})
		else:
			result = (response.status, body(response.read()), {item[0]: item[1] for item in response.getheaders()})
		conn.close()

		return result

	def copy(self, source, entry, dest_block, dest_path):
		action = None
		parameters = None

		if (source.uuid == self.uuid):
			action = "ffs.copy"
			parameters = {"src": entry, "dest": {"block_id": dest_block, "path": dest_path}}
		else:
			# Create authorization key for the source file in the source server.
			auth = source.request("auth.grant", {"count": 1, "rw": 0, "blocks": entry})
			auth = auth[1]
			args = query("ffs.archive", [{"block_id": auth["locations"][0]["id"], "path": ""}], source.uuid, auth_id=auth["auth_id"], enc=False, security_key=self.filement.security_key)

			action = "ffs.transfer"
			parameters = {"host": source.host, "port": source.port, "uuid": source.uuid, "args": args, "src": [e["path"] for e in entry], "dest": {"block_id": dest_block, "path": dest_path}}

		response = self.request(action, parameters)
		cache = response[1]

		# Wait until the copying is finished. On error, display message to the user.
		# TODO this should be asyncrhonous?
		while True:
			b = self.request("cache.get", cache)[1]
			print(b)
			status = b["status"]
			if (status <= 0): break
			time.sleep(1)
		return ERROR[-status]
