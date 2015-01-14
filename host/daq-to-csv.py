#!/usr/bin/python

import sys
import time
import argparse
import signal
import binascii
import numpy as np
import json

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

def bytes_to_int(byte_array):
    """Return integer value from little-endian byte array"""
    r = 0
    p = 0
    for b in byte_array:
        r |= (b << (8*p))
        p += 1
    return r

class Channel:
    VREFP = 3.3
    VREFN = 0

    def __init__(self, name):
        self.name = name

class SingleChannel(Channel):
    ZERO = 0
    MAX_COUNT = 0xFFF + 1 # number of 12-bit values

    def convert(self, sample):
        return self.VREFP / self.MAX_COUNT * float(sample - self.ZERO)

class DiffChannel(Channel):
    ZERO = 0x7FF
    MAX_COUNT = 0xFFF - ZERO # number of 12-bit values

    def convert(self, sample):
        return self.VREFP / self.MAX_COUNT * float(sample - self.ZERO)

class TempChannel(Channel):
    # See TM4C123 datasheet page 810
    def convert(self, sample):
        return 147.5 - ((75.0 * (self.VREFP - self.VREFN) * sample) / 4096)

class Sequence:
    def __init__(self, name, channels):
        self.name = name
        self.channels = channels

    def parse_samples(self, header, sample_data):
        """Parses raw data buffer into a 2D numpy array (count x channels) """
        num_sample_seqs = header.num_samples() / len(self.channels)
        samples = np.zeros([num_sample_seqs, len(self.channels)])
        pos = 0
        for i in range(num_sample_seqs):
            j = 0
            for j in range(len(self.channels)):
                chan = self.channels[j]

                raw_bytes = sample_data[pos : pos + header.SAMPLE_SIZE]
                pos += header.SAMPLE_SIZE

                raw_sample = bytes_to_int(bytearray(raw_bytes))
                sample = chan.convert(raw_sample)
                samples[i, j] = sample
        return samples

class Header:

    # The following buffer settings should match export module (export.c,h)

    MARKER = binascii.unhexlify('f00dcafe')

    MARKER_SIZE = 4
    SIZE_SIZE   = 2
    USER_ID_SIZE = 1
    SEQ_SIZE    = 1

    SIZE = MARKER_SIZE + SIZE_SIZE + USER_ID_SIZE + SEQ_SIZE

    MAX_BUF_SIZE = 1024 # constained by UDMA transfer size
    SAMPLE_SIZE = 2 # bytes

    POSSIBLE_SEQ_NUMS = 2**(SEQ_SIZE * 8)

    def __init__(self, data):
        pos = 0

        marker = data[pos : pos + self.MARKER_SIZE]
        if marker != self.MARKER:
            raise ParseException("Invalid marker: " + \
                str(binascii.hexlify(marker)) + \
                " (expected " + str(binascii.hexlify(self.MARKER)) + ")")
        pos += self.MARKER_SIZE

        buf_size = data[pos : pos + self.SIZE_SIZE]
        buf_size = bytes_to_int(bytearray(buf_size))
        if buf_size >= self.SIZE and buf_size > self.MAX_BUF_SIZE:
            raise ParseException("Invalid buffer size: " + str(buf_size) + \
                    " (max is " + str(self.MAX_BUF_SIZE) + ")")
        self.buf_size = buf_size
        pos += self.SIZE_SIZE

        buf_user_id = data[pos : pos + self.USER_ID_SIZE]
        self.buf_user_id = bytes_to_int(bytearray(buf_user_id))
        pos += self.USER_ID_SIZE

        seq_num = data[pos : pos + self.SEQ_SIZE]
        self.seq_num = bytes_to_int(bytearray(seq_num))

        pos += self.SEQ_SIZE

    def num_samples(self):
        return (self.buf_size - self.SIZE) / self.SAMPLE_SIZE

class ParseException(Exception):
    pass
class ConfigException(Exception):
    pass

# TODO: support channel names
def channel_from_string(s):
    if s[0] == "A":
        return SingleChannel(s)
    elif s == "TS":
        return TempChannel(s)
    else:
        raise ConfigException("Unrecognized channel type: " + s)

