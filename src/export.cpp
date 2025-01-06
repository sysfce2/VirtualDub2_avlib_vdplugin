/*
 * Copyright (C) 2015-2020 Anton Shekhovtsov
 * Copyright (C) 2023-2025 v0lt
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "InputFile2.h"
#include "FileInfo2.h"
#include "VideoSource2.h"
#include "AudioSource2.h"
#include "export.h"
#include "a_compress.h"
#include <algorithm>
#include <string>
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include "resource.h"
#include <vfw.h>
#include "Helper.h"
#include "Utils/StringUtil.h"

extern HINSTANCE hInstance;

void adjust_codec_tag2(const char* src_format, const AVOutputFormat* format, AVStream* st)
{
	if (src_format && strcmp(src_format, format->name) == 0) {
		return;
	}
	AVCodecID codec_id = st->codecpar->codec_id;
	unsigned int tag = st->codecpar->codec_tag;
	st->codecpar->codec_tag = 0;
	AVCodecID codec_id1 = av_codec_get_id(format->codec_tag, tag);
	unsigned int codec_tag2;
	int have_codec_tag2 = av_codec_get_tag2(format->codec_tag, codec_id, &codec_tag2);
	if (!format->codec_tag || codec_id1 == codec_id || !have_codec_tag2) {
		st->codecpar->codec_tag = tag;
	}
}

void adjust_codec_tag(const char* src_format, const AVOutputFormat* format, AVStream* st)
{
	if (src_format && strcmp(src_format, format->name) == 0) {
		return;
	}
	AVCodecID codec_id = st->codecpar->codec_id;
	unsigned int tag = st->codecpar->codec_tag;
	st->codecpar->codec_tag = 0;
	AVCodecID codec_id1 = av_codec_get_id(format->codec_tag, tag);
	unsigned int codec_tag2;
	int have_codec_tag2 = av_codec_get_tag2(format->codec_tag, codec_id, &codec_tag2);
	if (!format->codec_tag || codec_id1 == codec_id || !have_codec_tag2) {
		st->codecpar->codec_tag = tag;
	}
}

uint32 export_avi_fcc(AVStream* src)
{
	AVFormatContext* ctx = avformat_alloc_context();
	AVStream* st = avformat_new_stream(ctx, nullptr);
	avcodec_parameters_copy(st->codecpar, src->codecpar);
	const AVOutputFormat* format = av_guess_format("avi", nullptr, nullptr);
	adjust_codec_tag(0, format, st);
	uint32 r = st->codecpar->codec_tag;
	// missing tag in type1 avi
	if (!r) {
		r = av_codec_get_tag(format->codec_tag, src->codecpar->codec_id);
	}
	avformat_free_context(ctx);
	return r;
}

bool VDXAPIENTRY VDFFInputFile::GetExportMenuInfo(int id, char* name, int name_size, bool* enabled)
{
	if (id == 0) {
		strncpy_s(name, 256, "Stream copy...", name_size);
		*enabled = !is_image && !is_image_list;
		return true;
	}

	return false;
}

bool VDXAPIENTRY VDFFInputFile::GetExportCommandName(int id, char* name, int name_size)
{
	if (id == 0) {
		strncpy_s(name, 128, "Export.StreamCopy", name_size);
		return true;
	}

	return false;
}

bool exportSaveFile(HWND hwnd, wchar_t* path, int max_path) {
	OPENFILENAMEW ofn = { 0 };
	wchar_t szFile[MAX_PATH];

	if (path) {
		wcscpy_s(szFile, path);
	} else {
		szFile[0] = 0;
	}

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrTitle = L"Export Stream Copy";

	std::wstring ext;
	const wchar_t* p = GetFileExt(path);
	if (p) {
		ext = p;
	} else {
		ext = L".";
	}

	std::wstring filter = std::format(L"Same as source (*{0})\a*{0}\a", ext.c_str());
	filter.append(L"All files (*.*)\a*.*\a");
	for (auto it = filter.begin(); it != filter.end(); ++it) {
		if (*it == '\a') {
			*it = '\0';
		}
	}

	ofn.lpstrFilter = filter.c_str();
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof szFile;
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT;

	if (GetSaveFileNameW(&ofn)) {
		wcscpy_s(path, max_path, szFile);
		const wchar_t* p1 = GetFileExt(szFile);
		if (!p1) {
			wcscat_s(path, max_path, ext.c_str());
		}
		return true;
	}

	return false;
}

struct ProgressDialog : public VDXVideoFilterDialog {
public:
	bool abort            = false;
	DWORD dwLastTime      = 0;
	int mSparseCount      = 1;
	int mSparseInterval   = 1;
	int64_t current_bytes = 0;
	double current_pos    = 0.0;
	bool changed          = false;
	HWND parent           = nullptr;

	~ProgressDialog() { Close(); }
	void Show(HWND parent);
	void Close();
	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void init_bytes(int64_t bytes);
	void init_pos(double p);
	void sync_state();
	HWND getHwnd() { return mhdlg; }
	void check();
};

void ProgressDialog::Show(HWND parent)
{
	this->parent = parent;
	VDXVideoFilterDialog::ShowModeless(hInstance, MAKEINTRESOURCEW(IDD_EXPORT_PROGRESS), parent);
}

void ProgressDialog::Close()
{
	EnableWindow(parent, true);
	DestroyWindow(mhdlg);
}

INT_PTR ProgressDialog::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
	{
		EnableWindow(parent, false);
		dwLastTime = GetTickCount();
		init_bytes(0);
		SendMessageW(GetDlgItem(mhdlg, IDC_EXPORT_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 16384));
		SetTimer(mhdlg, 1, 500, NULL);
		return TRUE;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCANCEL:
			abort = true;
			return TRUE;
		}
		return TRUE;
	case WM_TIMER:
		sync_state();
		return TRUE;
	}

	return FALSE;
}

void ProgressDialog::sync_state()
{
	if (changed) {
		init_bytes(current_bytes);
		init_pos(current_pos);
		changed = false;
	}
}

void ProgressDialog::init_bytes(int64_t bytes)
{
	double n = double(bytes);
	wchar_t x = L'K';
	n = n / 1024;
	if (n / 1024 > 8) {
		n = n / 1024;
		x = L'M';
	}
	if (n / 1024 > 8) {
		n = n / 1024;
		x = L'G';
	}
	auto str = std::format(L"{:.2f}{} bytes copied", n, x);
	SetDlgItemTextW(mhdlg, IDC_EXPORT_STATE, str.c_str());
}

void ProgressDialog::init_pos(double p)
{
	p = std::clamp(p, 0.0, 1.0);
	int v = int(p * 16384);
	SendMessageW(GetDlgItem(mhdlg, IDC_EXPORT_PROGRESS), PBM_SETPOS, v, 0);
}

void ProgressDialog::check()
{
	MSG msg;

	if (--mSparseCount) {
		return;
	}

	DWORD dwTime = GetTickCount();

	mSparseCount = mSparseInterval;

	if (dwTime < dwLastTime + 50) {
		++mSparseInterval;
	}
	else if (dwTime > dwLastTime + 150) {
		if (mSparseInterval > 1) {
			--mSparseInterval;
		}
	}

	dwLastTime = dwTime;

	while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
		if (!IsDialogMessageW(mhdlg, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}

bool VDXAPIENTRY VDFFInputFile::ExecuteExport(int id, VDXHWND parent, IProjectState* state)
{
	if (id == 0) {
		sint64 start;
		sint64 end;
		if (!state->GetSelection(start, end)) {
			start = 0;
			end = video_source->sample_count;
		}

		const wchar_t* ext0 = GetFileExt(m_path.c_str());
		wchar_t path2[MAX_PATH];
		if (ext0) {
			wcsncpy_s(path2, m_path.c_str(), ext0 - m_path.c_str());
			path2[ext0 - m_path.c_str()] = 0;
		}
		else {
			wcscpy_s(path2, m_path.c_str());
		}
		wcscat_s(path2, L"-01");
		if (ext0) {
			wcscat_s(path2, ext0);
		}
		if (!exportSaveFile((HWND)parent, path2, MAX_PATH)) {
			return false;
		}

		const wchar_t* ext1 = GetFileExt(path2);
		bool same_format = wcscmp(ext0, ext1) == 0;

		std::string ff_path = ConvertWideToUtf8(m_path);
		std::string out_ff_path = ConvertWideToUtf8(path2);

		const AVOutputFormat* oformat = av_guess_format(nullptr, out_ff_path.c_str(), nullptr);
		if (!oformat) {
			MessageBoxW((HWND)parent, L"Unable to find a suitable output format", L"Stream copy", MB_ICONSTOP | MB_OK);
			return false;
		}

		ProgressDialog progress;
		progress.Show((HWND)parent);

		AVFormatContext* fmt = nullptr;
		AVFormatContext* ofmt = nullptr;
		bool v_end;
		bool a_end;
		int64_t vt_end = -1;
		int64_t at_end = -1;
		int64_t pos0, pos1;
		int64_t a_bias = 0;
		int video = -1;
		int audio = -1;
		AVStream* out_video = nullptr;
		AVStream* out_audio = nullptr;

		int err = 0;
		err = avformat_open_input(&fmt, ff_path.c_str(), nullptr, nullptr);
		if (err < 0) {
			goto end;
		}
		err = avformat_find_stream_info(fmt, nullptr);
		if (err < 0) {
			goto end;
		}

		err = avformat_alloc_output_context2(&ofmt, nullptr, nullptr, out_ff_path.c_str());
		if (err < 0) {
			goto end;
		}

		video = video_source->m_streamIndex;
		if (audio_source) {
			audio = audio_source->m_streamIndex;
		}

		for (int i = 0; i < (int)fmt->nb_streams; i++) {
			if (i != video && i != audio) {
				continue;
			}

			AVStream* in_stream = fmt->streams[i];
			AVStream* out_stream = avformat_new_stream(ofmt, nullptr);
			if (!out_stream) {
				err = AVERROR_UNKNOWN;
				goto end;
			}

			err = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
			if (err < 0) {
				goto end;
			}

			adjust_codec_tag2(m_pFormatCtx->iformat->name, ofmt->oformat, out_stream);

			out_stream->sample_aspect_ratio = in_stream->sample_aspect_ratio;
			out_stream->avg_frame_rate = in_stream->avg_frame_rate;
			out_stream->time_base = in_stream->time_base;
			err = avformat_transfer_internal_stream_timing_info(ofmt->oformat, out_stream, in_stream, AVFMT_TBCF_AUTO);
			if (err < 0) {
				goto end;
			}

			out_stream->r_frame_rate = in_stream->r_frame_rate;
			out_stream->avg_frame_rate = in_stream->avg_frame_rate;

			if (i == video) {
				out_video = out_stream;
			}
			else if (i == audio) {
				out_audio = out_stream;
			}
		}

		if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
			err = avio_open(&ofmt->pb, out_ff_path.c_str(), AVIO_FLAG_WRITE);
			if (err < 0) {
				goto end;
			}
		}

		err = avformat_write_header(ofmt, nullptr);
		if (err < 0) {
			goto end;
		}

		/*
		int64_t vt_end = video_source->frame_to_pts_next(end);
		int64_t at_end = -1;
		if(out_audio) at_end = audio_source->frame_to_pts(end,video_source->m_pStream);
		*/

		pos1 = end * video_source->frame_ts.num / video_source->frame_ts.den + video_source->start_time;
		av_seek_frame(fmt, video, pos1, 0);

		while (1) {
			std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> pkt{ av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); } };

			err = av_read_frame(fmt, pkt.get());
			if (err < 0) {
				break;
			}

			AVStream* s = fmt->streams[pkt->stream_index];
			int64_t t = pkt->pts;
			if (t == AV_NOPTS_VALUE) {
				t = pkt->dts;
			}
			if (pkt->stream_index == video) {
				if (vt_end == -1) {
					vt_end = t;
				}
			}
			if (pkt->stream_index == audio) {
				if (at_end == -1) {
					at_end = t;
				}
			}
			av_packet_unref(pkt.get());

			if (vt_end != -1 && (at_end != -1 || audio == -1)) {
				break;
			}
		}

		pos0 = start * video_source->frame_ts.num / video_source->frame_ts.den + video_source->start_time;
		av_seek_frame(fmt, video, pos0, AVSEEK_FLAG_BACKWARD);

		v_end = out_video == 0;
		a_end = out_audio == 0;
		if (out_audio) {
			a_bias = av_rescale_q(pos0, fmt->streams[video]->time_base, fmt->streams[audio]->time_base);
		}

		while (1) {
			if (v_end && a_end) {
				break;
			}
			progress.check();
			if (progress.abort) {
				break;
			}

			std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> pkt{ av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); } };

			err = av_read_frame(fmt, pkt.get());
			if (err < 0) {
				err = 0;
				break;
			}

			AVStream* in_stream = fmt->streams[pkt->stream_index];
			int64_t t = pkt->pts;
			if (t == AV_NOPTS_VALUE) {
				t = pkt->dts;
			}

			AVStream* out_stream = nullptr;
			if (pkt->stream_index == video) {
				if (vt_end != -1 && t >= vt_end) {
					v_end = true;
				} else {
					out_stream = out_video;
				}
				if (pkt->pts != AV_NOPTS_VALUE) {
					pkt->pts -= pos0;
				}
				if (pkt->dts != AV_NOPTS_VALUE) {
					pkt->dts -= pos0;
				}
				progress.current_pos = (double(t) - pos0) / (pos1 - pos0);
			}
			if (pkt->stream_index == audio) {
				if (at_end != -1 && t >= at_end) {
					a_end = true;
				} else {
					out_stream = out_audio;
				}
				out_stream = out_audio;
				if (pkt->pts != AV_NOPTS_VALUE) {
					pkt->pts -= a_bias;
				}
				if (pkt->dts != AV_NOPTS_VALUE) {
					pkt->dts -= a_bias;
				}
			}

			if (out_stream) {
				int64_t size = pkt->size;
				av_packet_rescale_ts(pkt.get(), in_stream->time_base, out_stream->time_base);
				pkt->pos = -1;
				pkt->stream_index = out_stream->index;
				err = av_interleaved_write_frame(ofmt, pkt.get());
				if (err < 0) {
					av_packet_unref(pkt.get());
					break;
				}
				progress.current_bytes += size;
				progress.changed = true;
			}

			av_packet_unref(pkt.get());
		}

		av_write_trailer(ofmt);

	end:
		avformat_close_input(&fmt);
		if (ofmt && !(ofmt->oformat->flags & AVFMT_NOFILE)) {
			avio_closep(&ofmt->pb);
		}
		avformat_free_context(ofmt);

		progress.current_pos = 1;
		progress.sync_state();

		if (err < 0) {
			char buf[1024];
			char errstr[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(err, errstr, sizeof(errstr));
			strcpy_s(buf, "Operation failed.\nInternal error (FFMPEG): ");
			strcat_s(buf, errstr);
			MessageBoxA(progress.getHwnd(), buf, "Stream copy", MB_ICONSTOP | MB_OK);
			return false;
		}
		else if (!progress.abort) {
			MessageBoxW(progress.getHwnd(), L"Operation completed successfully.", L"Stream copy", MB_OK);
			return true;
		}
	}

	return false;
}

