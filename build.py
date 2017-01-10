#!/usr/bin/env python

import sys
import os
import collections
import re

## check
if (len(sys.argv)<8):
	print "usage: python build.py"
	print "\t<path_to_gen_esp32part.py>"
	print "\t<folder_name> <output_h_name> <output_to_makefile> <output_binary>"
	print "\t<partition_table_filename> <partition_name> <grow_size_boundary>"
	print "\t[<file_name>=[p:<partition_name>,o:<offset>|r:<raw_flash_offset>][[,]g:<growsize>][...]]"
	exit()

sys.path.append(sys.argv[1])
sys.argv[0:] = sys.argv[1:]

from gen_esp32part import PartitionTable, PartitionDefinition

class Option(object):
	partition = ""
	offset = 0
	growsize = -1
	raw_flash_offset = -1

	def __init__(self, input):
		opts = input.split(',');
		for opt in opts:
			if opt[1:2] == ':':
				if opt[0:1] == 'p':
					self.partition = opt[2:]
				if opt[0:1] == 'o':
					self.offset = opt[2:]
				if opt[0:1] == 'g':
					try:
						self.growsize = int(opt[2:],0)
					except:
						if (opt[2:] == "max"):
							self.growsize = -2
				if opt[0:1] == 'r':
					self.raw_flash_offset = int(opt[2:],0)


## init
files = []
partitions = []

## parameters
folder = sys.argv[1]
output_h = sys.argv[2]
output_m = sys.argv[3]
output_b = sys.argv[4]
part_file = sys.argv[5]
partition = sys.argv[6]
increment = int(sys.argv[7], 0)

## load partition table
table = PartitionTable.from_csv(open(part_file,'r').read())

## find referenced main partition
sadr = table[partition].offset
offset = 0
filldata = bytearray(table[partition].size)

for (dirpath, dirnames, filenames) in os.walk(folder):
	files.extend([os.path.normpath(os.path.relpath(os.path.join(dirpath,item),folder)) for item in filenames])

print "FFS files found: %s" % files

outh = open(output_h, 'w')
outm = open(output_m, 'w')
outb = open(output_b, 'wb')

h_enum = "#define FFS_FILE_LIST \\\n"
h_def = "#define FFS_FILE_METADATA \\\n"

for file in files:
	fullname = os.path.join(folder, file)
	fh = open(fullname, 'rb')
	inbytes = bytearray(fh.read())
	flen = len(inbytes)
	fh.close()
	fn = re.sub("[^A-Za-z0-9]", "_", file)
	file = "/" + file.replace(os.pathsep, "/")

	fstart = sadr
	foffset = offset

	blocks = flen // increment
	blocks += 1
	blocks *= increment

	addoffset = 1
	other_partition = 0
	basename = os.path.basename(file)
	check = basename + "="

	for arg in sys.argv:
		wlist = arg.split(' ')
		for w in wlist:
			if w.startswith(check):
				opts = Option(w[len(check):])

				if (opts.growsize >= 0):
					blocks = flen // opts.growsize
					blocks += 1
					blocks *= opts.growsize
	
				if (opts.growsize == -2):
					blocks = flen // table[opts.partition].size
					blocks += 1
					blocks *= table[opts.partition].size
	
				if (opts.partition != ""):
					fstart = table[opts.partition].offset
					foffset = opts.offset
					addoffset = 0
					other_partition = 1
	
				if (opts.raw_flash_offset >= 0):
					fstart = opts.raw_flash_offset
					foffset = 0
					addoffset = 0

	if addoffset:
		offset += blocks

	h_enum += "%s, \\\n" % fn
	h_def += "\t\t{ \"%s\", %s, %d, %d, 0x%x, 0x%x }, \\\n" % (file, fn, flen, blocks, fstart, foffset)
	
	if (flen>0):
		if (other_partition):
			outm.write(" 0x%x %s " % (fstart + foffset, fullname))
		else:
			filldata[foffset:foffset+flen-1] = inbytes[0:flen-1]

h_enum += "\n"
h_def += "\n"

outh.write(h_enum)
outh.write(h_def)

outm.write(" 0x%x %s " % (fstart, output_b))

outb.write(filldata)

outh.close()
outm.close()
outb.close()