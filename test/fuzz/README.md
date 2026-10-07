# Fuzzing

This directory contains code for fuzz testing libhttpserver with LLVM's [libFuzzer](http://llvm.org/docs/LibFuzzer.html).

## Build the libraries

Build libhttpserver and the dependent libraries with ASAN.
```
export CC=clang
export CXX=clang++
export CFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link"
export CXXFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link"

cd libmicrohttpd-0.9.71/
./configure
make && sudo make install

cd ../libhttpserver
cd build
../configure
make && sudo make install
```

## Build the fuzz target
```
cd libhttpserver/test/fuzz
clang++ $CXXFLAGS basic_fuzzer.cc -o basic_fuzzer -fsanitize=fuzzer,undefined /usr/local/lib/libhttpserver.a /usr/local/lib/libmicrohttpd.a -lgnutls
clang++ $CXXFLAGS ip_representation.cc -o ip_representation -fsanitize=fuzzer,undefined /usr/local/lib/libhttpserver.a /usr/local/lib/libmicrohttpd.a -lgnutls
```

## Run the fuzz target
```
unzip basic_fuzzer_seed_corpus.zip
./basic_fuzzer corpus/

unzip ip_representation_seed_corpus.zip
./ip_representation ip_corpus/
```
