/*
 * Copyright (C) 2015-2020 Anton Shekhovtsov
 * Copyright (C) 2023-2024 v0lt
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "InputFile2.h"
#include "AudioSource2.h"
#include <Ks.h>
#include <KsMedia.h>
#include <memory>
#include <functional>

VDFFAudioSource::VDFFAudioSource(const VDXInputDriverContext& context)
	:mContext(context)
{
}

VDFFAudioSource::~VDFFAudioSource()
{
	if (m_pFrame) {
		av_frame_free(&m_pFrame);
	}
	if (m_pCodecCtx) {
		avcodec_free_context(&m_pCodecCtx);
	}
	if (m_pSwrCtx) {
		swr_free(&m_pSwrCtx);
	}
	if (m_pFormatCtx) {
		avformat_close_input(&m_pFormatCtx);
	}
	for (auto& page : buffer) {
		free(page.aud_data);
	}
}

int VDFFAudioSource::AddRef()
{
	return vdxunknown<IVDXStreamSource>::AddRef();
}

int VDFFAudioSource::Release()
{
	return vdxunknown<IVDXStreamSource>::Release();
}

void* VDXAPIENTRY VDFFAudioSource::AsInterface(uint32_t iid)
{
	if (iid == IVDXAudioSource::kIID)
		return static_cast<IVDXAudioSource*>(this);

	return vdxunknown<IVDXStreamSource>::AsInterface(iid);
}

int VDFFAudioSource::initStream(VDFFInputFile* pSource, int streamIndex)
{
	m_pFormatCtx = pSource->open_file(AVMEDIA_TYPE_AUDIO, streamIndex);
	if (!m_pFormatCtx) {
		return -1;
	}

	m_pSource = pSource;
	m_streamIndex = streamIndex;
	m_pStreamCtx = m_pFormatCtx->streams[m_streamIndex];

	const AVCodec* pDecoder = avcodec_find_decoder(m_pStreamCtx->codecpar->codec_id);
	if (!pDecoder) {
		mContext.mpCallbacks->SetError("FFMPEG: Unsupported audio codec (%d)", m_pStreamCtx->codecpar->codec_id);
		return -1;
	}
	m_pCodecCtx = avcodec_alloc_context3(pDecoder);
	if (!m_pCodecCtx) {
		return -1;
	}
	avcodec_parameters_to_context(m_pCodecCtx, m_pStreamCtx->codecpar);
	m_pCodecCtx->thread_count = 1;

	int ret = avcodec_open2(m_pCodecCtx, pDecoder, nullptr);
	if (ret < 0) {
		char errstr[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, errstr, sizeof(errstr));
		mContext.mpCallbacks->SetError("FFMPEG audio decoder error: %s.", errstr);
		return -1;
	}

	const AVRational tb = m_pStreamCtx->time_base;
	// should normally reduce to integer if timebase is derived from sample_rate
	av_reduce(&time_base.num, &time_base.den, int64(m_pCodecCtx->sample_rate) * tb.num, tb.den, INT_MAX);

	if (time_base.den == 1) {
		trust_sample_pos = true; // works for mp4
	} else {
		trust_sample_pos = false;
	}

	if (m_pStreamCtx->duration == AV_NOPTS_VALUE) {
		/*
		const char* class_name = m_pFormatCtx->iformat->priv_class->class_name;
		if(strcmp(class_name,"avi")==0){
			// pcm avi has it here, maybe bug in avidec
			// not using this now as there is no win
			sample_count = m_pStreamCtx->nb_frames;
		} else*/ {
		// this gives inexact value
			if (m_pFormatCtx->duration == AV_NOPTS_VALUE) {
				// fill 10 hours
				sample_count = int64_t(3600 * 10) * m_pCodecCtx->sample_rate;
			} else {
				sample_count = (m_pFormatCtx->duration * m_pCodecCtx->sample_rate + AV_TIME_BASE / 2) / AV_TIME_BASE;
			}
		}
	}
	else {
		sample_count = (m_pStreamCtx->duration * time_base.num + time_base.den / 2) / time_base.den;
	}

	use_keys = false;
	int nb_index_entries = avformat_index_get_entries_count(m_pStreamCtx);
	if (nb_index_entries < 60) {
		// go to the last record of the current index and back. this will fill up the index a bit.
		// here you should not search to the end of the file, this will greatly slow down
		// the opening of a large file with a large number of audio tracks.
		// works for MKV and FLV
		const AVIndexEntry* ie = avformat_index_get_entry(m_pStreamCtx, nb_index_entries - 1);
		if (ie) {
			seek_frame(m_pFormatCtx, m_streamIndex, ie->pos, AVSEEK_FLAG_BACKWARD);
			seek_frame(m_pFormatCtx, m_streamIndex, AV_SEEK_START, AVSEEK_FLAG_BACKWARD);
			// get the number of index entries again
			nb_index_entries = avformat_index_get_entries_count(m_pStreamCtx);
		}
	}

	for (int i = 0; i < nb_index_entries; i++) {
		if (avformat_index_get_entry(m_pStreamCtx, i)->flags & AVINDEX_KEYFRAME) {
			use_keys = true;
			break;
		}
	}

	// lazy initialized by init_start_time
	// requires video to initialize first
	start_time = AV_NOPTS_VALUE;
	time_adjust = 0;

	m_streamInfo.mSampleCount = sample_count;
	m_streamInfo.mSampleRate.mNumerator = m_pCodecCtx->sample_rate;
	m_streamInfo.mSampleRate.mDenominator = 1;
	m_streamInfo.mPixelAspectRatio.mNumerator = 0;
	m_streamInfo.mPixelAspectRatio.mDenominator = 0;

	if (m_pCodecCtx->ch_layout.nb_channels > 32) {
		mContext.mpCallbacks->SetError("FFMPEG: Unsupported number of channels (%d)", m_pCodecCtx->ch_layout.nb_channels);
		return -1;
	}

	if (m_pCodecCtx->ch_layout.order > AV_CHANNEL_ORDER_NATIVE || (m_pCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC && m_pCodecCtx->ch_layout.nb_channels > 2)) {
		mContext.mpCallbacks->SetError("FFMPEG: Unsupported channel layout.");
		return -1;
	}

	m_pFrame = av_frame_alloc();

	first_page     = 0;
	last_page      = 0;
	used_pages     = 0;
	used_pages_max = 1024;
	size_t buffer_size = (int)((sample_count + BufferPage::size - 1) / BufferPage::size);
	buffer.clear();
	buffer.resize(buffer_size);

	next_sample = AV_NOPTS_VALUE;
	SetTargetFormat(0);

	return 0;
}

