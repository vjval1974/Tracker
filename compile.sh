#!/bin/bash
gcc -c -Wall -o sflisten.o $(mysql_config --cflags) sflisten.c $(mysql_config --libs)
gcc  -g -O2  -Wl,-Map=sflisten.map -o sflisten sflisten.o libmote.a $(mysql_config --libs)
