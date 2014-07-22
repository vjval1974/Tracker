#!/bin/bash
gcc -c -Wall -o Tracker.o $(mysql_config --cflags) Tracker.c $(mysql_config --libs)
gcc  -g -O2  -Wl,-Map=Tracker.map -o Tracker Tracker.o libmote.a $(mysql_config --libs)