void VDFFAudioSource::SetTargetFormat(const VDXWAVEFORMATEX* target)
{
	uint64_t in_layout = 0;
	if (m_pCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
		in_layout = m_pCodecCtx->ch_layout.u.mask;
	}
	else {
		// hmmm
		AVChannelLayout ch_layout_def = {};
		av_channel_layout_default(&ch_layout_def, m_pCodecCtx->ch_layout.nb_channels);
		in_layout = ch_layout_def.u.mask;
	}

	uint64 layout = in_layout;
	AVSampleFormat fmt;
	switch (m_pCodecCtx->sample_fmt) {
	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
		fmt = AV_SAMPLE_FMT_U8;
		break;
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
		fmt = AV_SAMPLE_FMT_S16;
		break;
	default:
		fmt = AV_SAMPLE_FMT_FLT;
	}

	if (target) {
		if (target->mChannels == 1) {
			layout = AV_CH_LAYOUT_MONO;
		}
		else if (target->mChannels == 2) {
			switch (layout) {
			case AV_CH_LAYOUT_MONO:
			case AV_CH_LAYOUT_STEREO:
				break;
			default:
				layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
			}
		}

		if (target->mBitsPerSample == 8) {
			fmt = AV_SAMPLE_FMT_U8;
		}
		if (target->mBitsPerSample == 16 && fmt != AV_SAMPLE_FMT_U8) {
			fmt = AV_SAMPLE_FMT_S16;
		}
	}

	if (layout == out_layout && fmt == out_fmt) {
		return;
	}

	out_layout = layout;
	out_fmt = fmt;

	if (m_pSource->head_segment) {
		VDFFAudioSource* a0 = m_pSource->head_segment->audio_source;
		out_layout = a0->out_layout;
		out_fmt = a0->out_fmt;
	}

	av_samples_get_buffer_size(&src_linesize, m_pCodecCtx->ch_layout.nb_channels, 1, m_pCodecCtx->sample_fmt, 1);

	mRawFormat.Format.wFormatTag      = WAVE_FORMAT_PCM;
	mRawFormat.Format.nChannels       = av_popcount64(out_layout);
	mRawFormat.Format.nSamplesPerSec  = m_pCodecCtx->sample_rate;
	mRawFormat.Format.wBitsPerSample  = av_get_bytes_per_sample(out_fmt) * 8;
	mRawFormat.Format.nAvgBytesPerSec = mRawFormat.Format.nSamplesPerSec * mRawFormat.Format.nChannels * mRawFormat.Format.wBitsPerSample / 8;
	mRawFormat.Format.nBlockAlign     = mRawFormat.Format.nChannels * mRawFormat.Format.wBitsPerSample / 8;
	mRawFormat.Format.cbSize          = 0;

	if (mRawFormat.Format.wBitsPerSample > 16 || mRawFormat.Format.nChannels > 2) {
		mRawFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		if (out_fmt == AV_SAMPLE_FMT_FLT) {
			mRawFormat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		} else {
			mRawFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		}
		mRawFormat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		mRawFormat.Samples.wValidBitsPerSample = mRawFormat.Format.wBitsPerSample;
		mRawFormat.dwChannelMask = uint32_t(out_layout) & 0x3ffff;
	}

	swr_layout = 0;
	swr_rate = 0;
	swr_fmt = AV_SAMPLE_FMT_NONE;
	reset_swr();

	reset_cache();
}

