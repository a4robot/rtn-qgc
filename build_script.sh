export CMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64
cd build
cmake ..
cmake --build . -j$(nproc) > build_output.log 2>&1
