#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#ifdef NOGLEW
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/glew.h>
#endif
#include <GL/glut.h>
/* Using GLM for our transformation matrix */
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
/* Using FreeType 2 for rendering fonts */
#include <ft2build.h>
#include FT_FREETYPE_H

GLuint program;
GLint attribute_coord;
GLint uniform_tex;
GLint uniform_color;

struct point {
	GLfloat x;
	GLfloat y;
	GLfloat s;
	GLfloat t;
};

GLuint vbo;

FT_Library ft;
FT_Face face;

const char *fontfilename;

/**
 * Store all the file's contents in memory, useful to pass shaders
 * source code to OpenGL
 */
char* file_read(const char* filename)
{
	FILE* in = fopen(filename, "rb");
	if (in == NULL) return NULL;

	int res_size = BUFSIZ;
	char* res = (char*)malloc(res_size);
	int nb_read_total = 0;

	while (!feof(in) && !ferror(in)) {
		if (nb_read_total + BUFSIZ > res_size) {
			if (res_size > 10*1024*1024) break;
			res_size = res_size * 2;
			res = (char*)realloc(res, res_size);
		}
		char* p_res = res + nb_read_total;
		nb_read_total += fread(p_res, 1, BUFSIZ, in);
	}
	
	fclose(in);
	res = (char*)realloc(res, nb_read_total + 1);
	res[nb_read_total] = '\0';
	return res;
}

/**
 * Display compilation errors from the OpenGL shader compiler
 */
void print_log(GLuint object)
{
	GLint log_length = 0;
	if (glIsShader(object))
		glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else if (glIsProgram(object))
		glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else {
		fprintf(stderr, "printlog: Not a shader or a program\n");
		return;
	}

	char* log = (char*)malloc(log_length);

	if (glIsShader(object))
		glGetShaderInfoLog(object, log_length, NULL, log);
	else if (glIsProgram(object))
		glGetProgramInfoLog(object, log_length, NULL, log);

	fprintf(stderr, "%s", log);
	free(log);
}

/**
 * Compile the shader from file 'filename', with error handling
 */
GLuint create_shader(const char* filename, GLenum type)
{
	const GLchar* source = file_read(filename);
	if (source == NULL) {
		fprintf(stderr, "Error opening %s: ", filename); perror("");
		return 0;
	}
	GLuint res = glCreateShader(type);
	glShaderSource(res, 1, &source, NULL);
	free((void*)source);

	glCompileShader(res);
	GLint compile_ok = GL_FALSE;
	glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
	if (compile_ok == GL_FALSE) {
		fprintf(stderr, "%s:", filename);
		print_log(res);
		glDeleteShader(res);
		return 0;
	}

	return res;
}

/**
 * The atlas struct holds a texture that contains the visible US-ASCII characters
 * of a certain font rendered with a certain character height.
 * It also contains an array that contains all the information necessary to
 * generate the appropriate vertex and texture coordinates for each character.
 *
 * After the constructor is run, you don't need to use any FreeType functions anymore.
 */
struct atlas {
	GLuint tex;    // texture object

	float w; // width of texture in pixels
	float h; // height of texture in pixels

	struct {
		float ax; // advance.x
		float ay; // advance.y

		float bw; // bitmap.width;
		float bh; // bitmap.height;

		float bl; // bitmap_left;
		float bt; // bitmap_top;

		float tx; // x offset of glyph in texture coordinates
	} c[128]; // character information

	atlas(FT_Face face, int height) {
		FT_Set_Pixel_Sizes(face, 0, height);
		FT_GlyphSlot g = face->glyph;

		int minw = 0;
		int minh = 0;

		memset(c, 0, sizeof c);

		/* Find minimum size for a texture holding all visible ASCII characters */
		for(int i = 32; i < 128; i++) {
			if(FT_Load_Char(face, i, FT_LOAD_RENDER)) {
				fprintf(stderr, "Loading character %c failed!\n", i);
				continue;
			}
			minw += g->bitmap.width + 1;
			minh = std::max(minh, g->bitmap.rows);
		}

		w = minw;
		h = minh;

		/* Create a texture that will be used to hold all ASCII glyphs */
		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniform1i(uniform_tex, 0);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, minw, minh, 0, GL_ALPHA, GL_UNSIGNED_BYTE, 0);

