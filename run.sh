#!/bin/bash

set -e

cmake -B build || { echo "cmake config failed"; exit 1; }

make -C build || { echo "make build failed"; exit 1; }

echo "------------ Result ------------"

./build/main || { echo "return $?"; }