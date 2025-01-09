#!/usr/bin/env bash

PYTHON_API_DIR="docs/fake/rollnw"
STUBGEN_OUTPUT_DIR="/tmp/generated_stubs"
PYTHON_STUBS_DIR="rollnw-stubs"

echo "Running stubgen..."
mkdir -p "${STUBGEN_OUTPUT_DIR}"
stubgen "${PYTHON_API_DIR}"/*.py -o "${STUBGEN_OUTPUT_DIR}"
mkdir -p "${PYTHON_STUBS_DIR}"
cp "${STUBGEN_OUTPUT_DIR}/rollnw"/*.pyi "${PYTHON_STUBS_DIR}/"
rm -rf "${STUBGEN_OUTPUT_DIR}"
