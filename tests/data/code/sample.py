# A small Python file for the test of larry code.
import os
from json import loads

def read_file(path):
    with open(path) as f:
        return loads(f.read())

class Reader:
    def read(self, path):
        return read_file(path)

    def count(self):
        return len(os.listdir("."))
