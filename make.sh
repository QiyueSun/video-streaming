#!/bin/bash

git pull
cd miProxy
make clean
make
cd ..
sudo python topology.py
