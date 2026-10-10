#!/bin/bash
TJS_LINK_MODE=dynamic TJS_USE_DEPS_LIBUV_SHARED=ON ./scripts/build-mips-static.sh

pushd build-mipsel-dyn/dist
tar czf xuan_mipsel.tgz *
popd