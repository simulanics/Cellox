#!/bin/bash
# Installs Cellox on your system but does not add it to the path
echo "Configuring CMake ..."
cmake -B ../build -DCMAKE_BUILD_TYPE=Release ..
echo "Building Interpreter ..."
cmake --build ../build --config Release --target Cellox
echo "Installing Interpreter ..."
cmake --install ../build/src --config Release