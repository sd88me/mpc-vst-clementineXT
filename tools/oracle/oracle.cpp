// Firmware oracle: boots the XT firmware (gearmulator xtLib) from the user's own ROM and answers questions about it.
// Run from the folder holding the ROM .bin files (gearmulator searches the working directory).
//   oracle dump <out.syx> [--set IDX VAL]...      set SDATA params by SNDP, then dump the edit buffer to a file
//   oracle dumpall <out.syx>                      dump all 256 sounds (a bank file)
//   oracle waves <out.bin> LO HI                  dump waves by number (records: u16 index + 64 signed bytes)
//   oracle tables <out.bin>                       dump wave control tables (records: u16 table + 64 x u16 wave numbers)
//   oracle dspdiff <tabA> <tabB> <out.bin>        DSP Y-memory words that change when the wavetable changes (records: addr, before, after)
//   oracle render <sound.bin|-> <out.f32> <note> <vel> <hold_blocks> <tail_blocks> [--set IDX VAL] [--cc N V] [--bend V]...   float32 left channel, 40 kHz
//   oracle sweep IDX LO HI                        set IDX to each value, print the value read back and the LCD text
// Output files stay on the developer's machine (see CLAUDE.md ground rules).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "synthLib/midiTypes.h"
#include "xtLib/xt.h"
#include "xtLib/xtHardware.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/audio.h"
#include <cmath>

static std::unique_ptr<xt::Xt> g_xt;
static std::vector<uint8_t> g_rx;

static void run(uint32_t blocks)
{
	for (uint32_t i = 0; i < blocks; ++i)
	{
		g_xt->process(64);
		std::vector<uint8_t> b;
		g_xt->receiveMidi(b);
		g_rx.insert(g_rx.end(), b.begin(), b.end());
	}
}

static void sendSysex(std::vector<uint8_t> s)
{
	synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
	e.sysex.assign(s.begin(), s.end());
	g_xt->sendMidiEvent(e);
}

static void setParam(int idx, int val)
{
	sendSysex({0xf0, 0x3e, 0x0e, 0x7f, 0x20, 0x00, (uint8_t)(idx >> 7), (uint8_t)(idx & 127), (uint8_t)val, 0xf7});
	run(200);
}

// request the edit buffer; returns the 256 SDATA bytes or empty
static std::vector<uint8_t> dumpEdit()
{
	g_rx.clear();
	sendSysex({0xf0, 0x3e, 0x0e, 0x7f, 0x00, 0x20, 0x00, 0x20, 0xf7});
	for (int i = 0; i < 400; ++i)
	{
		run(50);
		if (g_rx.size() >= 265 && g_rx[264] == 0xf7) break;
	}
	if (g_rx.size() < 265 || g_rx[0] != 0xf0 || g_rx[4] != 0x10) return {};
	return std::vector<uint8_t>(g_rx.begin() + 7, g_rx.begin() + 7 + 256);
}

// request all 256 sounds (A001..B128); returns the raw 65545-byte message
static std::vector<uint8_t> dumpAll()
{
	g_rx.clear();
	sendSysex({0xf0, 0x3e, 0x0e, 0x7f, 0x00, 0x10, 0x00, 0x10, 0xf7});
	for (int i = 0; i < 20000; ++i)
	{
		run(50);
		if (g_rx.size() >= 65545 && g_rx[65544] == 0xf7) break;
	}
	if (g_rx.size() < 65545) return {};
	return std::vector<uint8_t>(g_rx.begin(), g_rx.begin() + 65545);
}

// send a request and wait for a reply that ends with F7; empty on timeout (algorithmic tables and empty locations never answer)
static std::vector<uint8_t> request(std::vector<uint8_t> req, uint32_t maxBlocks = 3000)
{
	g_rx.clear();
	sendSysex(std::move(req));
	for (uint32_t i = 0; i < maxBlocks; i += 25)
	{
		run(25);
		if (!g_rx.empty() && g_rx.back() == 0xf7) return g_rx;
	}
	return {};
}