FFOutputFile::FFOutputFile(const VDXInputDriverContext& pContext)
	:mContext(pContext)
{
}

FFOutputFile::~FFOutputFile()
{
	Finalize();
	free(a_buf);
}

void FFOutputFile::av_error(int err)
{
	char buf[1024];
	char errstr[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err, errstr, sizeof(errstr));
	strcpy_s(buf, "Internal error (FFMPEG): ");
	strcat_s(buf, errstr);
	mContext.mpCallbacks->SetError(buf);
}

void init_av();

bool VDXAPIENTRY VDFFOutputFileDriver::GetStreamControl(const wchar_t* path, const char* format, VDXStreamControl& sc)
{
	init_av();

	std::string out_ff_path = ConvertWideToUtf8(path);

	int err = 0;
	const AVOutputFormat* oformat = nullptr;
	if (format && format[0]) {
		oformat = av_guess_format(format, nullptr, nullptr);
	}
	if (strcmp(format, "mov+faststart") == 0) {
		oformat = av_guess_format("mov", nullptr, nullptr);
	}
	if (strcmp(format, "mp4+faststart") == 0) {
		oformat = av_guess_format("mp4", nullptr, nullptr);
	}
	if (!oformat) {
		oformat = av_guess_format(nullptr, out_ff_path.c_str(), nullptr);
	}
	if (!oformat) {
		return false;
	}

	if (sc.version < 2) {
		return true;
	}

	if (oformat->flags & AVFMT_GLOBALHEADER) {
		sc.global_header = true;
	}
	if (oformat == av_guess_format("matroska", nullptr, nullptr)) {
		sc.use_offsets = true;
		sc.timebase_num = 1000;
		sc.timebase_den = AV_TIME_BASE;
	}
	if (oformat == av_guess_format("webm", nullptr, nullptr)) {
		sc.use_offsets = true;
		sc.timebase_num = 1000;
		sc.timebase_den = AV_TIME_BASE;
	}
	if (oformat == av_guess_format("mov", nullptr, nullptr)) {
		sc.use_offsets = true;
	}
	if (oformat == av_guess_format("mp4", nullptr, nullptr)) {
		sc.use_offsets = true;
	}
	if (oformat == av_guess_format("ipod", nullptr, nullptr)) {
		sc.use_offsets = true;
	}
	if (oformat == av_guess_format("nut", nullptr, nullptr)) {
		sc.use_offsets = true;
	}

	return true;
}

