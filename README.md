# C64m - Commodore 64 Emulator

This emulator was written primarily by Codex, OpenAI's coding agent, working from task specifications generated through ChatGPT. My workflow is to interact with ChatGPT, have it write implementation tasks as `.md` files in the `md-files` folder, and then have Codex use those files to implement the required functionality.

With two exceptions, all of the code in this project was written by Codex without me directly reviewing the implementation as it was being developed. The exceptions are:

1. The 6510 CPU implementation.
2. Portions of the Nuklear layout code.

In both cases, I asked Codex to refer to code from my `a2m` Apple II emulator. For the CPU, it reused my implementation almost verbatim. For the layout code, it wrote new code, but the examples I provided helped communicate the desired design and behavior.

The overall architecture of the emulator is my design. I specified a layered structure consisting of:

- Machine
- Runtime
- Platform
- Frontend

I also specified that the runtime and frontend should run on separate threads and communicate through message queues.

In addition, I created a tools layer where utilities such as the assembler will live. I may eventually integrate the assembler from my `a2m` project, although I may instead have Codex develop a new one based on the design and capabilities of the existing assembler.

## Stats

After approximately 6 hours and 40 minutes of development time, the emulator booted to BASIC, displayed the familiar startup screen, accepted keyboard input, and could run BASIC programs. At that point, the emulation speed was not yet correct.

After about 7 hours, the keyboard mapping had been refined to a very usable state and the emulation speed was much closer to correct. BASIC was functioning well, including commands such as the below doing what you would expect:

```basic
POKE 53281,0
POKE 53280,1
SYS 64738
```

## Next Steps

Development will continue. In truth, ChatGPT and Codex are doing the implementation work. Perhaps this process works especially well because I have previously written an emulator and can clearly describe the architecture and requirements.

Regardless, what AI is capable of today is genuinely impressive.

Stefan Wessels  
swessels@email.com  
June 14, 2026  
