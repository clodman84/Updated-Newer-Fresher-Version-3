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
    def __init__(self, playlist: list):
        # Producer consumer type beat
        # producer: file_reader() via a thread that we make
        # consumer: callback() via an sd.OutputStream
        self.playlist = playlist
        self.audio_buffer = queue.Queue(maxsize=BUFFER_SIZE)
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
            data = self.audio_buffer.get()
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

    def file_reader(self, path):
        # responsible for putting things into the queue
        with sf.SoundFile(path) as file:
            data = file.read(
                BLOCK_SIZE * BUFFER_SIZE
            )  # skipping the blocks that have already been written to the queue in .play()
            while True:
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    return
                self.audio_buffer.put(data)

    def finished_callback(self):
        logger.info("Finished playing audio track")

    def play(self, path):
        if self.stream and self.stream.active:
            return
        logger.info(f"Playing audio from path: {path}")
        with sf.SoundFile(path) as file:
            for _ in range(BUFFER_SIZE):
                # pre-loading the queue with BUFFER_SIZE blocks of audio
                data = file.read(BLOCK_SIZE)
                if not len(data):
                    self.audio_buffer.put(data)

        feeder_thread = threading.Thread(
            target=functools.partial(self.file_reader, path)
        )
        self.stream = sd.OutputStream(
            blocksize=BLOCK_SIZE,
            latency=0.1,
            callback=self.callback,
            finished_callback=self.finished_callback,
            channels=2,
        )
        feeder_thread.start()
        self.stream.start()
        logger.debug(f"{self.stream} started")

    def start(self):
        for track in self.playlist:
            self.play(track)
