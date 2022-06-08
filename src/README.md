MLiveCCPlayer
=========
- Video player based on [ffplay](http://ffmpeg.org)
- iOS: [MediaPlayer.framework-like](ios/IJKMediaPlayer/IJKMediaPlayer/IJKMediaPlayback.h)

### My Build Enviroment
- Common
 - Mac OS X 10.9.5
- iOS
 - Xcode 6.1.0
- [HomeBrew](http://brew.sh)
 - ruby -e "$(curl -fsSL https://raw.github.com/Homebrew/homebrew/go/install)"
 - brew install git

### TODO
- iOS
 - api: AVFoundation-like
 - hw-accelerator: HW decode

### NOT-ON-PLAN
- obsolete platforms (Android: API-8 and below; iOS: below 5.1.1)
- obsolete cpu: ARMv5, ARMv6, MIPS (I don't even have these types of devices…)
- native subtitle render
- cygwin compatibility

### Build iOS
```
git clone https://github.com/bbcallen/ijkplayer.git ijkplayer-ios
cd MLiveCCPlayer
git checkout -B latest n0.2.2
# or for master
# git checkout master

xcode project: src/ios/MLiveCCPlayer.xcodeproj

build:
python src/ios/scripts/build_frameworks.py
```


### Links
- [FFmpeg_b4a](http://www.basic4ppc.com/android/forum/threads/ffmpeg_b4a-a-ffmpeg-library-for-b4a-decoding-streaming.44476/)
- 中文
 - [ijkplayer学习系列之环境搭建 2013-11-23](http://blog.csdn.net/nfer_zhuang/article/details/16905755)
 - [Ubuntu 14.04 下编译 ijkplayer Android 2014-08-01](http://xqq.0ginr.com/ijkplayer-build/#more-134)

### License

```
Copyright (C) 2013-2014 Zhang Rui <bbcallen@gmail.com> 
Licensed under LGPLv2.1 or later
```

ijkplayer is based on or derives from projects below:
- LGPL
  - [FFmpeg](http://git.videolan.org/?p=ffmpeg.git)
  - [libVLC](http://git.videolan.org/?p=vlc.git)
  - [kxmovie](https://github.com/kolyvan/kxmovie)
- zlib license
  - [SDL](http://www.libsdl.org)
- Apache License v2
  - [VitamioBundle](https://github.com/yixia/VitamioBundle)
- BSD-style license
  - [libyuv](https://code.google.com/p/libyuv/)
- ISC license
  - [libyuv/source/x86inc.asm](https://code.google.com/p/libyuv/source/browse/trunk/source/x86inc.asm)

ijkplayer's build scripts are based on or derives from projects below:
- [gas-preprocessor](http://git.libav.org/?p=gas-preprocessor.git)
- [VideoLAN](http://git.videolan.org)
- [yixia/FFmpeg-Android](https://github.com/yixia/FFmpeg-Android)
- [kewlbear/FFmpeg-iOS-build-script](http://github.com/kewlbear/FFmpeg-iOS-build-script) 

### Commercial Use
MLiveCCPlayer is licensed under LGPLv2.1 or later, so itself is free for commercial use under LGPLv2.1 or later

But MLiveCCPlayer is also based on other different projects under various licenses, which I have no idea whether they are compatible to each other or to your product.

[IANAL](http://en.wikipedia.org/wiki/IANAL), you should always ask your lawyer for these stuffs before use it in your product.
