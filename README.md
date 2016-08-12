OpenGL source examples from the OpenGL Programming wikibook:

  http://en.wikibooks.org/wiki/OpenGL_Programming


# Installation on OSX


### GLUT

On OSX, `GLUT` framework is already available,
located at `/System/Library/Frameworks/GLUT.framework`.

Note that many of the functions are now deprecated by Apple,
and these functions can cause compile warnings, and also
not show up in TAGS file.

Still, `man` pages are available for most of the functions
(i.e. `man glutInit`)


### GLEW

`GLEW` needs to be installed. So do `brew install glew`.
This installs the header files in `/usr/local/include/GL/`

To add a TAGS file for it:
```
cd /System/Library/Frameworks/GLUT.framework;
sudo ctags -e -R
```

### Modifying the Makefiles

1. Use `LDLIBS=-framework GLUT -framework OpenGL -lGLEW`

2. To avoid deprecated glut warnings `CPPFLAGS=-Wno-deprecated-declarations -g`


### Modifying the code

1. Change `GL/freeglut.h` to `GLUT/glut.h`

2. Comment out unnavilable freeglut functions (ie `glutInitContextVersion`)
