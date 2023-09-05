# mm_mint_mp4

mm_mint_mp4 is an MP4 video and audio player.

## Description

![Product Name Screen Shot][product-screenshot]

<div style="text-align: justify">
mm_mint_mp4 aims to play MP4 h264 video + AAC audio on Freemint in GEM windows.
It supports 16bpp and 32bpp video format.
The player try to synchronize video with sound play depending of FPS.
It resample the original sound frequency to satisfy constraint imposed by Atari hardware.
The control bar is rebuilt in real time upon the video frame respecting the alpha channel of the png icons, so you can change the icons as your needs by just replacing the files under the icon directory.
There's no shared library or module needed to make it works.
</div>

## Getting Started

### Build Dependencies

* GEMLib
* FdLibm
* PTH (version 2.0.7)
* PNGLib
* ZLib
* LibYuv
* MP4v2
* Zita-resampler
* Fdk-aac
* Openh264

You should found them here: https://tho-otto.de/crossmint.php or you can read https://www.atari-forum.com/viewtopic.php?t=42086 if you want to rebuild them.

### Installing

* make commande will produce bin/mm_mint_mp4.prg.
* This repository contains samples icon files but you should replace them with yours (32bpp / 32px).

### Executing program

* Drag&Drop the MP4 file on the binary generated
* Right click in the window will show/hide the control bar who contains classic play/stop/rewind/mute icons...

## Help

* Notice that the ico folder must be present in the bin directory.
* Due to the lack of performance on Atari Hardware the player works as today only on Aranym JIT emulator:
    * For Falcon: I think that 56k DSP integration must be done in fdk-aac and Openh264
    * V4SA: Modifications to support 080 HyperThreading and SIMD instructions set

## Authors

Medour Mehdi
[@M.Medour](www.linkedin.com/in/mehdi-medour-2968b3b2)

## Version History

* 0.1
    * Initial Release

## License

This project is licensed under the GNU GENERAL PUBLIC LICENSE License - see the LICENSE.md file for details

## Acknowledgments

Inspiration, code snippets, etc.
* [AtariForum](https://github.com/matiassingers/awesome-readme)
* [T.Otto](https://tho-otto.de/crossmint.php)
* [Myaes](http://myaes.lutece.net/)
* [FreeMint](https://freemint.github.io/)

[product-screenshot]: screenshot.png
