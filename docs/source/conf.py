# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information
import os
import sys

sys.path.insert(0, os.path.abspath("./../.."))
print(sys.path)

project = "UNFv3"
copyright = "2024, The Department of Photography"
author = "The Department of Photography"
release = "0.1.0-alpha"

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "autoapi.extension",
    "sphinx.ext.viewcode",
    "sphinx.ext.todo",
    "pydata_sphinx_theme",
    "sphinxcontrib.video",
]

templates_path = ["_templates"]
exclude_patterns = []
autoapi_dirs = ["../../Application", "../../GUI/"]

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "pydata_sphinx_theme"
html_static_path = ["_static"]
