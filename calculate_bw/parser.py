#!/usr/bin/python3.4

import sys
import os
import errno
import optparse
import re

DEFAULT_OUTPATH = './parser_out'


def parse_log(file):
	with open(file, 'r') as fh:
		log = fh.read()
		#log = re.sub('module dff.*?endmodule', '', log, flags=re.DOTALL)

	outfile = os.path.join(os.path.abspath(opts.outdir), os.path.basename(file))
	with open(outfile, 'w') as out:
		idx = 0
		ts_count = 0
		msg_count = 0
		prev_ts = 0
		curr_ts = 0
		curr_tput = 0
		prev_tput = 0
		check_list = ["Task_cnt" , "program start" , "START" , "tposted"]
		first_ts = 0
		for line in log.splitlines():
			if any(x in line for x in check_list):
				continue
			ts = float(line.split()[1])
			if prev_ts == 0:
				prev_ts = ts
				first_ts = ts
				continue
			ts_count += ts - prev_ts
			prev_ts = ts
			msg_count += 1
			#print("ts_count: " + str(ts_count))
			if ts_count >= 1000 * float(opts.window_size):
				curr_ts = ts
				curr_tput = (opts.msg_size * msg_count / ts_count) / 1024 * 8
				tput = curr_tput * float(opts.lamda) + prev_tput * (1 - float(opts.lamda))
				prev_tput = curr_tput
				#print("msg_count: " + str(msg_count))
				#out.write("%d %.2f %.2f\n" % (idx, (curr_ts - first_ts)/1000000, tput))
				out.write("%d %.2f %.2f\n" % (idx, (curr_ts - first_ts)/1000000 + float(opts.offset), tput))
				ts_count = 0
				msg_count = 0
				idx += 1

			#if "dff" in line:
			#	s = re.split('\s+|,|\(|\)', line.lstrip())
			#	#s = re.search(r"\((.*?)\)", line).group(1).split(',')
			#	line = "  dffacs1 " + s[1] + "(" + s[3] + ",," + s[2] + ",RST," + s[4] + ");"
			#out.write(line + "\n")

	return

def main():
	global opts
	usage = '\n./parse.py [options]'
	parser = optparse.OptionParser(usage = usage)
	parser.add_option('-i', '--input',
					  dest = 'infile',
					  help = 'provide a single file for conversion')
	parser.add_option('-d', '--dir',
					  dest = 'indir',
					  help = 'convert all files in the given directory path')
	parser.add_option('-o', '--out_dir',
					  dest = 'outdir',
					  default = DEFAULT_OUTPATH,
					  help = 'specify an output directory path to store output files. \
					  Default=./parser_out')
	parser.add_option('-s', '--size',
					  dest = 'msg_size',
					  default = 1000000,
					  help = 'provide the message size in the log. Default is 1000000 Bytes')
	parser.add_option('-t', '--offset',
					  dest = 'offset',
					  default = 0,
					  help = 'provide the initial wait time (offset) before elephant flow joins. Default is 0 sec. You should really specify the correct value.')
	parser.add_option('-w', '--window',
					  dest = 'window_size',
					  default = 40,
					  help = 'provide the sliding window size in milliseconds when measuring tput. Default size is 40 ms')
	parser.add_option('-l', '--lamda',
					  dest = 'lamda',
					  default = 0.25,
					  help = 'provide the lamda value in EWMA. Default size is .25')

	(opts, args) = parser.parse_args()

	if args:
		print("Error: invalid input args")
		parser.print_help()
		sys.exit(1)

	if not (opts.infile or opts.indir):
		print("Need to provide a log file of a directory that contain log")
		parser.print_help()
		sys.exit(1)

	if not os.path.exists(opts.outdir):
		try:
			os.makedirs(opts.outdir)
		except OSError as exc:
			if exc.errno != errno.EEXIST:
				raise

	print(">>> Output Directory is at " , os.path.abspath(opts.outdir), "<<<")

	if opts.infile:
		assert os.path.exists(opts.infile), "Couldn't find log file at " \
		+ os.path.abspath(opts.infile)
		parse_log(opts.infile)
	else:
		assert os.path.exists(opts.indir), "Couldn't find log directory at " \
		+ os.path.abspath(opts.indir)
		for file in os.listdir(opts.indir):
			if file.endswith('.v'):
				parse_log(os.path.join(opts.indir, file))


	return

if __name__ == "__main__":
	main()
