#!/bin/bash
set -e

cd "$(dirname "$0")"

python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -e ../pn/python/
pip install -e s9sd