void VDFFAudioSource::reset_swr()
{
	uint64_t in_layout = 0;
	if (m_pCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
		in_layout = m_pCodecCtx->ch_layout.u.mask;
	}
	else {
		// hmmm
		AVChannelLayout ch_layout_def = {};
		av_channel_layout_default(&ch_layout_def, m_pCodecCtx->ch_layout.nb_channels);
		in_layout = ch_layout_def.u.mask;
	}

	if (in_layout == swr_layout && m_pCodecCtx->sample_rate == swr_rate && m_pCodecCtx->sample_fmt == swr_fmt) {
		return;
	}

	swr_layout = in_layout;
	swr_rate = m_pCodecCtx->sample_rate;
	swr_fmt = m_pCodecCtx->sample_fmt;

	if (m_pSwrCtx) {
		swr_free(&m_pSwrCtx);
	}
	m_pSwrCtx = swr_alloc();

	const int in_ch = av_popcount64(in_layout);
	const int out_ch = av_popcount64(out_layout);
	const AVChannelLayout in_ch_layout = { AV_CHANNEL_ORDER_NATIVE, in_ch, in_layout };
	const AVChannelLayout out_ch_layout = { AV_CHANNEL_ORDER_NATIVE, out_ch, out_layout };

	int ret = 0;
	ret = av_opt_set_chlayout(m_pSwrCtx, "in_chlayout", &in_ch_layout, 0);
	ret = av_opt_set_int(m_pSwrCtx, "in_sample_rate", m_pCodecCtx->sample_rate, 0);
	ret = av_opt_set_sample_fmt(m_pSwrCtx, "in_sample_fmt", m_pCodecCtx->sample_fmt, 0);

	ret = av_opt_set_chlayout(m_pSwrCtx, "out_chlayout", &out_ch_layout, 0);
	ret = av_opt_set_int(m_pSwrCtx, "out_sample_rate", m_pCodecCtx->sample_rate, 0);
	ret = av_opt_set_sample_fmt(m_pSwrCtx, "out_sample_fmt", out_fmt, 0);

	ret = swr_init(m_pSwrCtx);
	if (ret < 0) {
		mContext.mpCallbacks->SetError("FFMPEG: Audio resampler error.");
	}
}

