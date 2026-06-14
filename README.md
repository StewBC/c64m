# C64m - Commodore 64 Emulator
This emulator is written by Codex, the coding agent of ChartGPT, and by ChatGPT web client.  I interact with the web client and get it to write tasks as .md files in the md-files folder.  I then get Codex to use these files for the implementation.  All of this code was written, with 2 exceptions, by Codex.  It was written without me loking at the code.  The exceptions are the 6510 CPU and the to a lesser extent the Nuklear layout code.  In both those cases I asked Codex to look at the code in my a2m Apple II emulator.  In the case of the CPU it more or less took my code as-is.  In the case of the layout code, it wrote new code, but the code I showed it made clear what I wanted.

The structure of the emulator is mine.  I described that I wanted a machine, runtime, platform and then frontend on top of that.  I also said that I wanted runtime on a thread and frontend on a thread and they should communicate via message queues.  I also added tools, where the assembler will live.  I will probably bring in my a2m assembler, but maybe I'll get it to write a new one, based on the specs of the a2m one.

## Stats:
After 6h40m of me sitting in my chair, I had the C64 boot to basic with the fammiliar screen, and I could type and run basic programs.  The emulation speed was not correct.  Ater 7 hours, I had the keyboard mapped to a very usable point and the emulation speed was also good.  Things like POKE 53281,0 and POKE 53280,1 worked.  Basic worked, incl. SYS 64738, for example.

## Steps
I'll keep working on this.  Truth is, ChatGPT/Codex is working on this.  Maybe it's going so well because I have written an Emulator but what AI is capable of now, is mind-blowing.

Stefan Wessels
swessels@email.com
June 14, 2026
