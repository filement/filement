#!/usr/bin/python2.7
# coding=UTF-8

# Python 3 compatibility.
from __future__ import print_function

import os, time
import httplib, urllib 
import re
import json
import readline
import shlex
import Cookie

from Crypto.Cipher import AES

import sys

production = False

if production:
	#HOST_PASSPORT = "filement.com"
	HOST_PASSPORT = "filement.com"
	HOST_WEB = "filement.com"
	HOST_WEB_WWW = "www." + HOST_WEB
	HOST_DISTRIBUTE2 = "distribute2.filement.com"
	PORT_DISTRIBUTE2 = 80

	#HOST_WEB_WWW = "www" + HOST_WEB
	#HOST_DISTRIBUTE2 = "flmntdistribute.cloudapp.net"
	#HOST_DISTRIBUTE2 = "distribute.filement.com"
	#PORT_DISTRIBUTE2 = 4080
else:
	HOST_PASSPORT = "filement.com"
	HOST_WEB = "flmntdev.com"
	HOST_WEB_WWW = "www." + HOST_WEB
	HOST_DISTRIBUTE2 = "distribute.flmntdev.com"
	PORT_DISTRIBUTE2 = 80

user = "martin@webconnect.bg"
password = "lAS2qal#mc"
device_password = "test"

#user = "nikolanikov@mail.bg"
#password = "nnikov"
#device_password = "nnikov"

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
)

headers = {"Content-Type": "application/x-www-form-urlencoded", "Accept": "text/html,text/plain"};

def base64(data):
	digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"

	data = bytearray(data)
	length = len(data)

	block = 0;
	result = bytearray()

	index = 2
	while (index < length):
		# Store block of 24 bits in block.
		block = (data[index - 2] << 16) | (data[index - 1] << 8) | data[index];

		result += digits[block >> 18];
		result += digits[(block >> 12) & 0x3f];
		result += digits[(block >> 6) & 0x3f];
		result += digits[block & 0x3f];

		index += 3

	suffix = bytearray("....")

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

