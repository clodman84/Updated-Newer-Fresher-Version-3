import logging
import threading
import time
from queue import Queue

logger = logging.getLogger("Core.Utils")


def natural_time(time_in_seconds: float) -> str:
    """
    Converts a time in seconds to a 6-padded scaled unit
    E.g.:
        1.5000 ->   1.50  s
        0.1000 -> 100.00 ms
        0.0001 -> 100.00 us
    """
    units = (
        ("mi", 60),
        (" s", 1),
        ("ms", 1e-3),
        ("\N{GREEK SMALL LETTER MU}s", 1e-6),
    )

    absolute = abs(time_in_seconds)

    for label, size in units:
        if absolute > size:
            return f"{time_in_seconds / size:.2f} {label}"

    return f"{time_in_seconds / 1e-9:.2f} ns"


class ShittyParallism:
    def __init__(self, work, tasks, num_threads=10) -> None:
        self.work = work
        self.num_threads = num_threads
        self.queue = Queue(maxsize=len(tasks))
        for task in tasks:
            self.queue.put(task)

    def worker(self):
        while not self.queue.empty():
            task = self.queue.get()
            self.work(task)
            self.queue.task_done()

    def start(self):
        threads = []
        for _ in range(self.num_threads):
            threads.append(threading.Thread(target=self.worker))
        for thread in threads:
            thread.start()


class SimpleTimer:
    """
    A timer that also logs what it times.

    Example
    -------
        Timing something and printing the total time as a formatted string: ::

            with SimpleTimer("Test Timer") as timer:
                ...

            print(timer)
    """

    def __init__(self, process_name=None, log=False):
        self.start = 0
        self.time = 0
        self.name = process_name
        self.log = log

    def __enter__(self):
        self.start = time.perf_counter()
        return self

    def __exit__(self, *args):
        self.time = time.perf_counter() - self.start
        if self.log:
            logger.debug(str(self))

    def __str__(self):
        return f"{self.name + ' ' if self.name else ''}completed in {natural_time(self.time)}"