void VDFFAudioSource::init_start_time()
{
	int64_t first_pts = AV_NOPTS_VALUE;
	while (1) {
		std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> pkt{ av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); } };
		
		int ret = av_read_frame(m_pFormatCtx, pkt.get());
		if (ret < 0) {
			break;
		}
		if (pkt->stream_index == m_streamIndex) {
			first_pts = pkt->pts;
			av_packet_unref(pkt.get());
			break;
		}
		av_packet_unref(pkt.get());
	}

	start_time = m_pStreamCtx->start_time;
	if (start_time == AV_NOPTS_VALUE) {
		start_time = 0;
	}
	// somehow mov aac starts at pts -1024 but start_time is 0
	if (first_pts != AV_NOPTS_VALUE && first_pts < start_time) {
		start_time = first_pts;
	}
	time_adjust = 0;
	// offset start time so that it will match first video frame
	int vs = m_pSource->find_stream(m_pFormatCtx, AVMEDIA_TYPE_VIDEO);
	if (vs != -1) {
		AVStream* video = m_pFormatCtx->streams[vs];
		AVRational at = m_pStreamCtx->time_base;
		AVRational vt = video->time_base;

		// reference formula
		// -video_start_time * (vt.num/vt.den) / (at.num/at.den)

		int64_t d = -(m_pSource->video_start_time * vt.num * at.den) / vt.den / at.num;
		start_time += d;
		time_adjust += d;
		//sample_count += d * time_base.num / time_base.den;
	}
}

int64_t VDFFAudioSource::frame_to_pts(sint64 frame, AVStream* video)
{
	AVRational rate;
	AVRational fr = video->r_frame_rate;
	av_reduce(&rate.num, &rate.den, m_pCodecCtx->sample_rate * fr.den, fr.num, INT_MAX);
	int64 start = frame * rate.num / rate.den;
	int64_t pos = start * time_base.den / time_base.num - time_adjust;
	return pos;
}

