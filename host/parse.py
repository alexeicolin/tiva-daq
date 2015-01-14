#!/usr/bin/python

import sys
import time
import argparse

from daq import *

parser = argparse.ArgumentParser(description=\
        'Convert binary trace from ADC to CSV')
parser.add_argument('daq_config', \
        help="path to daq config in JSON format")
parser.add_argument('trace_file', \
        help="binary trace file from ADC ('-' for stdin)")
parser.add_argument('out', \
        help='name prefix for output files')
parser.add_argument('-s', '--start-seq-num', type=int, default=0, \
        help='packet sequence numbers start at this value')
args = parser.parse_args()

if args.trace_file == '-':
    fin = sys.stdin
else:
    fin = open(args.trace_file, 'r')

save_as_csv(fin, args.out, args.daq_config, start_seq_num=args.start_seq_num)
