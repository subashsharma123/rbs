#! /bin/bash

mkdir -p svrdir1 
mkdir -p svrdir2
mkdir -p svrdir3 
rm -f svrdir1/* 
rm -f svrdir2/*
rm -f svrdir3/*

mkdir -p cordir
mkdir -p clidir 
rm -f cordir/*
rm -f clidir/*

# prepare the config files 
cp account svrdir1
cp account svrdir2
cp account svrdir3
cp svr.cfg svrdir1
cp svr.cfg svrdir2
cp svr.cfg svrdir3

cp svr.cfg cordir/

# prepare the files 
cd svrdir1
ln -s ../server server 
cd ..
cd svrdir2
ln -s ../server server 
cd ..
cd svrdir3
ln -s ../server server 
cd ..

cd cordir 
ln -s ../coordinator coordinator 
cd ..

cd clidir
ln -s ../client client 
cd ..