def seqs_from_daq_config(path):
    """Build (adc,seq)->Sequence map from a JSON object in the given file"""

    daq_config_obj = json.load(open(path))

    seqs = {}
    for adc_idx in daq_config_obj["adcs"].keys():
        adc_config = daq_config_obj["adcs"][adc_idx]
        seqs[int(adc_idx)] = {}
        for seq_idx in adc_config["seqs"].keys():
            seq_config = adc_config["seqs"][seq_idx]
            name = adc_idx + "-" + seq_idx # TODO: support custom names
            chans = []
            for chan_config in seq_config["samples"]:
                chans.append(channel_from_string(chan_config))
            seqs[int(adc_idx)][int(seq_idx)] = Sequence(name, chans)

    return seqs

# MAIN

if args.trace_file == '-':
    fin = sys.stdin
else:
    fin = open(args.trace_file, 'r')

seqs = seqs_from_daq_config(daq_config_file)

# Open an output file per sequence and write the column header into each
fout = {}
for adc_idx in seqs.keys():
    for seq in seqs[adc_idx].values():
        fout_name = out_name + '.' + seq.name + '.csv'
        fout[seq] = open(fout_name, 'w')

        # Write CSV header for the sequence
        for chan_i in range(len(seq.channels)):
            chan = seq.channels[chan_i]
            fout[seq].write(chan.name)
            if chan_i != len(seq.channels) - 1:
                fout[seq].write(",")
        fout[seq].write("\n")

def close_output_files():
    for adc_idx in seqs.keys():
        for seq in seqs[adc_idx].values():
            fout[seq].close()

# Handle SIGINT to make all parsed content before Ctrl-C end up in the
# file. Respect the caller's handler by fwding to it and restoring it.
prev_sigint_handler = signal.signal(signal.SIGINT, signal.SIG_DFL)
def sigint_handler(sig, frame):
    close_output_files()
    if prev_sigint_handler == signal.SIG_DFL:
        prev_sigint_handler(sig, frame)
    else:
        sys.exit(0)
signal.signal(signal.SIGINT, sigint_handler)

cur_seq_num = start_seq_num
pos = 0

try:

    while True:

        skipped = False
        eof = False
        while True: # keep searching for header in case corruption occurred
            header_data = fin.read(Header.SIZE)
            if header_data is None or len(header_data) == 0: # EOF
                eof = True
                break

            if len(header_data) != Header.SIZE:
                raise ParseException("Failed to read header " + \
                    "(pos " + ("0x%x" % pos) + "):" + str(header_data))

            try:
                header = Header(bytearray(header_data))
                break
            except ParseException as e:
                print e
                skipped = True
                continue
            finally:
                pos += Header.SIZE

        if eof:
            break # done

        adc_idx = header.buf_user_id >> 4
        seq_idx = header.buf_user_id & 0xf

        if adc_idx not in seqs.keys() or \
            seq_idx not in seqs[adc_idx].keys():
            raise ParseException("Buffer ID does not match DAQ config " + \
                "(pos " + ("0x%x" % pos) + "): " + \
                "id " + ("0x%x" % header.buf_user_id) + ": " + \
                "adc " + str(adc_idx) + " seq " + str(seq_idx))

        try:
            if header.seq_num != cur_seq_num:
                raise ParseException("Seq number mismatch " + \
                    "(pos " + ("0x%x" % pos) + "): " + \
                    str(header.seq_num) + \
                    " (expected " + str(cur_seq_num) + ")")
        except ParseException as e:
            print e

            # Part of rudimentary corruption tolerance
            cur_seq_num = header.seq_num

        cur_seq_num = (cur_seq_num + 1) % header.POSSIBLE_SEQ_NUMS

        sample_data = fin.read(header.buf_size - header.SIZE)
        if len(sample_data) != header.buf_size - header.SIZE:
            if len(fin.read(1)) == 0: # hit EOF?
                print "done"
                break # ignore last incomplete packet
            else:
                raise ParseException("Failed to read sample data " + \
                        "(pos " + ("0x%x" % pos) + "): got " + \
                        str(len(sample_data)) + " bytes (requested " + \
                        str(header.buf_size - header.SIZE) + ")")
        pos += header.buf_size - header.SIZE

        seq = seqs[adc_idx][seq_idx]
        samples = seq.parse_samples(header, sample_data)

        for i in range(samples.shape[0]): # sample channel sequence
            for j in range(samples.shape[1]): # channel
                fout[seq].write("%f" % samples[i, j])
                if j != samples.shape[1] - 1:
                    fout[seq].write(',')
            fout[seq].write("\n")
except ParseException:
    print "ParseException at pos ", ("0x%x" % pos)
    raise
finally:
    close_output_files()

    # restore the caller's signal handler
    signal.signal(signal.SIGINT, prev_sigint_handler)