static std::vector<uint8_t> waveReq(int n)   // ROM 0..511 and user 1000..1249 by the spec's HH LL location scheme
{
	const int hh = n >> 7, ll = n & 127;
	return {0xf0, 0x3e, 0x0e, 0x7f, 0x02, (uint8_t)hh, (uint8_t)ll, (uint8_t)((hh + ll) & 127), 0xf7};
}

static dsp56k::Memory& dspMem() { return g_xt->getHardware()->getDSP(0).dsp().memory(); }

static std::vector<uint32_t> snapshotY(uint32_t lo, uint32_t hi)
{
	std::vector<uint32_t> v(hi - lo);
	for (uint32_t i = lo; i < hi; ++i) v[i - lo] = dspMem().get(dsp56k::MemArea_Y, i);
	return v;
}

static void sendMidi(uint8_t a, uint8_t b, uint8_t c)
{
	synthLib::SMidiEvent e(synthLib::MidiEventSource::Host, a, b, c);
	g_xt->sendMidiEvent(e);
}

// switch to Sound mode and load a 256-byte SDATA file into the edit buffer (checksum = SDATA sum, the firmware's form)
static bool loadSound(const char* path)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (d.size() != 256) return false;
	sendSysex({0xf0, 0x3e, 0x0e, 0x7f, 0x17, 0x00, 0xf7});
	run(3000);
	std::vector<uint8_t> m = {0xf0, 0x3e, 0x0e, 0x7f, 0x10, 0x20, 0x00};
	int sum = 0;
	for (auto b : d) { m.push_back(b); sum += b; }
	m.push_back((uint8_t)(sum & 127)); m.push_back(0xf7);
	sendSysex(std::move(m));
	run(3000);
	return true;
}

// options after the fixed arguments: --set IDX VAL (sound parameter by SNDP), --cc NUM VAL (MIDI controller on channel 1),
// --bend VAL (14-bit pitch bend, 8192 = centre)
static void applyOpts(int from, int argc, char** argv)
{
	for (int i = from; i < argc;)
	{
		if (!strcmp(argv[i], "--set") && i + 2 < argc) { setParam(atoi(argv[i + 1]), atoi(argv[i + 2])); i += 3; }
		else if (!strcmp(argv[i], "--cc") && i + 2 < argc) { sendMidi(0xB0, (uint8_t)atoi(argv[i + 1]), (uint8_t)atoi(argv[i + 2])); run(200); i += 3; }
		else if (!strcmp(argv[i], "--bend") && i + 1 < argc) { const int v = atoi(argv[i + 1]); sendMidi(0xE0, (uint8_t)(v & 127), (uint8_t)(v >> 7)); run(200); i += 2; }
		else ++i;
	}
}

// same messages as applyOpts but sent after the note has started: --acc NUM VAL, --abend VAL
static void applyLateOpts(int from, int argc, char** argv)
{
	for (int i = from; i < argc;)
	{
		if (!strcmp(argv[i], "--acc") && i + 2 < argc) { sendMidi(0xB0, (uint8_t)atoi(argv[i + 1]), (uint8_t)atoi(argv[i + 2])); run(100); i += 3; }
		else if (!strcmp(argv[i], "--abend") && i + 1 < argc) { const int v = atoi(argv[i + 1]); sendMidi(0xE0, (uint8_t)(v & 127), (uint8_t)(v >> 7)); run(100); i += 2; }
		else ++i;
	}
}

