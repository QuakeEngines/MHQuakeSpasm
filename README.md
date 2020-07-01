# MHQuakeSpasm
## MH fork of latest QuakeSpasm

This is my personal fork of QuakeSpasm.  Whereas back in 2010 it made sense to have a high-performance Direct3D engine, the problems with OpenGL from that time no longer exist.  OpenGL drivers are now available by default on all Windows machines, the version supported is sufficiently high that you can use a reasonable API baseline, and driver stability is massively improved.

It no longer makes sense to have a Direct3D engine as a solution to those problems, and these days I suggest that you're just better off using QuakeSpasm for everything.

However, there are - in my opinion - certain serious issues with the QuakeSpasm renderer that stem from it's origins in the OpenGL 1.0/1.1 based FitzQuake.  At that API level hardware features that implement the Quake renderer in high quality and with high performance are just not available, and so FitzQuake often took the path of brute-forcing in software.  True, it's an excellent quality implementation in those terms, but it's not what I want.

The primary purpose of this fork is to take this renderer and lift it up to a higher-quality and higher-performance implementation.  QuakeSpasm itself already takes steps in that direction with it's lightmapped surface and alias model paths, so this is just a logical evolution towards the rest of the way.  At the same time the various legacy codepaths will be removed to clean up and simplify the renderer.

The intent is feature-parity with QuakeSpasm, but it is a work in progress.  At times some features may be removed to allow for simplification of the critical codepaths, with the removed features then being brought back in once the key work is complete.  This temporary removal is not and should not be interpreted as meaning that the removed feature is permanently gone.

## FAQ

### What API level does this use?
MHQuakeSpasm is built around an approximate OpenGL 1.5 level; in it's current incarnation the following OpenGL extensions are mandatory:
 - GL_ARB_multitexture
 - GL_ARB_vertex_buffer_object
 - GL_ARB_vertex_program
 - GL_ARB_fragment_program

This is roughly equivalent to what (the original, not BFG) Doom 3 uses, so as a general rule if you can run Doom 3, you can probably run this too. Bear in mind that Doom 3 is now a 16-year-old game so what it requires should no longer be considered high-end. 

You should NOT misinterpret the above as indicating that this will have stencil shadows and normal maps. 

### Can you switch off interpolation?
Yes.

### GL_ARB_vertex_program?  What about GLSL?
The GLSL C interface at this API level sucks.  The main problems with it are:
 - No separate shaders, vertex and fragment shaders need to be linked to a single program object.
 - No standalone shared uniforms, uniforms are part of per-program state.
 - Everything needs to be queried rather than explicitly set.
 
True, most of these problems are solved in newer GL versions, but that would entail reaching beyond the API level I'm targetting.  ARB assembly programs, on the other hand, don't have these problems at all (which does make the claimed advantages of GLSL seem more like an attempt to retroactively justify a bad initial design).  While they do have some shortcomings (the ones most relevant to me being lack of the texGrad instruction and no array texture support) on balance they are just a more productive tool.

### Where's r_drawflat? Your engine sucks! I hate you forever!
That's OK.

More seriously, if you need functionality like r_drawflat, r_fullbright, r_showtris, etc, then I'd suggest that just using QuakeSpasm instead may be a more suitable option for you.  As part of a code simplification pass these features were removed, and while it wouldn't necessarily be a huge imposition to add them back, there's likewise no real requirement to do so either.

### How do you compile this?
On Windows, MHQuakeSpasm is a Visual Studio 2019 project.  It has been built and tested with a stock Visual Studio 2019 Community Edition installation, with no other tools or components being required.  So the build procedure is:
 - Download and install Visual Studio 2019 Community.
 - Download the source code.
 - Build it.

### What about Linux?  Mac OSX?  Other platforms?
I consider the Visual Studio project to be the "master" version and I haven't bothered keeping makefiles/etc for other platforms up to date.  The only real change made to the project was the addition of the GLEW header and source files; aside from that I haven't knowingly done anything to explicitly break a build on any other platform, so you should be able to manually edit the makefiles and get a build.  To be honest, if you can't do this kind of thing yourself, you probably have no business building on Linux anyway.
