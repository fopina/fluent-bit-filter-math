#!/bin/sh

set -e

cd $(dirname $0)

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev \
           cmake -DFLB_SOURCE=/usr/src/fluentbit/fluent-bit-1.4.2/ \
                 -DPLUGIN_NAME=filter_math ../

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev \
           make

docker run --rm \
           -v $(pwd)/build:/myplugin fluent/fluent-bit:1.4.2 \
           /fluent-bit/bin/fluent-bit -v \
           -f 1 \
           -e /myplugin/flb-filter_math.so \
           -i mem \
           -F math -p 'Operation=sum' \
                   -p 'Field=Mem.used' \
                   -p 'Field=Mem.total' \
                   -p 'Output_field=wtv' \
                   -m '*' \
           -o stdout