void FFOutputFile::Init(const wchar_t* path, const char* format)
{
	init_av();

	std::string out_ff_path = ConvertWideToUtf8(path);
	this->m_out_ff_path = out_ff_path;

	int err = 0;
	const AVOutputFormat* oformat = nullptr;
	if (format && format[0]) {
		oformat = av_guess_format(format, nullptr, nullptr);
	}
	if (strcmp(format, "mov+faststart") == 0) {
		oformat = av_guess_format("mov", nullptr, nullptr);
		mp4_faststart = true;
	}
	if (strcmp(format, "mp4+faststart") == 0) {
		oformat = av_guess_format("mp4", nullptr, nullptr);
		mp4_faststart = true;
	}
	if (!oformat) {
		oformat = av_guess_format(nullptr, out_ff_path.c_str(), nullptr);
	}
	if (!oformat) {
		mContext.mpCallbacks->SetError("Unable to find a suitable output format");
		Finalize();
		return;
	}

	format_name = oformat->name;

	err = avformat_alloc_output_context2(&m_ofmt, oformat, nullptr, out_ff_path.c_str()); // filename needed for second pass
	if (err < 0) {
		av_error(err);
		Finalize();
		return;
	}
}

uint32 FFOutputFile::CreateStream(int type)
{
	size_t index = stream.size();
	stream.resize(index + 1);
	StreamInfo& s = stream[index];
	return (uint32)index;
}

