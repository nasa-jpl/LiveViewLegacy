# FlightView
2014-2022 Jet Propulsion Laboratory, AVIRIS lab

Author/Maintainer: Elliott Liggett

Prior Authors: Jackie Ryan and Noah Levy

This version of LiveView uses NVIDIA's CUDA toolkit for real-time processing, which is covered in the following IEEE publication: 

[LiveView: A new utility for real-time calibration of focal plane arrays using commodity hardware](https://ieeexplore.ieee.org/abstract/document/7500690)

## Contact Us!

Elliott Liggett:
JPL email: [Elliott.Liggett@jpl.nasa.gov](mailto:Elliott.Liggett@jpl.nasa.gov)

## Overview
*LiveView* is graphical program which displays real-time information about image data from imaging spectrometers. Liveview supports high data rate acquisition over CameraLink and via reading files created by other processes. The branch [**FlightView**](https://github.com/nasa-jpl/LiveViewLegacy/tree/flightview) is AVIRIS-III's flight acquisition system, and includes an RGB waterfall. 

Plots are implemented using the [QCustomPlot](http://www.qcustomplot.com) library, which generates live color maps, bar graphs, and line graphs within the Qt C++ environment.

## Installation

Please see the documentation folder for the latest detailed directions.

## System Requirements
### Minimum Requirements:
Quad-core processor (x86 Architecture)
8GB RAM
NVIDIA Graphics Card with 512MB VRAM, i.e, GTX 560 (adjust the size of GPU_BUFFER_SIZE in constants.h of cuda_take and make a clean compile to change this setting)
Linux OS such as Ubuntu or Mint

### Recommended Settings:
32-core CPU array
16GB RAM (depending on the imaging application, memory allocation will increase)
2 SSLI NVIDIA Graphics Card with 1GB VRAM (or one high-end GPU)

## Keyboard Controls
### General
* p - Toggle the Precision Slider
* m - Toggle the Dark Subtraction Mask (if one is present)
* , - Begin recording Dark Frames
* . - Stop recording dark frames

### For frame views (raw image, dark subtraction, standard deviation)
* left click - profile the data at the specified coordinate
* esc - reset the crosshairs
* d - Toggle display of the crosshairs

### For the Histogram Widget
* r - reset the range of the display. Zooming may make it difficult to return to the original scale of the plot.

### For the Playback Widget
* *drag and drop onto the viewing window* - load the selected file. WARNING: Any filetype is accepted. This means if the filetype is not data, garbage will be displayed in the viewing window. This is done by design.
* s - Stop playback and return to the first frame
return - Play/Pause
* f - Fast Forward. Multiple presses increase the fast forward multiplier up to 64x faster.
* r - Rewind. Multiple presses increase the rewind multiplier up to 64x faster.
* a - Move back one frame. Only works when playback is paused.
* d - Move forward one frame. Only works when playback is paused.


