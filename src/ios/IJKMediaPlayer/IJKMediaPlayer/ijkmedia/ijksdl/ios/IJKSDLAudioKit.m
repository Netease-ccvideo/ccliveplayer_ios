/*
 * IJKSDLAudioKit.m
 *
 * Copyright (c) 2013-2014 Zhang Rui <bbcallen@gmail.com>
 *
 * based on https://github.com/kolyvan/kxmovie
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#import "IJKSDLAudioKit.h"

extern void IJKSDLGetAudioComponentDescriptionFromSpec(const CCSDL_AudioSpec *spec, AudioComponentDescription *desc)
{
    desc->componentType = kAudioUnitType_Output;
    desc->componentSubType = kAudioUnitSubType_RemoteIO;
    desc->componentManufacturer = kAudioUnitManufacturer_Apple;
    desc->componentFlags = 0;
    desc->componentFlagsMask = 0;
}

extern void IJKSDLGetAudioStreamBasicDescriptionFromSpec(const CCSDL_AudioSpec *spec, AudioStreamBasicDescription *desc)
{
    desc->mSampleRate = spec->freq;
    desc->mFormatID = kAudioFormatLinearPCM;
    desc->mFormatFlags = kLinearPCMFormatFlagIsPacked;
    desc->mChannelsPerFrame = spec->channels;
    desc->mFramesPerPacket = 1;

    desc->mBitsPerChannel = CCSDL_AUDIO_BITSIZE(spec->format);
    if (CCSDL_AUDIO_ISBIGENDIAN(spec->format))
        desc->mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
    if (CCSDL_AUDIO_ISFLOAT(spec->format))
        desc->mFormatFlags |= kLinearPCMFormatFlagIsFloat;
    if (CCSDL_AUDIO_ISSIGNED(spec->format))
        desc->mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;

    desc->mBytesPerFrame = desc->mBitsPerChannel * desc->mChannelsPerFrame / 8;
    desc->mBytesPerPacket = desc->mBytesPerFrame * desc->mFramesPerPacket;
}