typedef struct AVCodecTag {
	enum AVCodecID id;
	unsigned int tag;
} AVCodecTag;

void FFOutputFile::SetVideo(uint32 index, const VDXStreamInfo& si, const void* pFormat, int cbFormat)
{
	if (!m_ofmt) {
		return;
	}

	StreamInfo& s = stream[index];
	const VDXAVIStreamHeader& asi = si.aviHeader;
	if (s.st) {
		return;
	}

	AVStream* st = avformat_new_stream(m_ofmt, nullptr);
	st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

	import_bmp(st, pFormat, cbFormat);
	adjust_codec_tag(st);

	st->codecpar->format = (AVPixelFormat)si.format;
	st->codecpar->bit_rate = si.bit_rate;
	st->codecpar->bits_per_coded_sample = si.bits_per_coded_sample;
	st->codecpar->bits_per_raw_sample = si.bits_per_raw_sample;
	st->codecpar->profile = si.profile;
	st->codecpar->level = si.level;
	st->codecpar->sample_aspect_ratio.num = si.sar_width;
	st->codecpar->sample_aspect_ratio.den = si.sar_height;
	st->codecpar->field_order = (AVFieldOrder)si.field_order;
	st->codecpar->color_range = (AVColorRange)si.color_range;
	st->codecpar->color_primaries = (AVColorPrimaries)si.color_primaries;
	st->codecpar->color_trc = (AVColorTransferCharacteristic)si.color_trc;
	st->codecpar->color_space = (AVColorSpace)si.color_space;
	st->codecpar->chroma_location = (AVChromaLocation)si.chroma_location;
	st->codecpar->video_delay = si.video_delay;
	//?/avcodec_parameters_from_context(st->codecpar,st->codec);
	st->sample_aspect_ratio = st->codecpar->sample_aspect_ratio;

	st->avg_frame_rate = av_make_q(asi.dwRate, asi.dwScale);
	st->r_frame_rate = st->avg_frame_rate;

	st->time_base = av_make_q(asi.dwScale, asi.dwRate);

	s.st = st;
	s.time_base = st->time_base;
}

