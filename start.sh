#! /bin/bash

# prepare the files 
cd svrdir1
./server svr.cfg 1 & 
cd ..
cd svrdir2
./server svr.cfg 2 &
cd ..
cd svrdir3
./server svr.cfg 3 &
cd ..

cd cordir 
./coordinator svr.cfg & 
cd ..

sleep 5
cd clidir
./client 127.0.0.1 5004 > cli1.txt &
./client 127.0.0.1 5004 > cli2.txt &
./client 127.0.0.1 5004 > cli3.txt &
./client 127.0.0.1 5004 > cli4.txt &
./client 127.0.0.1 5004 > cli5.txt &
./client 127.0.0.1 5004 > cli6.txt &
cd ..

