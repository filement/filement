#!/usr/bin/env python

from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import os
import re
import json

from filement import Device

ROOT = 0
DEVICE = 1
BLOCK = 2
FILE = 3

class Fs():
	def __init__(self, filement):
		self.tree = {"": {"type": ROOT}}
		self.filement = filement

	def populate(self, absolutepath):
		if absolutepath[-1] == "/": # remove terminating slash
			absolutepath = absolutepath[:-1]

		# TODO add ..

		nodes = absolutepath.split("/")
		nodes_len = len(nodes)
		node_index = 0
		tree = self.tree
		while node_index < nodes_len:
			component = nodes[node_index]
			if component not in tree:
				return False # not found

			tree = tree[component]
			if "children" not in tree:
				if tree["type"] == ROOT:
					devices = self.filement.devices() # TODO what if this fails
					devices = {devices[index]["device_code"]: {"type": DEVICE, "mode": "d---"} for index in devices if devices[index]["type"] == "pc"}

					locations = self.filement.locations(devices.keys()) # TODO what if this fails
					for uuid in locations:
						address = re.search(r"(.*):(.*),(.*)", locations[uuid])
						devices[uuid]["host"] = address.group(1)
						devices[uuid]["port_http"] = int(address.group(2))
						devices[uuid]["port_https"] = int(address.group(3))
						devices[uuid]["mode"] = "drwx"

					# TODO start a thread to monitor connectivity change

					tree["children"] = devices
				elif tree["type"] == DEVICE:
					if "host" not in tree:
						return False # device offline

					tree["device"] = Device(self.filement, component, tree["host"], tree["port_http"], "test") # TODO dynamic password

					config = tree["device"].request("config.info", None)
					#if (not config): continue
					#config = config[1]
					#if (not config): continue

					blocks = {str(block["block_id"]): {"type": BLOCK, "device": tree["device"], "mode": "drwx"} for block in config[1]["fs.get_blocks"]["blocks"]}

					tree["children"] = blocks
				elif tree["type"] in (BLOCK, FILE):
					block_id = (int(component) if (tree["type"] == BLOCK) else tree["block_id"])
					path = "".join(map(lambda name: "/" + name, nodes[3:node_index + 1]))

					response = tree["device"].request("ffs.list", {"block_id": block_id, "path": path, "depth": 1})
					if (response[0] == 200): entries = response[1]
					else: return False # return fail("error " + str(response[0]))

					files = {}
					for entry in entries:
						entry = entry.split(" ")
						filename = ("." if (entry[3] == "/") else os.path.basename(entry[3]))
						files[filename] = {"type": FILE, "device": tree["device"], "block_id": block_id, "size": int(entry[1]), "mtime": int(entry[2]), "mode": entry[0]}

					tree["children"] = files

			tree = tree["children"]
			node_index += 1

		return tree
