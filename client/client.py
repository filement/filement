#!/usr/bin/env python

from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import os
import re
import readline
import shlex
from getpass import getpass

from filement import Filement
import fvfs

username = "martinkunev@gmail.com"

filement = Filement(username)
if not filement.connected:
	password = getpass("Enter password for " + username + ": ")
	filement.connect(password)

# TODO use proxy only when necessary

fs = fvfs.Fs(filement)

cwd = "/"

def normalize(location):
	if (not location) or (location[0] != "/"):
		location = cwd + location
	return os.path.normpath(location)

def furi_parse(furi):
	length = len(furi)
	if (length < (1 + 32 + 1 + 1)): return
	uuid = furi[1:33]
	relative = furi[34:].find("/")
	if (relative < 0): furi += "/"
	else: relative += 34
	block_id = int(furi[34:relative])
	return (uuid, block_id, furi[relative:])

def shell():
	def fail(message):
		print(message)
		return

	def pwd(*args):
		global cwd
		print(cwd)

	def cd(*locations):
		global cwd

		if len(locations) != 1:
			return
		location = locations[0]

		path = os.path.normpath(location if (location[0] == "/") else (cwd + location))
		if fs.populate(path) != False:
			cwd = path + "/"
		else:
			print("cd error")

	def ls(*args):
		global cwd

		def ls_location(location):
			path = os.path.normpath(cwd + location)
			files = fs.populate(path)
			print("\n".join(files[name]["mode"] + " " + name for name in sorted(files)))

		if len(args) > 1:
			first = True
			for location in args:
				if first:
					first = False
				else:
					print("\n")
				print(location + ":")
				ls_location(location)
		elif args:
			ls_location(args[0])
		else:
			ls_location("")

	def entries_list(locations):
		entries = {}
		for location in locations:
			location = furi_parse(normalize(location))
			if (location[0] not in entries): entries[location[0]] = []
			entries[location[0]].append({"block_id": location[1], "path": location[2]})
		return entries

	def get(*locations):
		# TODO return error if the location is not within a block

		entries = entries_list(locations)
		if (len(entries) != 1): return
		source = entries.keys()[0]

		container = normalize(locations[0])
		if container[-1] != "/":
			container = container[:container.rfind("/")]
		fs.populate(container)

		response = fs.tree[""]["children"][source]["device"].request("ffs.download", entries[source], raw=True)
		if (response[0] != 200):
			return fail("no such directory")

		filename = re.match(r'attachment; filename="(.*)"', response[2]["content-disposition"]).group(1)
		cookie = os.open(filename, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0600)
		os.write(cookie, response[1])
		os.close(cookie)

	# TODO implement a function getting device object by uuid

	def cp(*locations):
		entries = entries_list(locations[:-1])
		if (len(entries) != 1): return
		source_uuid = entries.keys()[0]

		(dest_uuid, dest_block, dest_path) = furi_parse(normalize(locations[-1]))

		container = normalize(locations[-1])
		if container[-1] != "/":
			container = container[:container.rfind("/")]
		fs.populate(container)
		if source_uuid != dest_uuid:
			container = normalize(locations[0])
			if container[-1] != "/":
				container = container[:container.rfind("/")]
			fs.populate(container)

		status = fs.tree[""]["children"][dest_uuid]["device"].copy(fs.tree[""]["children"][dest_uuid]["device"], entries[source_uuid], dest_block, dest_path)
		print(status)

	def complete(text, state):
		# TODO this functions is called as many times as there are entries; this shouldn't be necessary

		index = len(text)
		while index > 0:
			index -= 1
			if text[index] == "/":
				break

		path = text[:index]
		text = text[index + (index != 0):]

		files = fs.populate(normalize(path))
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
