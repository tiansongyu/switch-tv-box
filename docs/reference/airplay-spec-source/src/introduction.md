# Introduction

AirPlay is a family of protocols implemented by Apple to view various
types of media content on the *Apple TV* from any iOS device or iTunes.
In this documentation, &#8220;*iOS device*&#8221; refers to an iPhone, iPod touch or
iPad. The following scenarios are supported by AirPlay:

- Display photos and slideshows from an iOS device.
- Stream audio from an iOS device or iTunes.
- Display videos from an iOS device or iTunes.
- Show the screen content from an iOS device or OS X Mountain Lion.
  This is called *AirPlay Mirroring*. It requires hardware capable of
  encoding live video without taking too much CPU, so it is only
  available on iPhone 4S, iPad 2, the new iPad, and Macs with Sandy
  Bridge CPUs.

Audio streaming is also supported from an iOS device or iTunes to an
AirPort Express base station or a 3<sup>rd</sup> party AirPlay-enabled audio
device. Initially this was called *AirTunes*, but it was later renamed
to AirPlay when Apple added video support for the Apple TV.

This document describes these protocols, as implemented in Apple TV
software version 5.0, iOS 5.1 and iTunes 10.6. They are based on
well-known standard networking protocols such as *Multicast DNS*,
*HTTP*, *RTSP*, *RTP*, *PTP* or *NTP*, with custom extensions.

All these information have been gathered by using various techniques of
reverse engineering, so they might be somewhat inaccurate and
incomplete.
