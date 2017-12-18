#!/usr/bin/env python

from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import os
import re
import readline
import shlex
from getpass import getpass

from filement import Filement, Device, body

username = "martinkunev@gmail.com"

filement = Filement(username)
if not filement.connected:
	password = getpass("Enter password for " + username + ": ")
	filement.connect(password)

devices = filement.devices()
uuids = [devices[index]["device_code"] for index in devices if devices[index]["type"] == "pc"]

devices = {uuid: filement.device(uuid) for uuid in uuids}
for (uuid, address) in filement.locations(uuids).items():
	if (address):
		devices[uuid].on(address)

# TODO use proxy only when necessary

import fvfs

fs = fvfs.Fs(filement)

cwd = "/"
tree = {"/": [[str(devices[uuid]), None, None, uuid] for uuid in devices]}

for uuid in devices:
	device = devices[uuid]

	config = device.request("config.info", None)
	if (not config): continue
	config = config[1]
	if (not config): continue
	tree["/" + uuid + "/"] = [["dr--", None, None, str(block["block_id"])] for block in body(config)["fs.get_blocks"]["blocks"]]

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
		global cwd
		print(cwd)

	def ls(*args):
		global cwd
		files = fs.ls(cwd)
		print("\n".join(files[name]["mode"] + " " + name for name in sorted(files)))

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
		source_uuid = entries.keys()[0]

		(dest_uuid, dest_block, dest_path) = furi_parse(normalize(locations[-1]))

		status = devices[dest_uuid].copy(devices[source_uuid], entries[source_uuid], dest_block, dest_path)
		print(status)

	# os.path.normpath

	def complete(text, state):
		# TODO this functions is called as many times as there are entries; this shouldn't be necessary

		index = len(text)
		while index > 0:
			index -= 1
			if text[index] == "/":
				break

		path = text[:index]
		text = text[index + (index != 0):]

		# TODO add ..
		files = fs.ls(os.path.normpath(cwd + path))
		possible = [name for name in files if name.startswith(text)]
		if (state < len(possible)): return (path + "/" if path else "") + possible[state] + "/"

	commands = {
		"pwd": pwd,
		"ls": ls,
		"cd": cd,
		"get": get,
		"cp": cp,
	}

	if 'libedit' in readline.__doc__:
		readline.parse_and_bind("bind ^I rl_complete")
	else:
		readline.parse_and_bind("tab: complete")
	#readline.parse_and_bind("tab: complete")
	readline.set_completer_delims(" ")
	readline.set_completer(complete)

	while True:
		try:
			args = shlex.split(raw_input("> ").strip())
		except EOFError:
			break

		if (not len(args)): continue
		cmd = args[0]
		if (cmd == "exit"): break

		if (cmd in commands):
			#try:
			commands[cmd](*args[1:])
			#except Exception as ex:
			#	raise ex
			#	print(ex)
			#	print("filement: Invalid arguments.")
		else: print("filement: Command not found.")

shell()
