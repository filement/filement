#!/bin/bash

rm -rf bin/* && \
./make device && \
./make device debug && \
cp windows/*.dll bin/ && \
echo 'Success.'
