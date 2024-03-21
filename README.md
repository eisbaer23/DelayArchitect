# DelayArchitect

A visual, musical editor for delay effects

![screenshot](docs/screen.png)

## Download development builds

soon?

## How to build

Install the prerequisites.

``` sh
sudo apt install build-essential libgl-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libasound2-dev
```

Check out the source code and build.

``` sh
git clone --recursive https://github.com/jpcima/DelayArchitect.git
mkdir DelayArchitect/build
cd DelayArchitect/build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```
