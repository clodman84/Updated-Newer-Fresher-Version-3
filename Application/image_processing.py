from PIL import Image as PImage


def get_histogram(image: PImage.Image):
    return [a.histogram for a in image.split()]
