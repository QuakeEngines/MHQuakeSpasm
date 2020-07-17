# mhQuakeSpasm
## MH fork of latest QuakeSpasm

This is my personal fork of QuakeSpasm.  Whereas back in 2010 it made sense to have a high-performance Direct3D engine, the problems with OpenGL from that time no longer exist.  OpenGL drivers are now available by default on all Windows machines, the version supported is sufficiently high that you can use a reasonable API baseline, and driver stability is massively improved.

It no longer makes sense to have a Direct3D engine as a solution to those problems, and these days I suggest that you're just better off using QuakeSpasm for everything.

However, there are - in my opinion - issues with the QuakeSpasm renderer that stem from it's origins in the OpenGL 1.0/1.1 based FitzQuake.  At that API level hardware features that implement the Quake renderer in high quality and with high performance are just not available, and so FitzQuake often took the path of brute-forcing in software.  True, it's an excellent quality implementation in those terms, but it's not what I want.

The primary purpose of this fork is to take this renderer and lift it up to a higher-quality and higher-performance implementation.  QuakeSpasm itself already takes steps in that direction with it's lightmapped surface and alias model paths, so this is just a logical evolution towards the rest of the way.  At the same time the various legacy codepaths will be removed to clean up and simplify the renderer.

The intent is broad (but not necessarily exact) feature-parity with QuakeSpasm, and it is a work in progress.  At times some features may be removed to allow for simplification of critical codepaths, with removed features then being brought back in once key work is complete.  This temporary removal does not mean and should not be misinterpreted as meaning that any removed feature is permanently gone.  Likewise there is no implicit promise that a removed feature will definitely be restored.

A secondary intent is to revert some (but not all) classic Quake behaviours which QuakeSpasm, and FitzQuake before it, changed. 

## FAQ

### What API level does this use?
mhQuakeSpasm is built around an approximate OpenGL 1.5 level; in it's current incarnation the following OpenGL extensions are mandatory:
 - GL_ARB_vertex_program
 - GL_ARB_fragment_program
 - One of GL_EXT_texture_edge_clamp or GL_SGIS_texture_edge_clamp or OpenGL 1.2
 - One of GL_ARB_texture_cube_map or GL_EXT_texture_cube_map or OpenGL 1.3
 - One of GL_ARB_multitexture (with a minimum of 6 texture units) or OpenGL 1.3
 - One of GL_ARB_vertex_buffer_object or OpenGL 1.5
 - One of GL_ARB_texture_rectangle or GL_EXT_texture_rectangle or GL_NV_texture_rectangle or OpenGL 3.1

This is roughly equivalent to what (the original, not BFG) Doom 3 uses, so as a general rule if you can run Doom 3, you can probably run this too. Bear in mind that Doom 3 is now a 16-year-old game so these requirements should no longer be considered high-end. 

You should NOT misinterpret the above as indicating that this will have stencil shadow volumes and normal maps. 

### Can you switch off interpolation?
Yes.

### GL_ARB_vertex_program?  What about GLSL?
The GLSL C interface at this API level sucks.  The main problems with it are:
 - No separate shaders, vertex and fragment shaders need to be linked to a single program object.
 - No standalone shared uniforms, uniforms are part of per-program state.
 - Everything needs to be queried rather than explicitly set.
 
True, most of these problems are solved in newer GL versions, but that would entail reaching beyond the API level I'm targetting.  ARB assembly programs, on the other hand, don't have these problems at all (which does make the claimed advantages of GLSL seem more like an attempt to retroactively justify a bad initial design).  While they do have other shortcomings (the ones most relevant to me being lack of the texGrad instruction and no array texture support) on balance they are just a more productive tool.

### Why fork QuakeSpasm instead of doing your own engine, like you did in the past?
At this stage in my life I've been round the block enough to know what I'm good at and what I'm not good at. There is a certain enjoyment to be had from working on some of the latter, but it's just not for something I'm going to put out publicly.  By taking an existing codebase and playing to my strengths with it, much of the heavy lifting with the boring parts has already been done for me, and I get to have more fun and be more productive with the rest of it.

