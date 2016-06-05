PulseAudio sink for Chromecast
==============================

**Work in progress.** Nothing works right now.

Build
-----

This program requires following libraries to be installed in your
system: Boost Asio, libavahi-client, libpulse, protobuf-lite, spdlog,
OpenSSL.

The build process also requires `protoc` protobuf compiler to be installed.

    $ git clone --recursive https://github.com/p2004a/pulseaudio-chromecast-sink
    # cd pulseaudio-chromecast-sink
    $ mkdir build
    $ cd build
    $ cmake .. -DCMAKE_BUILD_TYPE=Release
    $ make -j

License
-------

For licensing information see file [COPYING.txt](COPYING.txt).