static std::string lcd()
{
	std::array<char, 80> d{};
	g_xt->readLCD(d);
	std::string s;
	for (int r = 0; r < 2; ++r) { s.append(d.data() + r * 40, 40); s += r ? "" : " | "; }
	return s;
}

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: oracle dump <out.syx> [--set IDX VAL]... | oracle sweep IDX LO HI\n"); return 2; }
	g_xt = std::make_unique<xt::Xt>(std::vector<uint8_t>(), std::string());
	if (!g_xt->isValid()) { fprintf(stderr, "no valid ROM found in the working directory\n"); return 1; }
	while (!g_xt->isBootCompleted()) g_xt->process(64);
	run(5000);
	fprintf(stderr, "booted\n");

	if (!strcmp(argv[1], "sweep") && argc == 5)
	{
		int idx = atoi(argv[2]), lo = atoi(argv[3]), hi = atoi(argv[4]);
		for (int v = lo; v <= hi; ++v)
		{
			setParam(idx, v);
			auto d = dumpEdit();
			printf("%3d -> %s\n", v, d.empty() ? "no dump" : std::to_string(d[idx]).c_str());
		}
		return 0;
	}
	// waves <out.bin> <lo> <hi>: records of u16 index + 64 signed samples (the stored first half)
	if (!strcmp(argv[1], "waves") && argc == 5)
	{
		std::ofstream f(argv[2], std::ios::binary);
		int got = 0;
		for (int n = atoi(argv[3]); n <= atoi(argv[4]); ++n)
		{
			auto r = request(waveReq(n));
			if (r.size() != 137 || r[4] != 0x12) { fprintf(stderr, "wave %d: no dump\n", n); continue; }
			uint16_t idx = (uint16_t)n; f.write((const char*)&idx, 2);
			for (int i = 0; i < 64; ++i)
			{
				const int8_t v = (int8_t)(((r[7 + 2 * i] << 4) | (r[8 + 2 * i] & 15)) ^ 0x80);
				f.write((const char*)&v, 1);
			}
			++got;
		}
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[2]); return 1; }
		printf("%d waves -> %s\n", got, argv[2]);
		return 0;
	}
	// tables <out.bin>: records of u16 table number + 64 x u16 wave numbers, for the tables that have a control table
	if (!strcmp(argv[1], "tables") && argc == 3)
	{
		std::ofstream f(argv[2], std::ios::binary);
		int got = 0;
		for (int n = 0; n < 128; ++n)
		{
			auto r = request({0xf0, 0x3e, 0x0e, 0x7f, 0x03, 0x00, (uint8_t)n, (uint8_t)n, 0xf7}, 1200);
			if (r.size() != 265 || r[4] != 0x13) { fprintf(stderr, "table %d: no control table\n", n); continue; }
			uint16_t idx = (uint16_t)n; f.write((const char*)&idx, 2);
			for (int i = 0; i < 64; ++i)
			{
				const uint16_t v = (uint16_t)(((r[7 + 4 * i] & 15) << 12) | ((r[8 + 4 * i] & 15) << 8) | ((r[9 + 4 * i] & 15) << 4) | (r[10 + 4 * i] & 15));
				f.write((const char*)&v, 2);
			}
			++got;
		}
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[2]); return 1; }
		printf("%d control tables -> %s\n", got, argv[2]);
		return 0;
	}
	// dspdiff <tableA> <tableB> <out.bin>: which Y-memory words change when the sound's wavetable changes A -> B (0-based table numbers)
	if (!strcmp(argv[1], "dspdiff") && argc == 5)
	{
		const uint32_t hi = std::min<uint32_t>(dspMem().size(dsp56k::MemArea_Y), 0x100000);
		setParam(25, atoi(argv[2])); run(4000);
		const auto a = snapshotY(0, hi);
		setParam(25, atoi(argv[3])); run(4000);
		const auto b = snapshotY(0, hi);
		for (int side = 0; side < 2; ++side)   // the wave region of part 0 for each table: 64 waves x 256 words
		{
			std::ofstream w(std::string(argv[4]) + (side ? ".b" : ".a"), std::ios::binary);
			const auto& v = side ? b : a;
			w.write((const char*)&v[0x20000], 0x4000 * sizeof(uint32_t));
		}
		std::ofstream f(argv[4], std::ios::binary);
		uint32_t changed = 0, runStart = 0, runLen = 0, runs = 0;
		auto flush = [&]() { if (runLen) { printf("run at 0x%06x len %u\n", runStart, runLen); ++runs; } runLen = 0; };
		for (uint32_t i = 0; i < hi; ++i)
		{
			if (a[i] != b[i])
			{
				++changed;
				if (runLen && runStart + runLen == i) ++runLen; else { flush(); runStart = i; runLen = 1; }
				const uint32_t rec[3] = {i, a[i], b[i]};
				f.write((const char*)rec, sizeof rec);
			}
		}
		flush();
		printf("Y size 0x%x scanned 0x%x, %u words changed in %u runs\n", dspMem().size(dsp56k::MemArea_Y), hi, changed, runs);
		return 0;
	}
	// render <sound.bin|-> <out.f32> <note> <vel> <hold_blocks> <tail_blocks> [--set IDX VAL]...
	// raw little-endian float32, left channel, 40 kHz, 64-frame blocks; the sound file is 256 SDATA bytes
	if (!strcmp(argv[1], "render") && argc >= 8)
	{
		if (strcmp(argv[2], "-") && !loadSound(argv[2])) { fprintf(stderr, "cannot load sound %s\n", argv[2]); return 1; }
		applyOpts(8, argc, argv);
		const int note = atoi(argv[4]), vel = atoi(argv[5]), hold = atoi(argv[6]), tail = atoi(argv[7]);
		std::ofstream f(argv[3], std::ios::binary);
		bool stereo = false;
		for (int i = 8; i < argc; ++i) if (!strcmp(argv[i], "--stereo")) stereo = true;
		auto capture = [&](int blocks) {
			for (int b = 0; b < blocks; ++b)
			{
				g_xt->process(64);
				auto& outs = g_xt->getAudioOutputs();
				for (int i = 0; i < 64; ++i)
				{
					const int32_t w = (int32_t)((uint32_t)outs[0][i] << 8) >> 8;   // 24-bit signed in a 32-bit word
					const float v = (float)w / 8388608.0f;
					f.write((const char*)&v, 4);
					if (stereo)   // --stereo: interleave the right channel after each left sample
					{
						const int32_t wr = (int32_t)((uint32_t)outs[1][i] << 8) >> 8;
						const float vr = (float)wr / 8388608.0f;
						f.write((const char*)&vr, 4);
					}
				}
			}
		};
		int note2 = -1, at2 = 0;   // --n2 NOTE BLOCKS: a second key goes down BLOCKS after the first (both held)
		for (int i = 8; i + 2 < argc; ++i) if (!strcmp(argv[i], "--n2")) { note2 = atoi(argv[i + 1]); at2 = atoi(argv[i + 2]); }
		sendMidi(0x90, (uint8_t)note, (uint8_t)vel);
		applyLateOpts(8, argc, argv);
		if (note2 >= 0 && at2 < hold) { capture(at2); sendMidi(0x90, (uint8_t)note2, (uint8_t)vel); capture(hold - at2); }
		else capture(hold);
		sendMidi(0x80, (uint8_t)note, 0);
		capture(tail);
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[3]); return 1; }
		printf("rendered %d blocks -> %s\n", hold + tail, argv[3]);
		return 0;
	}
	// ext <sound.bin> <in.f32> <out.f32> <note> <blocks> <mode> [--set IDX VAL]...
	// Feed a test signal into the external input (mix External must be up in the sound) with a note held, and record the left
	// output. mode: noise (white, fixed seed), impulse (one click per 8192 samples), sine:<Hz>. Both files float32, 40 kHz.
	if (!strcmp(argv[1], "ext") && argc >= 8)
	{
		if (strcmp(argv[2], "-") && !loadSound(argv[2])) { fprintf(stderr, "cannot load sound %s\n", argv[2]); return 1; }
		applyOpts(8, argc, argv);
		const int note = atoi(argv[5]), blocks = atoi(argv[6]);
		const std::string mode = argv[7];
		std::ofstream fin(argv[3], std::ios::binary), fout(argv[4], std::ios::binary);
		uint32_t rng = 12345; uint64_t n = 0;
		auto sig = [&]() -> float {
			if (mode == "noise") { rng = rng * 1664525u + 1013904223u; return ((int32_t)rng / 2147483648.0f) * 0.3f; }
			if (mode == "impulse") return (n % 8192 == 0) ? 0.5f : 0.0f;
			return 0.3f * sinf(6.2831853f * (float)atof(mode.c_str() + 5) * (float)n / 40000.0f);
		};
		sendMidi(0x90, (uint8_t)note, 100);
		for (int b = 0; b < blocks; ++b)
		{
			auto& ins = g_xt->getAudioInputs();
			float in[64];
			for (int i = 0; i < 64; ++i) { in[i] = sig(); ++n; ins[0][i] = ins[1][i] = dsp56k::sample2dsp(in[i]); }
			g_xt->process(64);
			auto& outs = g_xt->getAudioOutputs();
			float o[64];
			for (int i = 0; i < 64; ++i) o[i] = (float)((int32_t)((uint32_t)outs[0][i] << 8) >> 8) / 8388608.0f;
			fin.write((const char*)in, sizeof in); fout.write((const char*)o, sizeof o);
		}
		fin.flush(); fout.flush();
		if (!fin || !fout) { fprintf(stderr, "could not write outputs\n"); return 1; }
		printf("ext %s: %d blocks\n", mode.c_str(), blocks);
		return 0;
	}
	// tabledump <lo> <hi> <out.bin>: for each wavetable number lo..hi (0-based), select it on the sound and record the firmware's built table
	// (64 waves x 256 words of DSP Y memory at 0x20000): records of u16 table + 16384 x u32
	if (!strcmp(argv[1], "tabledump") && argc == 5)
	{
		std::ofstream f(argv[4], std::ios::binary);
		for (int t = atoi(argv[2]); t <= atoi(argv[3]); ++t)
		{
			setParam(25, t); run(3000);
			const auto v = snapshotY(0x20000, 0x24000);
			const uint16_t id = (uint16_t)t; f.write((const char*)&id, 2);
			f.write((const char*)v.data(), v.size() * sizeof(uint32_t));
		}
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[4]); return 1; }
		printf("tables %s..%s -> %s\n", argv[2], argv[3], argv[4]);
		return 0;
	}
	if (!strcmp(argv[1], "dumpall") && argc == 3)
	{
		auto d = dumpAll();
		if (d.empty()) { fprintf(stderr, "no all-sounds dump (%zu bytes)\n", g_rx.size()); return 1; }
		std::ofstream f(argv[2], std::ios::binary);
		f.write((const char*)d.data(), (std::streamsize)d.size());
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[2]); return 1; }
		printf("wrote %s (%zu bytes)\n", argv[2], d.size());
		return 0;
	}
	if (!strcmp(argv[1], "dump") && argc >= 3)
	{
		for (int i = 3; i + 2 < argc; i += 3)
			if (!strcmp(argv[i], "--set")) setParam(atoi(argv[i + 1]), atoi(argv[i + 2]));
		auto d = dumpEdit();
		if (d.empty()) { fprintf(stderr, "no dump received (%zu bytes)\n", g_rx.size()); return 1; }
		std::ofstream f(argv[2], std::ios::binary);
		f.write((const char*)g_rx.data(), 265);
		f.flush();
		if (!f) { fprintf(stderr, "could not write %s\n", argv[2]); return 1; }
		printf("wrote %s; LCD: %s\n", argv[2], lcd().c_str());
		return 0;
	}
	fprintf(stderr, "bad arguments\n");
	return 2;
}
