// Firmware oracle: boots the XT firmware (gearmulator xtLib) from the user's own ROM and answers questions about it.
// Run from the folder holding the ROM .bin files (gearmulator searches the working directory).
//   oracle dump <out.syx> [--set IDX VAL]...      set SDATA params by SNDP, then dump the edit buffer to a file
//   oracle dumpall <out.syx>                      dump all 256 sounds (a bank file)
//   oracle sweep IDX LO HI                        set IDX to each value, print the value read back and the LCD text
// Output files stay on the developer's machine (see CLAUDE.md ground rules).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "synthLib/midiTypes.h"
#include "xtLib/xt.h"

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