def query(action, args, uuid = None, session_id=None, auth_id=None, enc=False):
	if session_id:
		if (enc and production):
			global security_key
			cipher = AES.new(security_key.decode("hex"))
			j = json.dumps({"actions": {action: args}, "session_id": session_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}})
			enc = str(cipher.encrypt(j + (16 - len(j) % 16) * "\0"))
			a = base64(enc)
			return ("/" + uuid + "/" if uuid else "/") + session_id + "?" + urllib.quote(json.dumps(base64(enc)))
		else: j = {"actions": {action: args}, "session_id": session_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	elif auth_id: j = {"actions": {action: args}, "auth_id": auth_id, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	else: j = {"actions": {action: args}, "protocol": {"name": "n", "function": "_", "request_id": "0"}}
	return ("/" + uuid + "/?" if uuid else "/?") + urllib.quote(json.dumps(j))

class Device(dict):
	# TODO self["encrypt"]

	def __init__(self, uuid):
		self["uuid"] = uuid

	#def __repr__(self):
	#	return self["uuid"]

	def on(self, address):
		address = re.search(r"(.*):(.*),(.*)", address)
		self["host"] = address.group(1)
		self["port"] = int(address.group(2))
		self["port_https"] = int(address.group(3))

	def off(self):
		del self["host"]
		del self["port"]
		if production: del self["port_https"]

	def __eq__(self, device):
		self["uuid"] == device["uuid"]

	def __hash__(self):
		h = 0
		index = 0
		while (index < len(self["uuid"])):
			h = h * 256 + ord(self["uuid"][index:index+2].decode("hex"))
			index += 2
		return h

	#def _connect(self):
	#	self["_conn"] = httplib.HTTPConnection(self["host"], self["port"])

	def login(self):
		global session, security_key

		conn = httplib.HTTPSConnection(HOST_WEB)
		conn.request("GET", "/private/requests/sessions/generateFsToken.php?session_id=" + session + "&security_key=" + security_key + "&jsonversion=2", urllib.urlencode({}), headers)
		redirect = conn.getresponse()
		token = re.search(r'"OK ([0-9a-f]+)"', redirect.read()).group(1)
		conn.close()

		cipher = AES.new(security_key.decode("hex"), AES.MODE_CBC, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")

		conn = httplib.HTTPConnection(self["host"], self["port"])

		conn.request("GET", query("session.login", {"token": token}, self["uuid"]), urllib.urlencode({}), headers)
		response = conn.getresponse()
		response = body(response.read())
		self["session_id"] = response["session_id"]
		self["encrypt"] = ("encryption" in response)
		conn.request("GET", query("session.grant_login", {"hash": cipher.encrypt(device_password + (16 - len(device_password)) * "\0").encode("hex")}, self["uuid"], self["session_id"], enc=self["encrypt"]))
		response = conn.getresponse()
		conn.close()

	def request(self, action, arguments):
		if ("host" not in self): return # TODO handle non-connected devices

		if ("session_id" not in self): self.login()

		request_query = query(action, arguments, self["uuid"], self["session_id"], enc=self["encrypt"])

		conn = httplib.HTTPConnection(self["host"], self["port"])
		conn.request("GET", request_query, urllib.urlencode({}), headers)
		response = conn.getresponse()
		if (response.status == 403):
			self.login()
			conn = httplib.HTTPConnection(self["host"], self["port"])
			conn.request("GET", request_query, urllib.urlencode({}), headers)
			response = conn.getresponse()
			if (response.status >= 400): return

		result = (response.status, response.read(), {item[0]: item[1] for item in response.getheaders()})
		conn.close()

		return result

def init(username, password):
	print("Inializing...")

	conn = httplib.HTTPSConnection(HOST_PASSPORT)
	conn.request("POST", "/passport/login.php?redirect=https://" + HOST_WEB_WWW, urllib.urlencode({"username": username, "password": password}), headers)
	redirect = conn.getresponse()
	redirect = redirect.getheader("location")
	conn.close()

	redirect = re.split(r"\?", redirect[len("https://"):])

	conn = httplib.HTTPSConnection(redirect[0])
	conn.request("GET", "/?" + redirect[1], urllib.urlencode({}), headers)
	response = conn.getresponse()
	headers["cookie"] = response.getheader("set-cookie")
	conn.close()

	cookie = os.open(username, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0600)
	os.write(cookie, headers["cookie"])
	os.close(cookie)

def body(data):
	return json.loads(re.search(r'_\("\d+",(.+)\);', data).group(1))

# Login.
try:
	cookie = os.open(user, os.O_RDONLY)
	headers["cookie"] = os.read(cookie, os.fstat(cookie).st_size)
	os.close(cookie)
except:
	init(user, password)

# Normalizes path.
def normalize(location):
	# Form absolute path to location.
	if (location[0] != "/"): location = cwd + location

	nodes = location.split("/")[1:]
	index = 0
	while (index < len(nodes)):
		node = nodes[index]
		if (node in ("", ".")): del nodes[index]
		elif (node == ".."):
			del nodes[index]
			if (index):
				index -= 1
				del nodes[index]
		else: index += 1

	return ("/" + "/".join(nodes))

security_key = re.search(r"security_key=([0-9a-f]+);", headers["cookie"]).group(1)
session = re.search(r"PHPSESSID=([0-9a-z]+);", headers["cookie"]).group(1)

# Make sure the cookie is set properly.
c = Cookie.SimpleCookie()
c.load(headers["cookie"])
skip = len("set-cookie: ")
headers["cookie"] = ", ".join((c["PHPSESSID"].output()[skip:], c["security_key"].output()[skip:]))

#conn = httplib.HTTPSConnection(HOST_WEB)
conn = httplib.HTTPConnection(HOST_WEB)
conn.request("GET", "/", urllib.urlencode({}), headers)
response = conn.getresponse()
if (response.status >= 400): init(user, password)
response = response.read()
conn.close()

# Get devices.
devices = json.loads(re.search(r"var __GLOBAL_DEVICES = (.*?);</script>", response).group(1))
uuids = [devices[index]["device_code"] for index in devices if devices[index]["type"] == "pc"]

conn = httplib.HTTPConnection(HOST_DISTRIBUTE2, PORT_DISTRIBUTE2)
conn.request("GET", query("location", {"device": uuids, "ftp": ["1"], "cloud": ["1"]}), urllib.urlencode({}), headers)
response = conn.getresponse().read()
print(response)
conn.close()

devices = {uuid: Device(uuid) for uuid in uuids}
for (uuid, address) in body(response)["device"].items():
	if (address): devices[uuid].on(address)

cwd = "/"
tree = {"/": [["dr--", None, None, uuid] for uuid in devices]}

for uuid in devices:
	device = devices[uuid]

	config = device.request("config.info", None)
	if (not config): continue
	config = config[1]
	if (not config): continue
	tree["/" + uuid + "/"] = [["dr--", None, None, str(block["block_id"])] for block in body(config)["fs.get_blocks"]["blocks"]]

def furi_parse(furi):
	length = len(furi)
	if (length < (1 + 32 + 1 + 1)): return
	uuid = furi[1:33]
	relative = furi[34:].find("/")
	if (relative < 0): furi += "/"
	else: relative += 34
	block_id = int(furi[34:relative])
	return (uuid, block_id, furi[relative:])

def entries_list(locations):
	entries = {}
	for location in locations:
		location = furi_parse(normalize(location))
		if (location[0] not in entries): entries[location[0]] = []
		entries[location[0]].append({"block_id": location[1], "path": location[2]})
	return entries

def shell():
	def fail(message):
		print(message)
		return

	def pwd(*args):
		global tree, cwd
		print(cwd)

	def ls(*args):
		global tree, cwd
		print("\n".join([item[0] + " " + item[3] for item in tree[cwd]]))

	def cd(location):
		global tree, cwd

		location = normalize(location)
		if (location[-1] != "/"): location += "/"

		if (location in tree): cwd = location
		else:
			depth = location.count("/")
			if (depth < 3): return fail("no such directory")
			relative = 34 + location[34:].find("/")
			prefix = location[:relative+1]
			uuid = location[1:33]
			try: block_id = int(location[34:relative])
			except: return fail("no such directory")

			while True:
				if (prefix not in tree):
					response = devices[uuid].request("ffs.list", {"block_id": block_id, "path": prefix[relative:-1], "depth": 1})
					if (response[0] == 200): entries = body(response[1])
					else: return fail("error " + str(response[0]))

					tree[prefix] = [re.split(r" ", entry, 3, re.DOTALL) for entry in entries]
					tree[prefix] = [item[:3] + [item[3][1:]] for item in tree[prefix] if item[3] != "/"]

				index = location[len(prefix):].find("/")
				if (index < 0):
					cwd = location
					break
				prefix = location[:len(prefix)+index+1]

	def get(*locations):
		entries = entries_list(locations)
		if (len(entries) != 1): return
		source = entries.keys()[0]

		response = devices[source].request("ffs.download", entries[source])
		if (response[0] != 200): return fail("no such directory")

		filename = re.match(r'attachment; filename="(.*)"', response[2]["content-disposition"]).group(1)
		cookie = os.open(filename, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0600)
		os.write(cookie, response[1])
		os.close(cookie)

	def cp(*locations):
		entries = entries_list(locations[:-1])
		if (len(entries) != 1): return
		source = entries.keys()[0]

		(dest_uuid, dest_block, dest_path) = furi_parse(normalize(locations[-1]))

		if (source == dest_uuid):
			action = "ffs.copy"
			parameters = {"src": entries[source], "dest": {"block_id": dest_block, "path": dest_path}}
		else:
			# Create authorization key for the source file in the source server.
			auth = devices[source].request("auth.grant", {"count": 1, "rw": 0, "blocks": entries[source]})
			auth = body(auth[1])
			args = query("ffs.archive", [{"block_id": auth["locations"][0]["id"], "path": ""}], source, auth_id=auth["auth_id"], enc=False)

			action = "ffs.transfer"
			parameters = {"host": devices[source]["host"], "port": devices[source]["port"], "uuid": source, "args": args, "src": [entry["path"] for entry in entries[source]], "dest": {"block_id": dest_block, "path": dest_path}}

		response = devices[dest_uuid].request(action, parameters)
		cache = body(response[1])

		# Wait until the copying is finished. On error, display message to the user.
		# TODO this should be asyncrhonous
		while True:
			b = body(devices[dest_uuid].request("cache.get", cache)[1])
			print(b)
			status = ["status"]
			if (status <= 0): break
			time.sleep(1)
		if (status < 0): print(ERROR[-status])

	commands = {
		"pwd": pwd,
		"ls": ls,
		"cd": cd,
		"get": get,
		"cp": cp,
	}

	def complete(text, state):
		#buf = readline.get_line_buffer()

		possible = [f[3] for f in tree[cwd] if f[3].startswith(text)]
		if (state < len(possible)): return possible[state] + "/"

	if 'libedit' in readline.__doc__:
		readline.parse_and_bind("bind ^I rl_complete")
	else:
		readline.parse_and_bind("tab: complete")
	#readline.parse_and_bind("tab: complete")
	readline.set_completer(complete)

	while True:
		args = shlex.split(raw_input("> ").strip())
		if (not len(args)): continue
		cmd = args[0]
		if (cmd == "exit"): break

		if (cmd in commands):
			try:
				commands[cmd](*args[1:])
			except Exception as ex:
				print(ex)
				print("filement: Invalid arguments.")
		else: print("filement: Command not found.")

shell()

"""try: pass
except:
	exc_type, exc_obj, exc_tb = sys.exc_info()
	fname = os.path.split(exc_tb.tb_frame.f_code.co_filename)[1]
	print(exc_type, fname, exc_tb.tb_lineno)"""

"""
import unittest

class TestSequenceFunctions(unittest.TestCase):
	def test(self):
		#

unittest.main()
"""
