#!/bin/sh

set -e

cd $(dirname $0)

docker run --rm \
           -v $(pwd):/myplugin fopina/fluent-bit-plugin-dev \
           sh -c "cmake -DFLB_SOURCE=/usr/src/fluentbit/fluent-bit-1.4.2/ \
                 -DPLUGIN_NAME=filter_math ../ && make"

docker run --rm \
           -v $(pwd)/build:/myplugin fluent/fluent-bit:1.4.2 \
           /fluent-bit/bin/fluent-bit -v \
           -q -f 1 \
           -e /myplugin/flb-filter_math.so \
           -i mem -t sum \
           -F math -p 'Operation=sum' \
                   -p 'Field=Mem.used' \
                   -p 'Field=Mem.total' \
                   -p 'Output_field=wtv' \
                   -m 'sum' \
           -i mem -t sub \
           -F math -p 'Operation=sub' \
                   -p 'Field=Mem.used' \
                   -p 'Field=Mem.total' \
                   -p 'Output_field=wtv' \
                   -m 'sub' \
           -i mem -t mul \
           -F math -p 'Operation=mul' \
                   -p 'Field=Mem.used' \
                   -p 'Field=Mem.total' \
                   -p 'Output_field=wtv' \
                   -m 'mul' \
           -i mem -t div \
           -F math -p 'Operation=div' \
                   -p 'Field=Mem.used' \
                   -p 'Field=Mem.total' \
                   -p 'Output_field=wtv' \
                   -m 'div' \
           -o stdout -m '*' \
           -o exit -m '*'