		/* We require 1 byte alignment when uploading texture data */
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		/* Clamping to edges is important to prevent artifacts when scaling */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		/* Linear filtering usually looks best for text */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		/* Paste all glyph bitmaps into the texture, remembering the offset */
		int o = 0;

		for(int i = 32; i < 128; i++) {
			if(FT_Load_Char(face, i, FT_LOAD_RENDER)) {
				fprintf(stderr, "Loading character %c failed!\n", i);
				continue;
			}

			glTexSubImage2D(GL_TEXTURE_2D, 0, o, 0, g->bitmap.width, g->bitmap.rows, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap.buffer);
			c[i].ax = g->advance.x >> 6;
			c[i].ay = g->advance.y >> 6;

			c[i].bw = g->bitmap.width;
			c[i].bh = g->bitmap.rows;

			c[i].bl = g->bitmap_left;
			c[i].bt = g->bitmap_top;

			c[i].tx = o / w;

			o += g->bitmap.width + 1;
		}

		glGenerateMipmap(GL_TEXTURE_2D);
		fprintf(stderr, "Generated a %d x %d (%d kb) texture atlas\n", minw, minh, minw * minh / 1024);
	}

	~atlas() {
		glDeleteTextures(1, &tex);
	}
};

atlas *a48;
atlas *a24;
atlas *a12;

int init_resources()
{
	/* Initialize the FreeType2 library */
	if(FT_Init_FreeType(&ft)) {
		fprintf(stderr, "Could not init freetype library\n");
		return 0;
	}

	/* Load a font */
	if(FT_New_Face(ft, fontfilename, 0, &face)) {
		fprintf(stderr, "Could not open font %s\n", fontfilename);
		return 0;
	}

	/* Create texture atlasses for several font sizes */
	a48 = new atlas(face, 48);
	a24 = new atlas(face, 24);
	a12 = new atlas(face, 12);

	/* Compile and link the shader program */
	GLint link_ok = GL_FALSE;

	GLuint vs, fs;
	if ((vs = create_shader("text.v.glsl", GL_VERTEX_SHADER))	 == 0) return 0;
	if ((fs = create_shader("text.f.glsl", GL_FRAGMENT_SHADER)) == 0) return 0;

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
	if (!link_ok) {
		fprintf(stderr, "glLinkProgram:");
		print_log(program);
		return 0;
	}

	const char* attribute_name;
	attribute_name = "coord";
	attribute_coord = glGetAttribLocation(program, attribute_name);
	if (attribute_coord == -1) {
		fprintf(stderr, "Could not bind attribute %s\n", attribute_name);
		return 0;
	}

	const char* uniform_name;
	uniform_name = "tex";
	uniform_tex = glGetUniformLocation(program, uniform_name);
	if (uniform_tex == -1) {
		fprintf(stderr, "Could not bind uniform %s\n", uniform_name);
		return 0;
	}

	uniform_name = "color";
	uniform_color = glGetUniformLocation(program, uniform_name);
	if (uniform_color == -1) {
		fprintf(stderr, "Could not bind uniform %s\n", uniform_name);
		return 0;
	}

	// Create the vertex buffer object
	glGenBuffers(1, &vbo);

	return 1;
}

/**
 * Render text using the currently loaded font and currently set font size.
 * Rendering starts at coordinates (x, y), z is always 0.
 * The pixel coordinates that the FreeType2 library uses are scaled by (sx, sy).
 */
void render_text(const char *text, atlas *a, float x, float y, float sx, float sy) {
	const char *p;

	/* Use the texture containing the atlas */
	glBindTexture(GL_TEXTURE_2D, a->tex);
	glUniform1i(uniform_tex, 0);

	/* Set up the VBO for our vertex data */
	glEnableVertexAttribArray(attribute_coord);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

	point coords[6 * strlen(text)];
	int c = 0;

	/* Loop through all characters */
	for(p = text; *p; p++) {
		/* Calculate the vertex and texture coordinates */
		float x2 =  x + a->c[*p].bl * sx;
		float y2 = -y - a->c[*p].bt * sy;
		float w = a->c[*p].bw * sx;
		float h = a->c[*p].bh * sy;

		/* Advance the cursor to the start of the next character */
		x += a->c[*p].ax * sx;
		y += a->c[*p].ay * sy;

		/* Skip glyphs that have no pixels */
		if(!w || !h)
			continue;

		coords[c++] = (point){x2,     -y2    , a->c[*p].tx,                      0};
		coords[c++] = (point){x2 + w, -y2    , a->c[*p].tx + a->c[*p].bw / a->w, 0};
		coords[c++] = (point){x2,     -y2 - h, a->c[*p].tx,                      a->c[*p].bh / a->h};
		coords[c++] = (point){x2 + w, -y2    , a->c[*p].tx + a->c[*p].bw / a->w, 0};
		coords[c++] = (point){x2,     -y2 - h, a->c[*p].tx,                      a->c[*p].bh / a->h};
		coords[c++] = (point){x2 + w, -y2 - h, a->c[*p].tx + a->c[*p].bw / a->w, a->c[*p].bh / a->h};
	}

	/* Draw all the character on the screen in one go */
	glBufferData(GL_ARRAY_BUFFER, sizeof coords, coords, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, c);

	glDisableVertexAttribArray(attribute_coord);
}

