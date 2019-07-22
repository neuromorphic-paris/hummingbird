import math
import PIL.Image
import numpy
import subprocess

# size contains the display's width and height in pixels.
size = (343, 342) # pixels

# maximum_framerate is the largest number of frames per second the LightCrafter can handle.
maximum_framerate = 1440

class Generator:
    """
    Generator creates a LightCrafter-compatible video from binary frames
    synchronization_pattern must be a list or tuple of bytes
    """
    def __init__(self, filename, synchronization_pattern=(), corner_size=10, framerate=maximum_framerate, ffmpeg='ffmpeg'):
        assert maximum_framerate % framerate == 0, 'the framerate must divide the maximum framerate ({} fps)'.format(maximum_framerate)
        self.replicates = int(maximum_framerate / framerate)
        self.process = subprocess.Popen(
            'ffmpeg -y -i pipe: -c:v libx264 -preset veryslow -pix_fmt yuv420p -crf 0 {}'.format(filename),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=True)
        self.process.stdin.write(b'YUV4MPEG2 W1216 H684 F60:1 Ip C420\n')
        self.synchronization_pattern = synchronization_pattern
        self.corner_size = corner_size
        self.synchronization_pattern_index = 0
        self.index = 0
        self.frames = [
            numpy.empty((size[1], size[0]), dtype=numpy.uint8),
            numpy.empty((size[1], size[0]), dtype=numpy.uint8),
            numpy.empty((size[1], size[0]), dtype=numpy.uint8)]
    def push_frame(self, frame):
        """push_frame adds a binary frame to the output"""
        return_code = self.process.poll()
        if return_code == None:
            if frame.size == (size[0] * 2, size[1] * 2):
                frame = frame.resize(size, resample=PIL.Image.BOX)
            for replicate_index in range(0, self.replicates):
                channel = (int(self.index / 8) + 2) % 3
                mask = (1 << (self.index % 8))
                binary_frame = numpy.where(numpy.asarray(frame.convert(mode='L')) > 127, mask, 0).astype(numpy.uint8)
                self.frames[channel] &= (0b11111111 ^ mask)
                self.frames[channel] = numpy.bitwise_or(self.frames[channel], binary_frame, dtype=numpy.uint8)
                if self.index == 23:
                    lightcrafter_frames = []
                    for local_frame in self.frames:
                        lightcrafter_frame = numpy.zeros((684, 608), dtype=numpy.uint8)
                        for y in range(0, size[1]):
                            for x in range(0, size[0]):
                                lightcrafter_frame[342 - x + y, 133 + int((x + y) / 2)] = local_frame[y, x]
                        if self.synchronization_pattern_index < len(self.synchronization_pattern):
                            for y in range(0, self.corner_size * 2):
                                for x in range(608 - (self.corner_size + 1 - int((y + 1) / 2)), 608):
                                    lightcrafter_frame[y, x] = self.synchronization_pattern[self.synchronization_pattern_index]
                            self.synchronization_pattern_index += 1
                        lightcrafter_frames.append(lightcrafter_frame)
                    self.process.stdin.write(b'FRAME\n')
                    self.process.stdin.write(
                        numpy
                            .vstack((lightcrafter_frames[0].flatten(), lightcrafter_frames[1].flatten()))
                            .reshape((-1, ), order='F')
                            .astype(numpy.uint8)
                            .tobytes())
                    self.process.stdin.write(lightcrafter_frames[2][::2].astype(numpy.uint8).tobytes())
                    self.process.stdin.write(lightcrafter_frames[2][1::2].astype(numpy.uint8).tobytes())
                    self.process.stdin.flush()
                    self.index = 0
                else:
                    self.index += 1
        else:
            raise RuntimeError('ffmpeg returned with the error {}\nstdout: {}\nsterr: {}'.format(
                return_code,
                self.process.stdout.read(),
                self.process.stderr.read()))
    def close(self):
        self.process.stdin.close()
    def __enter__(self):
        return self
    def __exit__(self, type, value, traceback):
        self.close()
