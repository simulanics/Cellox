#!/bin/bash
cd ../build/src
echo "Configuring CMake ..."
cmake -B ./build -DCMAKE_BUILD_TYPE=Release
echo "Building Interpreter ..."
cmake --build ./build --config Release --target Cellox
echo "Installing Interpreter ..."
cmake --install . --config Release