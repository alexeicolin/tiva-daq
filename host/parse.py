#!/usr/bin/python

import sys
import time
import argparse

from daq import *

parser = argparse.ArgumentParser(description=\
        'Convert binary trace from ADC to CSV')
parser.add_argument('trace_file', \
        help="binary trace file from ADC ('-' for stdin)")
parser.add_argument('out', \
        help='name prefix for output files')
parser.add_argument('-s', '--start-seq-num', type=int, default=0, \
        help='packet sequence numbers start at this value')
args = parser.parse_args()

# The ADC config must match the config in the target application

# exp buf index (skipping double-buffer) -> seq configuration
SEQS = {
    0: Sequence("load",
        10, # samples per sec
        [
            # Order is counter-clockwise on the breadboard
            SingleChannel("A0"),
            SingleChannel("A1"),
            SingleChannel("A2"),
            SingleChannel("A3"),
        ]),
    1: Sequence("temp",
        1, # samples per sec
        [
            TempChannel("T"),
        ]),
}

if args.trace_file == '-':
    fin = sys.stdin
else:
    fin = open(args.trace_file, 'r')

save_as_csv(fin, args.out, SEQS, start_seq_num=args.start_seq_num)
