#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if ! python -m pip --version &> /dev/null; then
  echo "pip is not installed. Attempting to install pip..."
  curl -O https://bootstrap.pypa.io/get-pip.py
  python get-pip.py
  rm get-pip.py
  echo "pip has been installed."
fi

# python_modules.txt contains pairs of module name and installable package names, separated by pipe symbol
cut -d\| -f2 < "${SCRIPT_DIR}/python_modules.txt" | sed '/^#/d' | while read m
do
	if [ -n "$m" ]; then
		echo "pip installing module $m"
		python -m pip install $m
	fi
done