bool VDFFAudioSource::Read(int64_t start, uint32_t count, void* lpBuffer, uint32_t cbBuffer, uint32_t* lBytesRead, uint32_t* lSamplesRead)
{
	if (start >= sample_count) {
		if (m_pSource->next_segment) {
			VDFFAudioSource* a1 = m_pSource->next_segment->audio_source;
			if (a1) {
				return a1->Read(start - sample_count, count, lpBuffer, cbBuffer, lBytesRead, lSamplesRead);
			}
		}
	}

	if (!lpBuffer) {
		*lBytesRead = 0;
		*lSamplesRead = 1;
		return false;
	}

	if (start_time == AV_NOPTS_VALUE) {
		init_start_time();
	}

	int px = (int)(start / BufferPage::size);
	int s0 = start % BufferPage::size;

	if (px < 0 || px >= (int)buffer.size()) {
		*lBytesRead = 0;
		*lSamplesRead = 0;
		return false;
	}

	if (count * mRawFormat.Format.nBlockAlign > cbBuffer) count = cbBuffer / mRawFormat.Format.nBlockAlign;

	if (count == 0) {
		*lBytesRead = 0;
		*lSamplesRead = 0;
		return false;
	}

	if (start_time > 0) {
		int64_t real_start = start_time * time_base.num / time_base.den;
		if (start < real_start) {
			// read before actual stream
			int n = start + count < real_start ? count : int(real_start - start);
			insert_silence(start, n);
			next_sample = real_start;
		}
	}

	int n = buffer[px].copy(s0, count, lpBuffer, mRawFormat.Format.nBlockAlign);
	if (n > 0) {
		*lBytesRead = n * mRawFormat.Format.nBlockAlign;
		*lSamplesRead = n;
		return true;
	}

	if (next_sample == AV_NOPTS_VALUE || start > next_sample + m_pCodecCtx->sample_rate || start < next_sample) {
		// required to seek
		discard_samples = int(start >= 4096 ? 4096 : start);
		int64_t pos = (start - discard_samples) * time_base.den / time_base.num - time_adjust;
		if (start == 0) {
			pos = AV_SEEK_START;
			discard_samples = 0;
		}
		avcodec_flush_buffers(m_pCodecCtx);
		int flags = use_keys ? 0 : AVSEEK_FLAG_ANY;
		seek_frame(m_pFormatCtx, m_streamIndex, pos, AVSEEK_FLAG_BACKWARD | flags);
		next_sample = AV_NOPTS_VALUE;
	}

	std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> pkt{ av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); } };

	ReadInfo ri;

	while (1) {
		int ret = av_read_frame(m_pFormatCtx, pkt.get());
		if (ret < 0) {
			// typically end of stream
			// may result from inexact sample_count too
			//insert_silence(start,count);
			ri.last_sample = start;
		}
		else {
			if (pkt->stream_index != m_streamIndex) {
				av_packet_unref(pkt.get());
				continue;
			}

			auto pkt_data_orig = pkt->data;
			auto pkt_size_orig = pkt->size;

			do {
				int s = read_packet(pkt.get(), ri);
				if (s < 0) break;
				pkt->data += s;
				pkt->size -= s;
			} while (pkt->size > 0);

			pkt->data = pkt_data_orig;
			pkt->size = pkt_size_orig;
			av_packet_unref(pkt.get());
		}

		if (ri.last_sample < start) continue;

		if (start == 0 && first_sample > 0) {
			// some crappy padding
			// it seems aac discards first frame and vorbis too
			if (m_pCodecCtx->codec_id != AV_CODEC_ID_OPUS) { // but don't use this for Opus
				insert_silence(start, (int)first_sample);
			}
		}

		n = buffer[px].copy(s0, count, lpBuffer, mRawFormat.Format.nBlockAlign);
		if (n > 0) {
			*lBytesRead = n * mRawFormat.Format.nBlockAlign;
			*lSamplesRead = n;
			return true;
		}
		else {
			// seek/decode missed required sample
			n = buffer[px].empty(s0, count);
			write_silence(lpBuffer, n);
			*lBytesRead = n * mRawFormat.Format.nBlockAlign;
			*lSamplesRead = n;
			return true;
		}
	}

	*lBytesRead = 0;
	*lSamplesRead = 0;
	return false;
}

void VDFFAudioSource::write_silence(void* dst, uint32_t count)
{
	int src = mRawFormat.Format.wBitsPerSample == 8 ? 0x80 : 0;
	memset(dst, src, count * mRawFormat.Format.nBlockAlign);
}

void VDFFAudioSource::insert_silence(int64_t start, uint32_t count)
{
	while (count) {
		int px = (int)(start / BufferPage::size);
		int s0 = start % BufferPage::size;

		if (px >= (int)buffer.size()) {
			break;
		}
		alloc_page(px);
		BufferPage& bp = buffer[px];

		int changed = 0;
		int n = bp.alloc(s0, count, changed);
		if (changed) {
			uint8_t* dst = bp.aud_data + s0 * mRawFormat.Format.nBlockAlign;
			write_silence(dst, n);
		}

		start += n;
		count -= n;
	}
}

