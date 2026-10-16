#!/bin/bash
# Create the validation venv: the oracle package and its dependencies.
set -e

cd "$(dirname "$0")"

python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
# torch from PyTorch's CPU index first: the default wheel drags in gigabytes of CUDA libraries that no Pi can use
pip install --index-url https://download.pytorch.org/whl/cpu torch torchaudio
pip install -e ".[dev,tracker]"
