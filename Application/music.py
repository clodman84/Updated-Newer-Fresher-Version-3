"""
Most important part of the dopy app lmao
"""

import functools
import logging
import queue
import threading
import warnings

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
        self.stream = None

    def callback(
        self,
        outdata: np.ndarray,
        frames: int,
        time: CTypesData,
        status: sd.CallbackFlags,
    ):
        # reads data from the queue and then writes into the outdata ndarray
        try:
            data, fft = self.audio_buffer.get_nowait()
        except queue.Empty:
            logger.debug("In DJ.callback: Audio Queue is empty, raised CallbackStop")
            raise sd.CallbackStop

        if len(data) < len(outdata):
            # your operating system?? (portaudio) demands that the outdata be just 0s if there is nothing to be written
            outdata[: len(data)] = data
            outdata[len(data) :] = 0
            # this can only happen when the file has been fully processed and the length of data will just be the remainder of
            # the length of audio file divided by blocksize
            logger.debug(
                "In DJ.callback: len(data) < len(outdata), raised CallbackStop"
            )
            raise sd.CallbackStop
        else:
            outdata[:] = data

        if self.visualiser:
            self.visualiser(fft)

    def file_reader(self, path):
        # responsible for putting things into the queue
        with sf.SoundFile(path) as file:
            data = file.read(
                BLOCK_SIZE * BUFFER_SIZE
            )  # skipping the blocks that have already been written to the queue in .play()
            while self.stream and self.stream.active:
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    logger.debug("In DJ.file_reader: Feeder thread exited")
                    return
                self.compute_fft(data)
                self.audio_buffer.put([data, list(self.fft)])
        logger.debug(
            "In DJ.file_reader: Feeder thread exited since audiostream is inactive"
        )

    def compute_fft(self, data):
        # revisit this later
        hamming_window = np.hamming(len(data))
        mono_data = data.mean(1) * hamming_window
        transform = np.abs(np.fft.rfft(mono_data))[: int(len(data) / 2)]
        logarithmically_spaced_averages = []

        # octave shit
        previous = 0
        for i in np.logspace(1, int(np.emath.logn(8, len(data) / 2)), num=32, base=8):
            logarithmically_spaced_averages.append(
                np.average(transform[previous : int(i)])
            )
            previous = int(i)

        bins = np.array(logarithmically_spaced_averages)
        with warnings.catch_warnings():
            # this catches a rare case where the audio buffer sent by libsndfile is empty
            warnings.filterwarnings("error")
            try:
                # gamma correction https://dlbeer.co.nz/articles/fftvis.html
                bins = ((bins / bins.max()) ** 1 / 2) * 20
                # scaling this logarithmically
                bins = 10 * np.log10(bins / bins.min())
                # A = np.vstack([range(32), np.ones(32)]).T
                # m, c= np.linalg.lstsq(A, bins, rcond=None)[0]
                # bins = bins - [m * i + c for i in range(32)]
                # logger.debug(bins)

            except RuntimeWarning:
                logger.warning("In DJ.fft: No audio in this frame")
        s = 0.7
        self.fft = s * self.fft + (1 - s) * bins

    def audio_buffer_setup_from_file(self, path):
        # pre-loading the queue with BUFFER_SIZE blocks of audio
        logger.debug("In DJ.audio_buffer_setup_from_file: Filling audio_buffer")
        with sf.SoundFile(path) as file:
            for _ in range(BUFFER_SIZE):
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    return
                self.compute_fft(data)
                self.audio_buffer.put_nowait([data, list(self.fft)])

    def finished_callback(self):
        logger.info("In DJ.finished_callback: Finished playing audio track")

    def play(self, path):
        if self.stream and self.stream.active:
            return
        logger.info(f"In DJ.play: Playing audio from path: {path}")
        self.audio_buffer_setup_from_file(path)
        feeder_thread = threading.Thread(
            target=functools.partial(self.file_reader, path)
        )
        logger.debug("In DJ.play: new feeder_thread made")
        self.stream = sd.OutputStream(
            blocksize=BLOCK_SIZE,
            latency=0.1,
            callback=self.callback,
            finished_callback=self.finished_callback,
            channels=2,
        )
        self.stream.start()
        logger.debug(f"In DJ.play: {self.stream} started")
        feeder_thread.start()
        logger.debug(f"In DJ.play: {feeder_thread} started")

    def stop(self):
        if self.stream and self.stream.active:
            self.stream.stop()
            i = 0
            while BUFFER_SIZE - i >= 0:
                try:
                    self.audio_buffer.get_nowait()
                    i += 1
                except queue.Empty:
                    continue
                self.audio_buffer.task_done()
            logger.debug("In DJ.stop: Emptied Audio Buffer!")
