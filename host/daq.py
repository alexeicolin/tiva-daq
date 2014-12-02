import signal
import binascii
import numpy as np

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
    def __init__(self, name, samples_per_sec, channels):
        self.name = name
        self.samples_per_sec = samples_per_sec
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
    IDX_SIZE    = 1
    SEQ_SIZE    = 1

    SIZE = MARKER_SIZE + SIZE_SIZE + IDX_SIZE + SEQ_SIZE

    MAX_BUF_SIZE = 1024 # constained by UDMA transfer size
    SAMPLE_SIZE = 2 # bytes

    def __init__(self, data):
        pos = 0

        marker = data[pos : pos + self.MARKER_SIZE]
        if marker != self.MARKER:
            raise Exception("Invalid marker: " + \
                str(binascii.hexlify(marker)) + \
                " (expected " + str(binascii.hexlify(self.MARKER)) + ")")
        pos += self.MARKER_SIZE

        buf_size = data[pos : pos + self.SIZE_SIZE]
        buf_size = bytes_to_int(bytearray(buf_size))
        if buf_size >= self.SIZE and buf_size > self.MAX_BUF_SIZE:
            raise Exception("Invalid buffer size: " + str(buf_size) + \
                    " (max is " + str(self.MAX_BUF_SIZE) + ")")
        self.buf_size = buf_size
        pos += self.SIZE_SIZE

        buf_idx = data[pos : pos + self.IDX_SIZE]
        self.buf_idx = bytes_to_int(bytearray(buf_idx))
        pos += self.IDX_SIZE

        seq_num = data[pos : pos + self.SEQ_SIZE]
        self.seq_num = bytes_to_int(bytearray(seq_num))

        pos += self.SEQ_SIZE

    def num_samples(self):
        return (self.buf_size - self.SIZE) / self.SAMPLE_SIZE

class ParseException(Exception):
    pass

def save_as_csv(fin, out_name, seqs):
    """Read data stream from fin and save it to a set of CSV files"""

    # Open an output file per sequence and write the column header into each
    fout = {}
    for seq in seqs.values():
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
        for seq in seqs.values():
            fout[seq].close()

    # Handle SIGINT to make all parsed content before Ctrl-C end up in the
    # file. Respect the caller's handler by fwding to it and restoring it.
    prev_sigint_handler = signal.signal(signal.SIGINT, signal.SIG_DFL)
    def sigint_handler(signal, frame):
        close_output_files()
        if prev_sigint_handler == signal.SIG_DFL:
            prev_sigint_handler(signal, frame)
        else:
            sys.exit(0)
    signal.signal(signal.SIGINT, sigint_handler)

    cur_seq_num = 0

    try:

        while True:

            header_data = fin.read(Header.SIZE)
            if header_data is None or len(header_data) == 0: # EOF
                break
            
            if len(header_data) != Header.SIZE:
                raise ParseException("Failed to read header:" + \
                    str(header_data))
            header = Header(bytearray(header_data))

            # index in header counts double-buffer, but our seq map does not
            seq_idx = header.buf_idx / 2

            if seq_idx not in seqs.keys():
                raise ParseException("Invalid buffer index: " + \
                    str(header.buf_idx))

            if header.seq_num != cur_seq_num:
                raise ParseException("Seq number mismatch: " + \
                    str(header.seq_num) + \
                    " (expected " + str(cur_seq_num) + ")")

            cur_seq_num += 1

            sample_data = fin.read(header.buf_size - header.SIZE)
            if len(sample_data) != header.buf_size - header.SIZE:
                raise ParseException("Failed to read sample data: got " + \
                        str(len(sample_data)) + " bytes (requested " + \
                        str(header.buf_size - header.SIZE) + ")") 

            seq = seqs[seq_idx]
            samples = seq.parse_samples(header, sample_data)

            for i in range(samples.shape[0]): # sample channel sequence
                for j in range(samples.shape[1]): # channel
                    fout[seq].write("%f" % samples[i, j])
                    if j != samples.shape[1] - 1:
                        fout[seq].write(',')
                fout[seq].write("\n")
    finally:
        close_output_files()

        # restore the caller's signal handler
        signal.signal(signal.SIGINT, prev_sigint_handler)
