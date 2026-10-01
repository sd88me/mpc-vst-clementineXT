# Vendored code

Third-party source committed into this repo (never fetched at build time; mpc-vst-plugins docs/PORTING.md).
For each entry: upstream URL, exact commit, licence, and every local change.

| Component | Upstream | Commit | Licence | Local changes |
|---|---|---|---|---|
| Schwung plugin API header (`src/vendor/schwung/plugin_api_v1.h`) | https://github.com/charlesvestal/schwung `src/host/plugin_api_v1.h` | 53861b80 | MIT | none |
| mpc-vst-plugins engine interface (`src/vendor/mpc-vst-plugins/engine.h`) | https://github.com/sd88me/mpc-vst-plugins `wrapper/engine.h` | 0c2081e | GPL-3.0 (same author) | none; the Schwung build includes it from here so the engine builds without that checkout |

Candidates (docs/DESIGN.md section 10): sst-filters / sst-basic-blocks (GPL-3.0), simde (MIT),
schwung-tablor pieces (BSD-3-Clause), gearmulator ROM/OS parsing and parameter value texts (GPL-3.0).
