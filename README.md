
vakit
=====

A libva based encoder library in the style of avkit developed as part of the cppkit framework.

The 30 second explanation is:

   cppkit - cross platform, C++11 based library ecosystem (foundations, build system and unit test framework).
   avkit - RAII based ffmpeg wrapper library.
   vakit - RAII based libva wrapper library.

vakit's vakit::vah264_encoder class should be fully compatible with avkit::h264_encoder, which means it should
be drop in compatible with avkit's transcoder.
