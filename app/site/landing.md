# MYCEL-8

MYCEL-8 is for growing music rather than writing it. Each of eight parts is a handful of numbers: a Euclidean rhythm (pulses spread over steps), and a Turing-machine shift register that loops and, by a MUTATE amount, slowly changes. The register picks its notes from a pool of scale degrees lit on the white keys, so whatever it plays is in key. The design follows what the research found players love and hate about generative tools: randomness always has a LOCK, dice keep a history so a good moment is never lost, density is one knob, and eight snapshots return the whole field at the next bar.

![MYCEL-8 on the FM-1's screen](manual/img/screen-field.png "MYCEL-8 on the FM-1's screen")

Version {{VERSION}}. **Untested on a real FM-1 so far:** it builds, passes its tests in fm1-base's simulator, and
runs in the browser emulator. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): what it is for, the controls, the screens.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/davidpryan/mycel-8): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by Leo
  Kuroshita, Hügelton Instruments. One of six sequencer firmwares built from a survey of sequencer archetypes.

M-VAVE and FM-1 are trademarks of their owners. MYCEL-8 is not affiliated with M-VAVE.
