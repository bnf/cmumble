#!/bin/sh

PROTOC_C=${PROTOC_C:-protoc-c}

build_dir=".protoc-c_build_$(basename $2)/"

mkdir -p "${build_dir}"
cp $1 "${build_dir}"
cd "${build_dir}"
eval $PROTOC_C --c_out=. $(basename $1)
cd ..
mv "${build_dir}/$(basename $2)" $2
rm -rf "${build_dir}"