void VDFFAudioSource::invalidate(int64_t start, uint32_t count)
{
	while (count) {
		int px = (int)(start / BufferPage::size);
		int s0 = start % BufferPage::size;

		if (px >= (int)buffer.size()) {
			break;
		}
		BufferPage& bp = buffer[px];
		int n = s0 + count < BufferPage::size ? count : BufferPage::size - s0;
		if (bp.a0 && s0 <= bp.a0) {
			bp.a0 = 0;
			bp.a1 = 0;
			bp.b0 = 0;
			bp.b1 = 0;
		}
		else if (s0 < bp.a1) {
			bp.a1 = s0;
			bp.b0 = 0;
			bp.b1 = 0;
		}
		else if (bp.b0 && s0 <= bp.b0) {
			bp.b0 = 0;
			bp.b1 = 0;
		}
		else if (s0 < bp.b1) {
			bp.b1 = s0;
		}

		start += n;
		count -= n;
	}
}

int VDFFAudioSource::read_packet(AVPacket* pkt, ReadInfo& ri)
{
	int ret = avcodec_send_packet(m_pCodecCtx, pkt);
	if (ret != 0) {
		return -1;
	}

	while (1) {
		ret = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
		if (ret != 0) {
			break;
		}

		reset_swr();
		int count = m_pFrame->nb_samples;
		int64_t frame_start = next_sample;
		if (m_pFrame->pts != AV_NOPTS_VALUE) {
			if (m_pFrame->pts == start_time) {
				discard_samples = 0;
			}
			frame_start = (m_pFrame->pts + time_adjust) * time_base.num / time_base.den;
			if (next_sample != AV_NOPTS_VALUE && frame_start != next_sample) {
				trust_sample_pos = false;
				frame_start = next_sample;
			}
		}

		int64_t start = frame_start;
		if (first_sample == AV_NOPTS_VALUE || start < first_sample) {
			first_sample = start;
		}
		int src_pos = 0;

		// ignore samples to discard
		// this is workaround for some defect with AAC decoding (maybe other format too)
		if (discard_samples) {
			if (count > discard_samples) {
				int n = discard_samples;
				discard_samples = 0;
				src_pos = n;
				start += n;
				count -= n;
			}
			else {
				discard_samples -= count;
				start += count;
				count = 0;
			}
		}

		// ignore samples before start
		if (start < 0) {
			int64_t n = -start;
			if (n < count) {
				src_pos = int(n);
				start = 0;
				count -= int(n);

			}
			else {
				start = 0;
				count = 0;
			}
		}

		// ignore samples after end
		if (start + count > sample_count) {
			if (start < sample_count) {
				count = int(sample_count - start);
			}
			else {
				start = sample_count;
				count = 0;
			}
		}

		/*
		if(next_sample!=-1 && start>next_sample){
			// found gap between packets (maybe stream error?)
			insert_silence(next_sample,int(start-next_sample));
		}
		*/

		if (count) {
			if (ri.first_sample == -1) {
				ri.first_sample = start;
			}
			if (ri.last_sample < start + count - 1) {
				ri.last_sample = start + count - 1;
			}
		}

		while (count) {
			int px = (int)(start / BufferPage::size);
			int s0 = start % BufferPage::size;

			alloc_page(px);
			BufferPage& bp = buffer[px];

			int changed = 0;
			int n = bp.alloc(s0, count, changed);
			if (changed) {
				uint8_t* dst = bp.aud_data + s0 * mRawFormat.Format.nBlockAlign;
				const uint8_t* src[32];
				for (int i = 0; i < m_pFrame->ch_layout.nb_channels; i++) {
					src[i] = m_pFrame->extended_data[i] + src_pos * src_linesize;
				}
				swr_convert(m_pSwrCtx, &dst, n, src, n);
			}

			src_pos += n;
			start += n;
			count -= n;
		}

		next_sample = frame_start + m_pFrame->nb_samples;

		// we cannot reliably join cached regions
		// so create gap to force to continue decoding
		if (!trust_sample_pos && next_sample > 0) {
			invalidate(next_sample, 1);
		}
	}

	return pkt->size;
}

