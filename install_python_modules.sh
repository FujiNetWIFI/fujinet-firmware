#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
# The Python to install into; build_prereqs.py passes the build venv's.
PYTHON=${PYTHON:-python}

if ! "$PYTHON" -m pip --version &> /dev/null; then
  echo "pip is not installed. Attempting to install pip..."
  curl -O https://bootstrap.pypa.io/get-pip.py
  "$PYTHON" get-pip.py
  rm get-pip.py
  echo "pip has been installed."
fi

# python_modules.txt contains pairs of module name and pip requirement, separated by pipe symbol
cut -d\| -f2 < "${SCRIPT_DIR}/python_modules.txt" | sed '/^#/d' | while read m
do
	if [ -n "$m" ]; then
		echo "pip installing module $m"
		"$PYTHON" -m pip install "$m" || exit 1
	fi
done
