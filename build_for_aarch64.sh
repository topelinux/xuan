#!/bin/bash
BUILD_WITH_MIMALLOC=ON TJS_WAMR_SIMD=OFF ./scripts/build-aarch64.sh

pushd build-aarch64-dyn/dist
tar czf xuan_aarch64.tgz *
popd