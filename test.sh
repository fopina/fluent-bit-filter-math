#!/bin/sh

set -e

cd $(dirname $0)

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev \
           cmake -DFLB_SOURCE=/usr/src/fluentbit/fluent-bit-1.4.2/ \
                 -DPLUGIN_NAME=out_stdout2 ../

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev \
           make

docker run --rm \
           -v $(pwd)/build:/myplugin fluent/fluent-bit:1.4.2 \
           /fluent-bit/bin/fluent-bit -e /myplugin/flb-out_stdout2.so -i cpu -o stdout2
