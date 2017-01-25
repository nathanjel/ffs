#!/usr/bin/env python

import sys
import os
import collections
import re
import fnmatch

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
	regex = None
	partition = ""
	offset = 0
	growsize = -1
	raw_flash_offset = -1

	def fits(self, file):
		return self.regex.match(file) is not None

	def __init__(self, input, pattern):
		self.regex = re.compile(fnmatch.translate(pattern))
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

def MainCode():
	## init
	files = []
	partitions = []
	controls = []
	
	## parameters
	folder = sys.argv[1]
	output_h = sys.argv[2]
	output_m = sys.argv[3]
	output_b = sys.argv[4]
	part_file = sys.argv[5]
	partition_name = sys.argv[6]
	increment = int(sys.argv[7], 0)
	
	## load partition table
	table = PartitionTable.from_csv(open(part_file,'r').read())
	
	## find referenced main partition
	main_partition = table[partition_name]
	sadr = main_partition.offset
	offset = 0
	filldata = bytearray(main_partition.size)
	
	for (dirpath, dirnames, filenames) in os.walk(folder):
		files.extend([os.path.normpath(os.path.relpath(os.path.join(dirpath,item),folder)) for item in filenames])
	
	files.sort()
	
	print "FFS files found: %s" % files
	
	f_load = ""
	h_enum = "#define FFS_FILE_LIST \\\n"
	h_def = "#define FFS_FILE_METADATA \\\n"

	optprog = re.compile('(((\S|\\\s)+)=((\\\s|\S)+))')
	args = ' '.join(sys.argv[8:])

	for m in re.finditer(optprog, args):
		controls.append(Option(m.group(4), m.group(2)))
	
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
	
		curr_partition = main_partition
	
		for opts in controls:
			if opts.fits(file):
	
				if (opts.growsize >= 0):
					blocks = flen // opts.growsize
					blocks += 1
					blocks *= opts.growsize
		
				if ((opts.growsize == -2) and (opts.partition != "")):
					blocks = flen // table[opts.partition].size
					blocks += 1
					blocks *= table[opts.partition].size
		
				if (opts.partition != ""):
					fstart = table[opts.partition].offset
					foffset = opts.offset
					curr_partition = table[opts.partition]
		
				if (opts.raw_flash_offset >= 0):
					fstart = 0
					foffset = opts.raw_flash_offset
					curr_partition = None

				break
	
		h_enum += "%s, \\\n" % fn
	
		if (curr_partition is None):
			h_def += "\t\t{ \"%s\", %d, %d, %s, %d, %d, 0x%x }, \\\n" % (file, flen, blocks, "NULL", 0, 0, foffset)
		else:
			h_def += "\t\t{ \"%s\", %d, %d, %s, %d, %d, 0x%x }, \\\n" % (file, flen, blocks, "\"" + curr_partition.name + "\"", curr_partition.type, curr_partition.subtype, foffset)
			if (foffset + blocks) > curr_partition.size:
				raise Exception("File %s with grow is %d bytes, too large to fit in partition %s" % (file, blocks, curr_partition.name))
	
		if (flen>0):
			if (curr_partition is main_partition):
				filldata[foffset:foffset+flen-1] = inbytes[0:flen-1]
			else:
				f_load += " 0x%x %s " % (fstart + foffset, fullname)
	
		if (curr_partition is main_partition):
			offset += blocks
	
		if offset > main_partition.size:
			raise Exception("Filesystem data with grow space too large to fit in main partition %s" % main_partition.name)
	
	h_enum += "\n"
	h_def += "\n"
	
	outh = open(output_h, 'w')
	outm = open(output_m, 'w')
	outb = open(output_b, 'wb')
	
	outh.write(h_enum)
	outh.write(h_def)
	
	outm.write(f_load)
	outm.write(" 0x%x %s " % (main_partition.offset, output_b))
	
	outb.write(filldata)
	
	outh.close()
	outm.close()
	outb.close()

try:
	MainCode()
except Exception as ex:
	print "Failed: %s\n" % ex