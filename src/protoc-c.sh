#!/bin/sh

PROTOC_C=${PROTOC_C:-protoc-c}

mkdir -p .protoc-c_build/
cp $1 .protoc-c_build/
cd .protoc-c_build/
eval $PROTOC_C --c_out=. $(basename $1)
cd ..
mv .protoc-c_build/$(basename $2) $2
rm -rf .protoc-c_build/