I also get the ability to (hopefully) more easily integrate future changes made to QuakeSpasm, and - who knows?  Work I had done elsewhere in the past eventually ended up in QuakeSpasm, and gave back to the community.  Maybe some of this might too.

### Why did you change some FitzQuake/QuakeSpasm behaviours? 
A variety of reasons. 

Personal preference. For example, the crosshair image I use is something I've used for years, I like it, it's clean, it's simple, it fits the Quake aesthetic well enough. 

Reverting original engine behaviours. Not everything the original did was perfect, and some of it was flat-out wrong. Some of what Fitz/QS changed, should - however and in my opinion - not have been changed. 

In some cases I've gone back to Quake utility source code. So, for example, I reconstructed alias model bounding boxes and normals from modelgen.c, and my dynamic light formula is straight out of the light.exe source. The software Quake source code has also been used as an occasional authoritative reference. 

In one case I used an old .plan file entry from 1996 to determine the intent behind a strange-looking piece of code in the original sources. 

### Why did you replace certain Arcane Dimensions sprites with particles?
Performance.  The AD "particle system" is lovely, but it chugs badly on lower-end machines.  On one HP tablet I tested, doing this more than doubled framerates, and maps went from being almost unplayable to being quite playable.  I've tried to match the original sprites closely enough, and anyway, QSS replaces them as well, so I guess that makes it more acceptable.

At some stage I might go back and try some optimizations to sprite drawing that may make it possible to roll-back this replacement.

### Help! r_drawflat melted my GPU! 
Sorry about that. I haven't implemented r_drawflat in a robust or performant way, because it's not really intended to be a performant codepath to begin with. While it is useful to see how badly QBSP chopped up your geometry, with draw call batching that's really no longer something that badly affects performance. I guess you can switch it on if you just want to look at it, but otherwise it's value seems dubious. 

### Where's r_showtris? Your engine sucks! I hate you forever! 
That's OK. 

### Will you support Nehahra? 
Maybe. But a friendly word of advice: badgering me over and over again until I eventually do it just to shut you up won't endear you to me. 

### I want to use this engine as a server. 
Do not use this engine as a server. If you use this engine as a server Baby Jesus will stamp on puppies heads. Use something else instead. 

### How do you compile this?
On Windows, mhQuakeSpasm is a Visual Studio 2019 project.  It has been built and tested with a stock Visual Studio 2019 Community Edition installation, with no other tools or components being required.  So the build procedure is:
 - Download and install Visual Studio 2019 Community.
 - Download the source code.
 - Build it.

Github filters exclude the third-party .libs and .dlls you'll need to link and run, so I recommend you grab them from another copy of the QuakeSpasm source. These are located in the "Windows" folder of the source repository, and you should copy over everything aside from the "VisualStudio" folder.  If you do copy the "VisualStudio" folder you'll break the project files.

The SDL2 build should be used; the regular SDL build has been exercised very little, if at all, and I'll probably remove it from the project some time.

### What about Linux?  Mac OSX?  Other platforms?
I consider the Visual Studio project to be the "master" version and I haven't bothered keeping makefiles/etc for other platforms up to date.  The only real change made to the project was the addition of the GLEW header and source files; aside from that I haven't knowingly done anything to explicitly break a build on any other platform, so you should be able to manually edit the makefiles and get a build.  To be honest, if you can't do this kind of thing yourself, you probably have no business building on Linux anyway.

### Where's the OpenGL rant? This is not like you!
There's not much need to repeat what I've said before. The summary of my position as of 2020 goes something like this. OpenGL 1.5 with the vertex and fragment program extensions is a great API. OpenGL 4.6 core profile with all the AZDO stuff is a great (if unusual) API. Everything else is varying degrees of an inconsistent, poorly-featured (or under-featured) mess. 