void VDFFAudioSource::reset_cache()
{
	for (auto& page : buffer) {
		free(page.aud_data);
		page = {};
	}

	first_page  = 0;
	last_page   = 0;
	used_pages  = 0;
	next_sample = AV_NOPTS_VALUE;
}

void VDFFAudioSource::alloc_page(int i)
{
	BufferPage& bp = buffer[i];
	if (bp.aud_data) {
		return;
	}

	uint8_t* data = nullptr;

	if (used_pages > used_pages_max) {
		while (1) {
			if (last_page > i) {
				if (buffer[last_page].aud_data) {
					if (data) {
						break;
					}
					data = buffer[last_page].aud_data;
					buffer[last_page] = {};
					used_pages--;
				}
				last_page--;
			}
			else if (first_page < i) {
				if (buffer[first_page].aud_data) {
					if (data) {
						break;
					}
					data = buffer[first_page].aud_data;
					buffer[first_page] = {};
					used_pages--;
				}
				first_page++;
			}
		}
	}

	if (data) {
		bp.aud_data = data;
	} else {
		bp.aud_data = (uint8_t*)malloc(BufferPage::size * mRawFormat.Format.nBlockAlign);
	}

	if (i < first_page) {
		first_page = i;
	}
	if (i > last_page) {
		last_page = i;
	}
	used_pages++;
}

int VDFFAudioSource::BufferPage::copy(int s0, uint32_t count, void* dst, int sample_size)
{
	if (a0 <= s0 && a1 > s0) {
		// copy from range a
		int n = s0 + count < a1 ? count : a1 - s0;
		memcpy(dst, aud_data + s0 * sample_size, n * sample_size);
		return n;
	}
	if (b0 <= s0 && b1 > s0) {
		// copy from range b
		int n = s0 + count < b1 ? count : b1 - s0;
		memcpy(dst, aud_data + s0 * sample_size, n * sample_size);
		return n;
	}
	return 0;
}

int VDFFAudioSource::BufferPage::empty(int s0, uint32_t count)
{
	if (a0 <= s0 && a1 > s0) return 0;
	if (b0 <= s0 && b1 > s0) return 0;
	if (a0 > s0) return s0 + count < a0 ? count : a0 - s0;
	if (b0 > s0) return s0 + count < b0 ? count : b0 - s0;
	if (b1 <= s0) return s0 + count < size ? count : size - s0;
	return 0;
}

int VDFFAudioSource::BufferPage::alloc(int s0, uint32_t count, int& changed)
{
	int n = s0 + count < size ? count : size - s0;
	if (a0 <= s0 && a1 >= s0 + n) {
		// already in range a
		changed = 0;
		return n;
	}
	if (b0 <= s0 && b1 >= s0 + n) {
		// already in range b
		changed = 0;
		return n;
	}

	changed = 1;

	if (a1 == 0) {
		// empty page, just initialize
		a0 = s0;
		a1 = s0 + n;
		return n;
	}

	if (a0 <= s0 + n && a1 >= s0 + n) {
		// extend range a down
		a0 = s0;
		return n;
	}

	if (a0 <= s0 && a1 >= s0) {
		// extend range a up
		a1 = s0 + n;
		if (b0 && a1 >= b0) {
			a1 = b1;
			b0 = 0;
			b1 = 0;
		}
		return n;
	}

	if (b0) {
		if (b0 <= s0 + n && b1 >= s0 + n) {
			// extend range b down
			b0 = s0;
			return n;
		}

		if (b0 <= s0 && b1 >= s0) {
			// extend range b up
			b1 = s0 + n;
			return n;
		}

		// going to drop range b, possibly interesting for logging
		changed = 2;
	}

	if (a1 < s0) {
		// insert after range a
		b0 = s0;
		b1 = s0 + n;
	}
	else {
		// insert before range a
		b0 = a0;
		b1 = a1;
		a0 = s0;
		a1 = s0 + n;
	}

	return n;
}