uint8_t* copy_extradata(AVCodecParameters* p)
{
	if (!p->extradata_size) {
		return 0;
	}
	uint8_t* data = (uint8_t*)av_mallocz(p->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(data, p->extradata, p->extradata_size);
	return data;
}

uint8_t* copy_extradata(AVCodecContext* c)
{
	if (!c->extradata_size) {
		return 0;
	}
	uint8_t* data = (uint8_t*)av_mallocz(c->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(data, c->extradata, c->extradata_size);
	return data;
}

void FFOutputFile::SetAudio(uint32 index, const VDXStreamInfo& si, const void* pFormat, int cbFormat)
{
	if (!m_ofmt) {
		return;
	}

	StreamInfo& s = stream[index];
	const VDXAVIStreamHeader& asi = si.aviHeader;
	if (s.st) {
		// push new extradata (updated by aac, flac and something else)
		//! this doesn't work:
		// avienc does not implement it at all
		// matroskaenc, movenc expect audio packet with side_data

		AVFormatContext* ofmt1 = avformat_alloc_context();
		AVStream* st1 = avformat_new_stream(ofmt1, nullptr);
		import_wav(st1, pFormat, cbFormat);
		if (s.st->codecpar->extradata_size) {
			av_freep(&s.st->codecpar->extradata);
		}
		s.st->codecpar->extradata_size = st1->codecpar->extradata_size;
		s.st->codecpar->extradata = copy_extradata(st1->codecpar);
		avformat_free_context(ofmt1);

		// workaround for vorbis delay
		// could have worked with side_data maybe
		if (si.version >= 2) {
			s.offset_num = si.start_num;
			s.offset_den = si.start_den;
		}
		return;
	}

	AVStream* st = avformat_new_stream(m_ofmt, nullptr);
	st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;

	import_wav(st, pFormat, cbFormat);

	if (m_ofmt->oformat == av_guess_format("aiff", nullptr, nullptr)) {
		if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE) {
			st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
			s.bswap_pcm = true;
		}
		if (st->codecpar->codec_id == AV_CODEC_ID_PCM_F32LE) {
			st->codecpar->codec_id = AV_CODEC_ID_PCM_F32BE;
			s.bswap_pcm = true;
		}
	}

	adjust_codec_tag(st);

	if (si.avcodec_version) {
		st->codecpar->block_align = si.block_align;
		st->codecpar->frame_size = si.frame_size;
		st->codecpar->initial_padding = si.initial_padding;
		st->codecpar->trailing_padding = si.trailing_padding;
	}

	//?/avcodec_parameters_from_context(st->codecpar,st->codec);

	st->time_base = av_make_q(asi.dwScale, asi.dwRate);

	if (si.version >= 2) {
		s.offset_num = si.start_num;
		s.offset_den = si.start_den;
	}

	s.st = st;
	s.time_base = st->time_base;
}

void* FFOutputFile::bswap_pcm(uint32 index, const void* pBuffer, uint32 cbBuffer)
{
	if (a_buf_size < cbBuffer) {
		a_buf = realloc(a_buf, cbBuffer);
		a_buf_size = cbBuffer;
	}

	StreamInfo& s = stream[index];
	switch (s.st->codecpar->codec_id) {
	case AV_CODEC_ID_PCM_S16LE:
	case AV_CODEC_ID_PCM_S16BE:
	case AV_CODEC_ID_PCM_U16LE:
	case AV_CODEC_ID_PCM_U16BE:
	{
		const uint16* a = (const uint16*)pBuffer;
		uint16* b = (uint16*)a_buf;
		for (uint32 i = 0; i < cbBuffer / 2; i++) {
			b[i] = _byteswap_ushort(a[i]);
		}
	}
	break;
	case AV_CODEC_ID_PCM_F32LE:
	case AV_CODEC_ID_PCM_F32BE:
	case AV_CODEC_ID_PCM_S32LE:
	case AV_CODEC_ID_PCM_S32BE:
	case AV_CODEC_ID_PCM_U32LE:
	case AV_CODEC_ID_PCM_U32BE:
	{
		const uint32* a = (const uint32*)pBuffer;
		uint32* b = (uint32*)a_buf;
		for (uint32 i = 0; i < cbBuffer / 4; i++) {
			b[i] = _byteswap_ulong(a[i]);
		}
	}
	break;
	case AV_CODEC_ID_PCM_F64LE:
	case AV_CODEC_ID_PCM_F64BE:
	case AV_CODEC_ID_PCM_S64LE:
	case AV_CODEC_ID_PCM_S64BE:
	{
		const uint64* a = (const uint64*)pBuffer;
		uint64* b = (uint64*)a_buf;
		for (uint32 i = 0; i < cbBuffer / 8; i++) {
			b[i] = _byteswap_uint64(a[i]);
		}
	}
	break;
	}

	return a_buf;
}

const AVCodecTag* avformat_get_nut_video_tags();

void FFOutputFile::import_bmp(AVStream* st, const void* pFormat, int cbFormat)
{
	BITMAPINFOHEADER* bm = (BITMAPINFOHEADER*)pFormat;
	DWORD tag = bm->biCompression;

	AVCodecID codec_id = AV_CODEC_ID_NONE;

	if (!codec_id) {
		const AVCodecTag* riff_tag = avformat_get_riff_video_tags();
		while (riff_tag->id != AV_CODEC_ID_NONE) {
			if (riff_tag->tag == tag) {
				codec_id = riff_tag->id;
				break;
			}
			riff_tag++;
		}
	}

	if (!codec_id) {
		const AVCodecTag* mov_tag = avformat_get_mov_video_tags();
		while (mov_tag->id != AV_CODEC_ID_NONE) {
			if (mov_tag->tag == tag) {
				codec_id = mov_tag->id;
				break;
			}
			mov_tag++;
		}
	}

	if (!codec_id) {
		const AVCodecTag* nut_tag = avformat_get_nut_video_tags();
		while (nut_tag->id != AV_CODEC_ID_NONE) {
			if (nut_tag->tag == tag) {
				codec_id = nut_tag->id;
				break;
			}
			nut_tag++;
		}
	}

	st->codecpar->codec_id = codec_id;
	st->codecpar->codec_tag = tag;
	st->codecpar->width = bm->biWidth;
	st->codecpar->height = bm->biHeight;

	if (cbFormat > sizeof(BITMAPINFOHEADER)) {
		st->codecpar->extradata_size = cbFormat - sizeof(BITMAPINFOHEADER);
		st->codecpar->extradata = (uint8_t*)av_mallocz(st->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
		memcpy(st->codecpar->extradata, ((char*)pFormat) + sizeof(BITMAPINFOHEADER), st->codecpar->extradata_size);
	}
}

void FFOutputFile::import_wav(AVStream* st, const void* pFormat, int cbFormat)
{
	if (cbFormat >= sizeof(WAVEFORMATEX_VDFF)) {
		const WAVEFORMATEX_VDFF* ff = (const WAVEFORMATEX_VDFF*)pFormat;
		if (ff->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && ff->SubFormat == KSDATAFORMAT_SUBTYPE_VDFF) {
			st->codecpar->codec_id = ff->codec_id;
			st->codecpar->codec_tag = 0;
			av_channel_layout_default(&st->codecpar->ch_layout, ff->Format.nChannels);
			st->codecpar->sample_rate = ff->Format.nSamplesPerSec;
			st->codecpar->block_align = ff->Format.nBlockAlign;
			st->codecpar->frame_size = ff->Samples.wSamplesPerBlock;
			st->codecpar->format = AV_SAMPLE_FMT_NONE;
			st->codecpar->bits_per_coded_sample = ff->Format.wBitsPerSample;
			st->codecpar->bit_rate = ff->Format.nAvgBytesPerSec * 8;

			if (cbFormat > sizeof(WAVEFORMATEX_VDFF)) {
				int size = cbFormat - sizeof(WAVEFORMATEX_VDFF);
				st->codecpar->extradata_size = size;
				st->codecpar->extradata = (uint8_t*)av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
				memcpy(st->codecpar->extradata, ff + 1, size);
			}
			return;
		}
	}

	uint32 dwHeader[5];
	dwHeader[0] = FOURCC_RIFF;
	dwHeader[1] = 20 - 8 + cbFormat;
	dwHeader[2] = mmioFOURCC('W', 'A', 'V', 'E');
	dwHeader[3] = mmioFOURCC('f', 'm', 't', ' ');
	dwHeader[4] = cbFormat;

	std::vector<char> wav;
	wav.resize(20 + cbFormat);
	memcpy(&wav[0], dwHeader, 20);
	memcpy(&wav[20], pFormat, cbFormat);
	if (cbFormat & 1) {
		wav.push_back(0);
	}

	dwHeader[0] = mmioFOURCC('d', 'a', 't', 'a');
	dwHeader[1] = 0;

	size_t p = wav.size();
	wav.resize(p + 8);
	memcpy(&wav[p], dwHeader, 8);

	IOBuffer buf;
	buf.copy(&wav[0], (int)wav.size());
	AVIOContext* avio_ctx = avio_alloc_context(nullptr, 0, 0, &buf, &IOBuffer::Read, nullptr, nullptr);

	AVFormatContext* fmt_ctx = avformat_alloc_context();
	fmt_ctx->pb = avio_ctx;
	int err = avformat_open_input(&fmt_ctx, nullptr, nullptr, nullptr);
	if (err < 0) {
		av_error(err);
		goto fail;
	}
	err = avformat_find_stream_info(fmt_ctx, nullptr);
	if (err < 0) {
		av_error(err);
		goto fail;
	}

	{
		AVStream* fs = fmt_ctx->streams[0];

		st->codecpar->codec_id = fs->codecpar->codec_id;
		st->codecpar->codec_tag = fs->codecpar->codec_tag;
		av_channel_layout_copy(&st->codecpar->ch_layout, &fs->codecpar->ch_layout);
		st->codecpar->sample_rate = fs->codecpar->sample_rate;
		st->codecpar->block_align = fs->codecpar->block_align;
		st->codecpar->frame_size = fs->codecpar->frame_size;
		st->codecpar->format = fs->codecpar->format;
		st->codecpar->bits_per_coded_sample = fs->codecpar->bits_per_coded_sample;
		st->codecpar->bit_rate = fs->codecpar->bit_rate;

		st->codecpar->extradata_size = fs->codecpar->extradata_size;
		st->codecpar->extradata = copy_extradata(fs->codecpar);
	}

fail:
	avformat_free_context(fmt_ctx);
	av_free(avio_ctx->buffer);
	av_free(avio_ctx);
}

void FFOutputFile::adjust_codec_tag(AVStream* st)
{
	// initial tag imported from bmp: suitable for avi
	::adjust_codec_tag("avi", m_ofmt->oformat, st);
}

bool FFOutputFile::Begin()
{
	bool r = test_streams();
	stream_test = true;
	return r;
}

bool FFOutputFile::test_streams()
{
	for (int i = 0; i < (int)stream.size(); i++) {
		StreamInfo& si = stream[i];
		AVCodecID codec_id = si.st->codecpar->codec_id;

		IOWBuffer io;
		int buf_size = 4096;
		void* buf = av_malloc(buf_size);
		AVIOContext* avio_ctx = avio_alloc_context((unsigned char*)buf, buf_size, 1, &io, nullptr, &IOWBuffer::Write, &IOWBuffer::Seek);
		AVFormatContext* ofmt = avformat_alloc_context();
		ofmt->pb = avio_ctx;
		ofmt->oformat = this->m_ofmt->oformat;
		AVStream* st = avformat_new_stream(ofmt, nullptr);
		avcodec_parameters_copy(st->codecpar, si.st->codecpar);
		st->sample_aspect_ratio = si.st->sample_aspect_ratio;
		st->avg_frame_rate = si.st->avg_frame_rate;
		st->r_frame_rate = st->avg_frame_rate;
		st->time_base = si.st->time_base;

		if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			if (ofmt->oformat == av_guess_format("mp4", nullptr, nullptr)) {
				st->codecpar->codec_tag = 0;
			}
		}

		bool failed = true;
		if (avformat_write_header(ofmt, nullptr) < 0) {
			goto cleanup;
		}
		if (av_write_trailer(ofmt) < 0) {
			goto cleanup;
		}
		failed = false;

	cleanup:
		av_free(avio_ctx->buffer);
		av_free(avio_ctx);
		avformat_free_context(ofmt);
		if (failed) {
			std::string msg;
			msg += avcodec_get_name(codec_id);
			msg += ": codec not currently supported in container ";
			msg += format_name;
			mContext.mpCallbacks->SetError(msg.c_str());
			return false;
		}
	}

	/*
	if(!m_ofmt->oformat->codec_tag) return true;

	for(int i=0; i<(int)stream.size(); i++) {
		StreamInfo& si = stream[i];
		AVCodecID codec_id = si.st->codecpar->codec_id;

		unsigned int codec_tag2;
		int have_codec_tag2 = av_codec_get_tag2(m_ofmt->oformat->codec_tag, codec_id, &codec_tag2);
		if(!have_codec_tag2) {
			std::string msg;
			msg += avcodec_get_name(codec_id);
			msg += ": codec not currently supported in container";
			mContext.mpCallbacks->SetError(msg.c_str());
			return false;
		}
	}
	*/
	return true;
}

#define FF_MOV_FLAG_FASTSTART             (1 <<  7)

void FFOutputFile::Write(uint32 index, const void* pBuffer, uint32 cbBuffer, PacketInfo& info)
{
	if (!m_ofmt) {
		return;
	}

	if (!header) {
		if (!stream_test && !test_streams()) {
			Finalize();
			return;
		}

		if (!(m_ofmt->oformat->flags & AVFMT_TS_NEGATIVE)) {
			m_ofmt->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;
		}

		int err = 0;
		if (!(m_ofmt->oformat->flags & AVFMT_NOFILE)) {
			err = avio_open(&m_ofmt->pb, m_out_ff_path.c_str(), AVIO_FLAG_WRITE);
			if (err < 0) {
				av_error(err);
				Finalize();
				return;
			}
		}

		AVDictionary* options = nullptr;
		if (mp4_faststart) {
			av_dict_set_int(&options, "movflags", FF_MOV_FLAG_FASTSTART, 0);
		}
		if (strcmp(m_ofmt->oformat->name, "avi") == 0) {
			// we take care of tags
			av_dict_set_int(&options, "strict", -2, 0);
		}

		err = avformat_write_header(m_ofmt, &options);
		av_dict_free(&options);
		if (err < 0) {
			av_error(err);
			Finalize();
			return;
		}
		header = true;
	}

	StreamInfo& s = stream[index];
	if (!s.st) {
		return;
	}

	AVPacket* pkt = av_packet_alloc();
	// pkt->data points to pBuffer or a_buf
	if (s.bswap_pcm) {
		pkt->data = (uint8*)bswap_pcm(index, pBuffer, cbBuffer);
	} else {
		pkt->data = (uint8*)pBuffer;
	}
	pkt->size = cbBuffer;

	if (info.flags & AVIIF_KEYFRAME) {
		pkt->flags = AV_PKT_FLAG_KEY;
	}

	pkt->pos = -1;
	pkt->stream_index = s.st->index;
	pkt->pts = s.frame;
	if (info.pts != VDX_NOPTS_VALUE) {
		pkt->pts = info.pts;
		pkt->dts = info.dts;
	}

	int64_t samples = info.samples;
	if (info.pcm_samples != -1) {
		samples = info.pcm_samples;
		s.time_base = av_make_q(1, s.st->codecpar->sample_rate);
	}
	pkt->duration = samples;
	av_packet_rescale_ts(pkt, s.time_base, s.st->time_base);

	if (s.offset_num != 0) {
		double r = double(s.offset_num) * s.st->time_base.den / (s.offset_den * s.st->time_base.num);
		pkt->pts += r > 0 ? int64_t(r + 0.5) : int64_t(r - 0.5);
		//pkt.pts += av_rescale_q_rnd(s.offset_num, av_make_q(1,(int)s.offset_den), s.st->time_base, AV_ROUND_NEAR_INF);
	}

	s.frame += samples;

	int err = av_interleaved_write_frame(m_ofmt, pkt);
	if (err < 0) {
		av_error(err);
	}

	// pkt->data no longer points to pBuffer or a_buf
	pkt->data = nullptr;
	pkt->size = 0;
	av_packet_free(&pkt);
};

void FFOutputFile::Finalize()
{
	if (header) {
		av_write_trailer(m_ofmt);
	}

	if (m_ofmt && !(m_ofmt->oformat->flags & AVFMT_NOFILE)) {
		avio_closep(&m_ofmt->pb);
	}
	avformat_free_context(m_ofmt);
	m_ofmt = nullptr;
}

enum {
	f_avi,
	f_mkv,
	f_mka,
	f_webm,
	f_mov,
	f_mov_faststart,
	f_mp4,
	f_mp4_faststart,
	f_m4a,
	f_aiff,
	f_nut,
	f_ext,
	f_exta,
};

uint32 VDXAPIENTRY VDFFOutputFileDriver::GetFormatCaps(int i)
{
	switch (i) {
	case f_avi:
		return kFormatCaps_UseVideo | kFormatCaps_UseAudio;
	case f_mkv:
	case f_webm:
	case f_mov:
	case f_mp4:
	case f_mov_faststart:
	case f_mp4_faststart:
	case f_nut:
		return kFormatCaps_UseVideo | kFormatCaps_UseAudio | kFormatCaps_OffsetStreams;
	case f_mka:
	case f_m4a:
	case f_aiff:
		return kFormatCaps_UseAudio;
	case f_ext:
		return kFormatCaps_UseVideo | kFormatCaps_UseAudio | kFormatCaps_Wildcard;
	case f_exta:
		return kFormatCaps_UseAudio | kFormatCaps_Wildcard;
	}
	return 0;
}

bool VDXAPIENTRY VDFFOutputFileDriver::EnumFormats(int i, wchar_t* filter, wchar_t* ext, char* name)
{
	switch (i) {
	case f_avi:
		wcscpy_s(filter, 128, L"AVI handled by FFMPEG (*.avi)");
		wcscpy_s(ext, 128, L"*.avi");
		strcpy_s(name, 128, "avi");
		return true;
	case f_mkv:
		wcscpy_s(filter, 128, L"Matroska (*.mkv)");
		wcscpy_s(ext, 128, L"*.mkv");
		strcpy_s(name, 128, "matroska");
		return true;
	case f_mka:
		wcscpy_s(filter, 128, L"Matroska Audio (*.mka)");
		wcscpy_s(ext, 128, L"*.mka");
		strcpy_s(name, 128, "matroska");
		return true;
	case f_webm:
		wcscpy_s(filter, 128, L"WebM (*.webm)");
		wcscpy_s(ext, 128, L"*.webm");
		strcpy_s(name, 128, "webm");
		return true;
	case f_mov:
		wcscpy_s(filter, 128, L"QuickTime / MOV (*.mov)");
		wcscpy_s(ext, 128, L"*.mov");
		strcpy_s(name, 128, "mov");
		return true;
	case f_mov_faststart:
		wcscpy_s(filter, 128, L"MOV +faststart (*.mov)");
		wcscpy_s(ext, 128, L"*.mov");
		strcpy_s(name, 128, "mov+faststart");
		return true;
	case f_mp4:
		wcscpy_s(filter, 128, L"MP4 (MPEG-4 Part 14) (*.mp4)");
		wcscpy_s(ext, 128, L"*.mp4");
		strcpy_s(name, 128, "mp4");
		return true;
	case f_mp4_faststart:
		wcscpy_s(filter, 128, L"MP4 +faststart (*.mp4)");
		wcscpy_s(ext, 128, L"*.mp4");
		strcpy_s(name, 128, "mp4+faststart");
		return true;
	case f_m4a:
		wcscpy_s(filter, 128, L"M4A (MPEG-4 Part 14) (*.m4a)");
		wcscpy_s(ext, 128, L"*.m4a");
		strcpy_s(name, 128, "ipod");
		return true;
	case f_aiff:
		wcscpy_s(filter, 128, L"Audio IFF (*.aiff)");
		wcscpy_s(ext, 128, L"*.aiff");
		strcpy_s(name, 128, "aiff");
		return true;
	case f_nut:
		wcscpy_s(filter, 128, L"NUT (*.nut)");
		wcscpy_s(ext, 128, L"*.nut");
		strcpy_s(name, 128, "nut");
		return true;
	case f_ext:
	case f_exta:
		wcscpy_s(filter, 128, L"any format by FFMPEG (*.*)");
		wcscpy_s(ext, 128, L"*.*");
		strcpy_s(name, 128, "");
		return true;
	}
	return false;
}

bool VDXAPIENTRY ff_create_output(const VDXInputDriverContext* pContext, IVDXOutputFileDriver** ppDriver)
{
	VDFFOutputFileDriver* p = new VDFFOutputFileDriver(*pContext);
	if (!p) {
		return false;
	}
	*ppDriver = p;
	p->AddRef();
	return true;
}

VDXOutputDriverDefinition ff_output = {
	sizeof(VDXOutputDriverDefinition),
	0, //flags
	L"FFmpeg (all formats)",
	L"ffmpeg",
	ff_create_output
};

VDXPluginInfo ff_output_info = {
	sizeof(VDXPluginInfo),
	L"FFmpeg output",
	L"Anton Shekhovtsov",
	L"Save files through ffmpeg libs.",
	1,
	kVDXPluginType_Output,
	0,
	12, // min api version
	kVDXPlugin_APIVersion,
	kVDXPlugin_OutputDriverAPIVersion, // min output api version
	kVDXPlugin_OutputDriverAPIVersion,
	&ff_output
};
