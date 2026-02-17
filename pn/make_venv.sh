#!/bin/bash
set -e

cd "$(dirname "$0")"

python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install protobuf grpcio-tools "betterproto[compiler]>=2.0.0b7"
pip install -e python/
