# Minizip. Portable zip & unzip lib 

Based on the original work of [Gilles Vollant](http://www.winimage.com/zLibDll/minizip.html) and [nmoinvaz](https://github.com/nmoinvaz). I wrap the [minizip](https://github.com/nmoinvaz/minizip) to a shared library and execute files for Android and Unix/Linux

### Benefits

* Very small
* Easy to use
* Cross-platform

### Requirement:

* Zlib. The Zlib library allows to deflate compressed files and to create gzip (.gz) files. Zlib is free software and small.

### Build

Build share library and test exe via CMake
```
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=./install;make;make install
```

Cross compile for Android via Android makefile
```
Include the Android.mk
```

### Usage of share library

```
#include <minizip/miniunz.h>

unzip(src, dst);
```
