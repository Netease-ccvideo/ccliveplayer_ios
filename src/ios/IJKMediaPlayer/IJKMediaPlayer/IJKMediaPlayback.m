/*
 * IJKMediaPlayback.m
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#import "IJKMediaPlayback.h"

NSString *const IJKMediaPlaybackIsPreparedToPlayDidChangeNotification = @"IJKMediaPlaybackIsPreparedToPlayDidChangeNotification";
NSString *const IJKMoviePlayerAudioBeginInterruptionNotification = @"IJKMoviePlayerAudioBeginInterruptionNotification";
NSString *const IJKMoviePlayerLoadStateDidChangeNotification = @"IJKMoviePlayerLoadStateDidChangeNotification";
NSString *const IJKMoviePlayerPlaybackDidFinishNotification = @"IJKMoviePlayerPlaybackDidFinishNotification";
NSString *const IJKMoviePlayerPlaybackStateDidChangeNotification = @"IJKMoviePlayerPlaybackStateDidChangeNotification";
NSString *const IJKMoviePlayerPlaybackRestoreVideoPlay = @"IJKMoviePlayerPlaybackRestoreVideoPlay";
NSString *const IJKMoviePlayerPlaybackVideoViewCreatedNotification = @"IJKMoviePlayerPlaybackVideoViewCreatedNotification";
NSString *const IJKMoviePlayerPlaybackBufferingUpdateNotification = @"IJKMoviePlayerPlaybackBufferingUpdateNotification";
NSString *const IJKMoviePlayerPlaybackVideoSizeChangedNotification = @"IJKMoviePlayerPlaybackVideoSizeChanged";
NSString *const IJKMPMoviePlayerVideoDecoderOpenNotification = @"IJKMPMoviePlayerVideoDecoderOpenNotification";
NSString *const IJKMoviePlayerPlaybackSeekCompletedNotification = @"IJKMoviePlayerPlaybackSeekCompletedNotification";
NSString *const IJKMPMoviePlayerDidSeekCompleteNotification = @"IJKMPMoviePlayerDidSeekCompleteNotification";
NSString *const IJKMPMoviePlayerAccurateSeekCompleteNotification = @"IJKMPMoviePlayerAccurateSeekCompleteNotification";
NSString *const IJKMPMoviePlayerSeekAudioStartNotification = @"IJKMPMoviePlayerSeekAudioStartNotification";
NSString *const IJKMPMoviePlayerSeekVideoStartNotification = @"IJKMPMoviePlayerSeekVideoStartNotification";
NSString *const IJKMoviePlayerRotateChangedNotification = @"IJKMoviePlayerRotateChangedNotification";
NSString *const IJKMoviePlayerSubtitleChangedNotification = @"IJKMoviePlayerSubtitleChangedNotification";
NSString *const IJKMoviePlayerVideoSaveNotification = @"IJKMoviePlayerVideoSaveNotification";
