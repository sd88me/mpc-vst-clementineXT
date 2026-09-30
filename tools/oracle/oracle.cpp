// Firmware oracle: boots the XT firmware (gearmulator xtLib) from the user's own ROM and answers questions about it.
// Run from the folder holding the ROM .bin files (gearmulator searches the working directory).
//   oracle dump <out.syx> [--set IDX VAL]...      set SDATA params by SNDP, then dump the edit buffer to a file
//   oracle dumpall <out.syx>                      dump all 256 sounds (a bank file)
//   oracle waves <out.bin> LO HI                  dump waves by number (records: u16 index + 64 signed bytes)
//   oracle tables <out.bin>                       dump wave control tables (records: u16 table + 64 x u16 wave numbers)
//   oracle dspdiff <tabA> <tabB> <out.bin>        DSP Y-memory words that change when the wavetable changes (records: addr, before, after)
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
