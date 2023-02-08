# MiniMig code repository
Code repository is for reference only, copied from https://code.google.com/archive/p/minimig/  
Includes my PIC latest sources that might not be committed to original svn repo on google code.

Original project is done by Dennis van Weeren, unfortunately his original project homepage is
dead for a long time.  
Anyway Dennis if you ever read this...  
Congrats great work, this is a base for all other similar FPGA based projects that surfaced 
later so I consider you father for all of them!

## Repo structure
- ARM contains arm MCU controller sources
- BOOT68K is FPGA boot loader for kickstart embedded in FPGA
- FPGA contains fpga (XC3S400-4PQ208C) core sources 
- PIC contains pic (18F252) MCU controller sources
- PIC_GL contains my version of pic (18F252) MCU controller sources

There are no projects for building, that is something to do in order to make this repo usable.

## References
- https://en.wikipedia.org/wiki/Minimig
- https://code.google.com/archive/p/minimig/ (original code repo)
- https://www.techtravels.org/wp-content/uploads/pefiles/minimig/weeren001/home.html (original project mirror)
- http://www.etc.ugal.ro/cchiculita/software/picbootloader.htm (boot loader used for PIC flashing)
