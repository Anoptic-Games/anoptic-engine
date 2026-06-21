# ``include`` Directory

The `include/` directory contains the public header files for the Anoptic Game Engine. These header files define the public API that games will use to interact with the engine. Public API, in Anoptic Engine, means that these are meant to be FUNCTION SIGNATURES and the TYPE DEFINITIONS those function signatures (and ONLY the ones those function signatures) depend on. Absolutely 0 implementation details should be in an include/anoptic_xxx.h file, as doing so may break delicate interactions between modules and can also be platform-dependent.  

The IMPLEMENTATIONS of these Signatures and Definitions, that is to say, the specific platform-specific instructions, particular memory allocation strategies, particular data structures employed in intermediary steps, etc, are all in `src/{subdirectory-matching-the-module-name}/`. For instance: `include/anoptic_time.h` has its platform-specific implementations in `src/time/`, and serves as a good example of how platform differences are handled.

Functions in an `include/`  header always begin with the prefix `ano_` to distinguish them from functions internal to a source file, as well as from any imported libraries. An `ano_` prefix makes it immediately evident that:
1.  It is probably a public interface function, that is to say the surface of an engine module.
2.  It is written by and sanctioned by us, it is anoptische (the adjective form of anoptic).

## Directory Structure

Here are some canonical examples of how these work.
Current layout, flat:
```plaintext
include/
├── anoptic_memory.h    # Public memory allocation API
├── anoptic_threads.h   # Platform abstraction of pthread API
├── ...                 # other APIs
└── anoptic_time.h      # Public timekeeping API
```

Typical layout, mature (later):
```plaintext
include/
├── graphics/       # Public graphics API
├── audio/          # Public audio API
├── physics/        # Public physics API
├── input/          # Public input API
├── scripting/      # Public scripting API
└── utils/          # Public utilities API
```

# Cautionary Tales
Pyrus — 18:18
:ungroyperous:
Image
ssa — 18:18
>CONSUMER only
?!?!?!
Why??
Pyrus — 18:18
You're missing the part
where he implemented something in an include/ .h
ssa — 18:18
Nope
That was intentional
Pyrus — 18:19
Which I have repeated 100000x is only meant to be an interface.
Pyrus — 18:19
It's wrong.
ssa — 18:19
You have said it once after I already did it.
Pyrus — 18:19
I've been saying it this whole time since I first explained it
in like
2022
Anyways I guess I'll go and fix it myself.
Again.
ssa — 18:20
It's not a big header, we can freeze and specify
Pyrus — 18:20
All of this is vibe coded fucking garbage where you just ignore everything I've ever told you.
ssa — 18:20
..?
Pyrus — 18:20
Why the FUCK
Would I want an inline implementation of anything in the interface file
ssa — 18:20
True.
Should definitely be a .c 
Pyrus — 18:21
any .h in includes/ is just what other places call.
ssa — 18:21
There is also an src/render_bridge 
Pyrus — 18:21
Honestly the way you were doing it before was better, your own little ghetto in src/
That was fine.
Pyrus — 18:22
Yeah but Claude had to go and implement things in the header file of includes/
ssa — 18:22
{contents of }
Why didn't it put it here..?
Pyrus — 18:22
Is my documentation all just deleted in some lost commit somewhere? What the fuck.
I coulda sworn I had a docs/ that was more fleshed out
Wait here it is
(Out of Date -- To be Updated)
Include Directory
The include/ directory contains the public header files for the Anoptic Game Engine. These header files define the public API that games will use to interact with the engine.

Directory Structure
Typical layout:
include/
├── graphics/       # Public graphics API
├── audio/          # Public audio API
├── physics/        # Public physics API
├── input/          # Public input API
├── scripting/      # Public scripting API
└── utils/          # Public utilities API
Yeah so it just ignored all this shit
PUBLIC API
Not implementation.
Anyways I guess I will have to rewrite that and tell Claude to always read .md's it comes across before doing anything in a directory.
That ougtta fix it.
This too.
Image
Again either you or Claude or both completely misunderstood what an interface is.
Why would an interface (a handle, nothing more than a signature with NO function body) need to include malloc.
That's an implementation detail.
That goes in src/.
include/ is only for function signatures so that other parts of the code know what it can call, and have a reasonable expectation as to inputs/outputs.
That is IT.
The insides are always meant to be src/
ssa — 18:24
mhm
Tell that to Claude
Actually that should be in CLAUDE.md
Which gets injected into its context automatically.
my b for not catching it though
Pyrus — 18:25
The Claude Code harness has some kind of thing going on where it is only aware of files it has loaded up to look at.
So
CLAUDE.md is indeed the one that is force-injected.
However, I think it would be more reasonable
Any time you are in a directory, ALWAYS look for a .md and follow its instructions.
If there is no md, there is no wasted input tokens.
ssa — 18:26
Yep exactly
Pyrus — 18:26
And also now every directory can have its own mini .md
Well, only the ones that need it
>copy pasting this entire convo into include.md