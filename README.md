
Date:		2019-09-02
Version: 	0.65
Built from Tyr-Quake, by Kevin Shanahan 
Ported to Serenity OS by Jesse Buhagiar [quaker762]

Author:		Kevin Shanahan (aka. Tyrann)

Webpage:	http://disenchant.net

email:		kevin@shanahan.id.au

Why?
----
At this point, it's just to exercise my brain with some low-level programming.
I want this to be a version of Quake that basically looks like the Quake of
old, but one that works on current operating systems.  Some enhancements may
be added, as long as they don't completely change the look and feel of the
original game.

Building:
---------
Build like a regular Serenity port by typing `./package` in the `quake` folder. 
There are a few flags you can turn on/specify via `makeopts`.

`SYMBOLS_ON`: Y or N, specifies whether or not debugging symbols will be stripped from the binary on a build
`USE_X86_ASM`: This currently doesn't work (as it causes a linker error/undefined references)
