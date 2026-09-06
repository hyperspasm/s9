#!/bin/bash
# Create pn's development venv: the Python package with its test extras and the
# code generators that proto/generate.sh runs.
set -e

cd "$(dirname "$0")"

python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -e "python/[dev,codegen]"
