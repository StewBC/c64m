# c64m - Commodore 64 Emulator

This emulator was written primarily by Codex, OpenAI's coding agent, working from task specifications generated through ChatGPT. My workflow is to interact with ChatGPT, have it write implementation tasks as `.md` files in the `md-files` folder, and then have Codex use those files to implement the required functionality.

The VIC-II was primarily coded by Claude Code. I did use Codex and Claude interchangeably to compare the differences between them on various VIC-II tasks. I also used Claude to scope the work for bringing the `a2m` assembler, with improvements, into c64m. I then had Codex do the implementation work.

With three exceptions, all of the code in this project was written by Codex without me directly reviewing the implementation as it was being developed. The exceptions are:

1. The 6510 CPU implementation.
2. Portions of the Nuklear layout code.
3. The assembler, as mentioned above.

In those cases, I asked Codex and Claude to refer to code from my `a2m` Apple II emulator. For the CPU, Codex reused my implementation almost verbatim. For the layout code, it wrote new code, but the examples I provided helped communicate the desired design and behavior. For the assembler, it was a mix: it kept a lot of the old code and concepts, more than I would have liked.

The overall architecture of the emulator is my design. I specified a layered structure consisting of:

* Machine
* Runtime
* Platform
* Frontend

I also specified that the runtime and frontend should run on separate threads and communicate through message queues.

In addition, I created a tools layer where utilities such as the assembler will live.

## Stats

The times below are total elapsed development time, including my own time thinking through requirements, writing task specifications, refining plans, and communicating with ChatGPT and Codex. For example, the breakpoint system alone took about an hour for me to think through and write out clearly enough for an implementation plan to be produced.

After approximately 6 hours and 40 minutes of development time, the emulator booted to BASIC, displayed the familiar startup screen, accepted keyboard input, and could run BASIC programs. At that point, the emulation speed was not yet correct.

After about 7 hours, the keyboard mapping had been refined to a very usable state, and the emulation speed was much closer to correct. BASIC was functioning well, including commands such as the following doing what you would expect:

```basic
POKE 53281,0
POKE 53280,1
SYS 64738
```

Now, after 19 hours, the VIC-II is quite alive: text modes, graphics, and sprites.

## Next Steps

Development will continue. In truth, ChatGPT and Codex are doing the implementation work. Perhaps this process works especially well because I have previously written an emulator and can clearly describe the architecture and requirements.

Regardless, what AI is capable of today is genuinely impressive.

FWIW - At the 19-hour mark, I very much prefer Codex over Claude AI. It is more consistent, and GPT-5.5 Low performs better than Sonnet 4.6 Medium for this project. I have Codex speed set to Standard, and it is much faster at comparable tasks than Claude on Medium.

Stefan Wessels
[swessels@email.com](mailto:swessels@email.com)
June 14, 2026
