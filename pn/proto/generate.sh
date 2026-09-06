#!/bin/bash
# Regenerate the wire format from pn.proto: the nanopb C files here and the
# betterproto module of the Python package. Needs the venv from make_venv.sh.
set -e

cd "$(dirname "$0")"

NANOPB=../extern/nanopb
VENV=../venv
PYTHON_OUT=../python/src/pn

source "$VENV/bin/activate"

python "$NANOPB/generator/nanopb_generator.py" \
	--options-file=pn.options \
	--output-dir=. \
	pn.proto

# betterproto writes one module per protobuf package, as <out>/pn/__init__.py
TMP=$(mktemp -d)
python -m grpc_tools.protoc -I . --python_betterproto_out="$TMP" pn.proto
mv "$TMP/pn/__init__.py" "$PYTHON_OUT/_proto.py"
rm -rf "$TMP"

echo "generated: pn.pb.c pn.pb.h $PYTHON_OUT/_proto.py"