void display()
{
	float sx = 2.0 / glutGet(GLUT_WINDOW_WIDTH);
	float sy = 2.0 / glutGet(GLUT_WINDOW_HEIGHT);

	glUseProgram(program);

	/* White background */
	glClearColor(1, 1, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Enable blending, necessary for our alpha texture */
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GLfloat black[4] = {0, 0, 0, 1};
	GLfloat red[4] = {1, 0, 0, 1};
	GLfloat transparent_green[4] = {0, 1, 0, 0.5};

	/* Set color to black */
	glUniform4fv(uniform_color, 1, black);

	/* Effects of alignment */
	render_text("The Quick Brown Fox Jumps Over The Lazy Dog",          a48, -1 + 8 * sx,   1 - 50 * sy,    sx, sy);
	render_text("The Misaligned Fox Jumps Over The Lazy Dog",           a48, -1 + 8.5 * sx, 1 - 100.5 * sy, sx, sy);

	/* Scaling the texture versus changing the font size */
	render_text("The Small Texture Scaled Fox Jumps Over The Lazy Dog", a48, -1 + 8 * sx,   1 - 175 * sy,   sx * 0.5, sy * 0.5);
	render_text("The Small Font Sized Fox Jumps Over The Lazy Dog",     a24, -1 + 8 * sx,   1 - 200 * sy,   sx, sy);
	render_text("The Tiny Texture Scaled Fox Jumps Over The Lazy Dog",  a48, -1 + 8 * sx,   1 - 235 * sy,   sx * 0.25, sy * 0.25);
	render_text("The Tiny Font Sized Fox Jumps Over The Lazy Dog",      a12, -1 + 8 * sx,   1 - 250 * sy,   sx, sy);

	/* Colors and transparency */
	render_text("The Solid Black Fox Jumps Over The Lazy Dog",          a48, -1 + 8 * sx,   1 - 430 * sy,   sx, sy);

	glUniform4fv(uniform_color, 1, red);
	render_text("The Solid Red Fox Jumps Over The Lazy Dog",            a48, -1 + 8 * sx,   1 - 330 * sy,   sx, sy);
	render_text("The Solid Red Fox Jumps Over The Lazy Dog",            a48, -1 + 28 * sx,  1 - 450 * sy,   sx, sy);

	glUniform4fv(uniform_color, 1, transparent_green);
	render_text("The Transparent Green Fox Jumps Over The Lazy Dog",    a48, -1 + 8 * sx,   1 - 380 * sy,   sx, sy);
	render_text("The Transparent Green Fox Jumps Over The Lazy Dog",    a48, -1 + 18 * sx,  1 - 440 * sy,   sx, sy);

	glutSwapBuffers();
}

void free_resources()
{
	glDeleteProgram(program);
}

int main(int argc, char* argv[]) {
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA|GLUT_ALPHA|GLUT_DOUBLE);
	glutInitWindowSize(640, 480);
	glutCreateWindow("Texture atlas text");

	if(argc > 1)
		fontfilename = argv[1];
	else
		fontfilename = "../font/FreeSans.ttf";

#ifndef NOGLEW
	GLenum glew_status = glewInit();
	if (GLEW_OK != glew_status) {
		fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
		return 1;
	}

	if (!GLEW_VERSION_2_0) {
		fprintf(stderr, "No support for OpenGL 2.0 found\n");
		return 1;
	}
#endif

	if (init_resources()) {
		glutDisplayFunc(display);
		glutMainLoop();
	}

	free_resources();
	return 0;
}
