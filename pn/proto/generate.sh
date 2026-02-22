#!/bin/bash
set -e

cd "$(dirname "$0")"

NANOPB=../extern/nanopb
VENV=../venv

source "$VENV/bin/activate"

python "$NANOPB/generator/nanopb_generator.py" \
	--options-file=pn.options \
	--output-dir=. \
	pn.proto

echo "generated: pn.pb.c pn.pb.h"
