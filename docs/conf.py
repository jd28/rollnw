import os
import subprocess
import sys


def configureDoxyfile(input_dir, output_dir):
    with open('Doxyfile.in', 'r') as file:
        filedata = file.read()

    filedata = filedata.replace('@DOXYGEN_INPUT_DIR@', input_dir)
    filedata = filedata.replace('@DOXYGEN_OUTPUT_DIR@', output_dir)

    with open('Doxyfile', 'w') as file:
        file.write(filedata)


project = "rollNW"
extensions = [
    "breathe",
    'sphinx_tabs.tabs',
    "sphinx.ext.autodoc",
    "sphinx.ext.napoleon",
    "enum_tools.autoenum",
]
breathe_default_project = "rollNW"

# Check if we're running on Read the Docs' servers
read_the_docs_build = os.environ.get('READTHEDOCS', None) == 'True'

if not read_the_docs_build:
    html_theme = "sphinx_rtd_theme"

breathe_projects = {}

if read_the_docs_build:
    input_dir = '../lib/nw ../profiles'
    output_dir = 'build'
    breathe_projects['rollNW'] = 'build/xml/'
    configureDoxyfile(input_dir, output_dir)
    subprocess.call('doxygen', shell=True)
