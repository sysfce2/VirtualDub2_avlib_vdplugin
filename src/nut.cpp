/*
 * Copyright (C) 2015-2020 Anton Shekhovtsov
 * Copyright (C) 2023-2024 v0lt
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

extern "C"
{
#include <libavformat/avformat.h>
}

// copied from libavformat/nut.c because it is not exported

typedef struct AVCodecTag {
	enum AVCodecID id;
	unsigned int tag;
} AVCodecTag;

const AVCodecTag ff_nut_video_tags[] = {
	{ AV_CODEC_ID_GIF,              MKTAG('G', 'I', 'F',  0) },
	{ AV_CODEC_ID_XFACE,            MKTAG('X', 'F', 'A', 'C') },
	{ AV_CODEC_ID_VP9,              MKTAG('V', 'P', '9', '0') },
	{ AV_CODEC_ID_HEVC,             MKTAG('H', 'E', 'V', 'C') },
	{ AV_CODEC_ID_CPIA,             MKTAG('C', 'P', 'i', 'A') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 15) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 15) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(15 , 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(15 , 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 , 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 , 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 , 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 , 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 'A') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  0) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 'A') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  0) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('A', 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(0 , 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('A', 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(0 , 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 24) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 24) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '1', '1', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '2', '2', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '2', '2', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '0', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '0', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '4', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('4', '4', '4', 'P') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '1', 'W', '0') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '0', 'W', '1') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  8) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  8) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R',  4) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B',  4) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', '4', 'B', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', '4', 'B', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'G', 'R', 48) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'G', 'B', 48) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(48 , 'B', 'G', 'R') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(48 , 'R', 'G', 'B') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('R', 'B', 'A', 64) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('B', 'R', 'A', 64) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(64 , 'R', 'B', 'A') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(64 , 'B', 'R', 'A') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 11 ,  9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9 , 11 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 10 ,  9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9 , 10 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  0 ,  9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9 ,  0 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 11 , 10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10 , 11 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 10 , 10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10 , 10 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  0 , 10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10 ,  0 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 11 , 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 , 11 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 10 , 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 , 10 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  0 , 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 ,  0 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 11 , 14) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(14 , 11 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 10 , 14) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(14 , 10 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  0 , 14) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(14 ,  0 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',  0 , 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 ,  0 , '1', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 11 , 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 , 11 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3', 10 , 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 , 10 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '3',  0 , 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 ,  0 , '3', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4', 11 ,  8) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4', 10 ,  8) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  0 ,  8) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '2',  0 ,  8) },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,   9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9,     0, '1', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,   9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9,    11, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,   9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9,    10, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,   9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9,     0, '4', 'Y') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '1', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,  10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   11, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,  10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10,   10, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,  10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '4', 'Y') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,  12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12,    0, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,  12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12,   10, '4', 'Y') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12,    0, '1', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '1', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  11,  16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   11, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',  10,  16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16,   10, '4', 'Y') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '4',   0,  16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '4', 'Y') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('Y', '1',   0,  14) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(14,    0, '1', 'Y') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,   8) },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,   9) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(9,    0, '3', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,  10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10,    0, '3', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,  12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12,    0, '3', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,  14) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(14,    0, '3', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '3',   0,  16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16,    0, '3', 'G') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '4',   0,   8) },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '4', 00 , 10) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(10 , 00 , '4', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '4', 00 , 12) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(12 , 00 , '4', 'G') },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('G', '4', 00 , 16) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(16 , 00 , '4', 'G') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('X', 'Y', 'Z' , 36) },
	{ AV_CODEC_ID_RAWVIDEO,         MKTAG(36 , 'Z' , 'Y', 'X') },

	{ AV_CODEC_ID_RAWVIDEO,         MKTAG('P', 'A', 'L', 8) },

	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'B', 'G', 8) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'B', 'G', 16) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(16  , 'G', 'B', 0xBA) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'R', 'G', 8) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'R', 'G', 16) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(16  , 'G', 'R', 0xBA) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'G', 'B', 8) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'G', 'B', 16) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(16,   'B', 'G', 0xBA) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'G', 'R', 8) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(0xBA, 'G', 'R', 16) },
	{ AV_CODEC_ID_RAWVIDEO, MKTAG(16,   'R', 'G', 0xBA) },

	{ AV_CODEC_ID_NONE,             0 }
};

const AVCodecTag* avformat_get_nut_video_tags() { return ff_nut_video_tags; }
