"""
Most important part of the dopy app lmao
"""

import functools
import logging
import queue
import threading

import numpy as np
import sounddevice as sd
import soundfile as sf
from cffi.backend_ctypes import CTypesData

from .utils import Singleton

logger = logging.getLogger("Core.Music")

BUFFER_SIZE = 20
BLOCK_SIZE = 2048


class DJ(metaclass=Singleton):
    """
    There can only be one DJ object created throughout the entire program.
    DJ can play local files without blocking the calling thread
    """

    def __init__(self, visualiser=None):
        # Producer consumer type beat
        # producer: file_reader() via a thread that we make
        # consumer: callback() via an sd.OutputStream
        self.audio_buffer = queue.Queue(maxsize=BUFFER_SIZE)
        self.visualiser = visualiser
        self.fft = np.zeros((32,))
        self.stream = sd.OutputStream(
            blocksize=BLOCK_SIZE,
            latency=0.1,
            callback=self.callback,
            finished_callback=self.finished_callback,
            channels=2,
        )

    def callback(
        self,
        outdata: np.ndarray,
        frames: int,
        time: CTypesData,
        status: sd.CallbackFlags,
    ):
        # reads data from the queue and then writes into the outdata ndarray
        try:
            data, fft = self.audio_buffer.get()
        except queue.Empty:
            raise sd.CallbackStop

        if len(data) < len(outdata):
            # your operating system?? (portaudio) demands that the outdata be just 0s if there is nothing to be written
            outdata[: len(data)] = data
            outdata[len(data) :] = 0
            # this can only happen when the file has been fully processed and the length of data will just be the remainder of
            # the length of audio file divided by blocksize
            raise sd.CallbackStop
        else:
            outdata[:] = data

        if self.visualiser:
            self.visualiser(fft)

    def file_reader(self, path):
        # responsible for putting things into the queue
        logger.debug("Feeder Thread Made")
        with sf.SoundFile(path) as file:
            data = file.read(
                BLOCK_SIZE * BUFFER_SIZE
            )  # skipping the blocks that have already been written to the queue in .play()
            while self.stream.active:
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    logger.debug("Feeder thread exited")
                    return
                hamming_window = np.hamming(len(data))
                mono_data = data.mean(1) * hamming_window
                transform = np.abs(np.fft.rfft(mono_data))[: int(len(data) / 2)]
                logarithmically_spaced_averages = []
                previous = 0

                # octave shit
                for i in np.logspace(
                    1, int(np.emath.logn(8, len(data) / 2)), num=32, base=8
                ):
                    logarithmically_spaced_averages.append(
                        np.average(transform[previous : int(i)])
                    )
                    previous = int(i)

                bins = np.array(logarithmically_spaced_averages)
                # gamma correction
                bins = ((bins / bins.max()) ** 1 / 2) * 20
                # scaling this logarithmically
                bins = 10 * np.log10(bins / bins.min())
                s = 0.7
                self.fft = s * self.fft + (1 - s) * bins
                self.audio_buffer.put([data, list(self.fft)])
        logger.debug("Feeder thread exited")

    def audio_buffer_setup_from_file(self, path):
        # pre-loading the queue with BUFFER_SIZE blocks of audio
        with sf.SoundFile(path) as file:
            for _ in range(BUFFER_SIZE):
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    self.audio_buffer.put(data)

    def finished_callback(self):
        logger.info("Finished playing audio track")

    def play(self, path):
        if self.stream.active:
            return
        logger.info(f"Playing audio from path: {path}")
        self.audio_buffer_setup_from_file(path)
        feeder_thread = threading.Thread(
            target=functools.partial(self.file_reader, path)
        )
        self.stream.start()
        feeder_thread.start()
        logger.debug(f"{self.stream} started")

    def stop(self):
        if self.stream.active:
            self.stream.stop()
            while not self.audio_buffer.qsize() == 0:
                try:
                    self.audio_buffer.get_nowait()
                except queue.Empty:
                    continue
                self.audio_buffer.task_done()
            logger.debug("Emptied Audio Buffer!")
