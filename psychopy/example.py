import hummingbird
from psychopy import visual
import sys

# stimulus parameters
framerate = 1440 # frames per second
dot_density = 0.01 # average number of dots per pixel squared
dot_life = 1 # seconds
speed = 120 # pixels per second
duration = 1 # seconds

# create PsychoPy objects
window = visual.Window(hummingbird.size, units='pix', color=(-1, -1, -1))
stimulus = visual.DotStim(
    window,
    nDots=int(round(dot_density * max(hummingbird.size) ** 2)),
    fieldSize=max(hummingbird.size),
    dotLife=int(round(framerate * dot_life)),
    speed=float(speed) / float(framerate),
    units='pix')

# create the Hummingbird generator
with hummingbird.Generator(filename='example.mp4', framerate=framerate) as generator:

    # animate the stimulus and generate a Humingbird video
    for frame_index in range(0, framerate * duration):
        stimulus.draw()
        window.flip()
        generator.push_frame(window.getMovieFrame()) # push_frame must be called right after window.flip()
        sys.stdout.write('\rframe {} / {}'.format(frame_index + 1, framerate * duration))
        sys.stdout.flush()
    window.close()
    sys.stdout.write('\n')
