#!/bin/bash
# Create the validation venv: the oracle package and its dependencies.
set -e

cd "$(dirname "$0")"

python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -e ".[dev,tracker]"
