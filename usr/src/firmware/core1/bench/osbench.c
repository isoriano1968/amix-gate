/*
 * osbench.c -- phase 0 of the core 1 GPU plan: how long Mesa 18.3 takes per
 * frame, rendering offscreen through OSMesa (softpipe).  The same scenes run
 * on the PC, on the 060 (GALLIUM_NOOP=1 measures Mesa without drawing) and
 * on core 1.
 *
 * Scenes:
 *   gears  the classic gears in display lists, lit, 3 gears
 *   q3     an ioquake3-like load: TRIS textured triangles per frame from
 *          vertex arrays (glDrawElements), a second lightmap pass blended
 *          with GL_DST_COLOR/GL_ZERO, STATES texture/blend changes
 *
 * The platform supplies osb_now_us() and osb_print(); osb_run() returns the
 * average ms per frame, and osb_checksum() a sum of the last frame's pixels
 * so the targets can be compared.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "osbench.h"

static GLuint gear_list[3];
static float angle;

/* from the gears demo (Brian Paul) */
static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth)
{
    GLint i;
    GLfloat r0 = inner_radius, r1 = outer_radius - tooth_depth / 2.0f, r2 = outer_radius + tooth_depth / 2.0f;
    GLfloat a, da = 2.0f * (float)M_PI / teeth / 4.0f, u, v, len;

    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        if (i < teeth) {
            glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
            glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
        }
    }
    glEnd();
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
    }
    glEnd();
    glNormal3f(0.0f, 0.0f, -1.0f);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
        if (i < teeth) {
            glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
            glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
        }
    }
    glEnd();
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), -width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
    }
    glEnd();
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
        u = r2 * cosf(a + da) - r1 * cosf(a);
        v = r2 * sinf(a + da) - r1 * sinf(a);
        len = sqrtf(u * u + v * v);
        glNormal3f(v / len, -u / len, 0.0f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), -width * 0.5f);
        glNormal3f(cosf(a), sinf(a), 0.0f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), -width * 0.5f);
        u = r1 * cosf(a + 3 * da) - r2 * cosf(a + 2 * da);
        v = r1 * sinf(a + 3 * da) - r2 * sinf(a + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
        glNormal3f(cosf(a), sinf(a), 0.0f);
    }
    glVertex3f(r1 * cosf(0), r1 * sinf(0), width * 0.5f);
    glVertex3f(r1 * cosf(0), r1 * sinf(0), -width * 0.5f);
    glEnd();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * (float)M_PI / teeth;
        glNormal3f(-cosf(a), -sinf(a), 0.0f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
    }
    glEnd();
}

static void gears_init(int w, int h)
{
    static const GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
    static const GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f };
    static const GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
    static const GLfloat blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };
    GLfloat r = (GLfloat)h / (GLfloat)w;

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    gear_list[0] = glGenLists(3);
    gear_list[1] = gear_list[0] + 1;
    gear_list[2] = gear_list[0] + 2;
    glNewList(gear_list[0], GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();
    glNewList(gear_list[1], GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();
    glNewList(gear_list[2], GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();
    glEnable(GL_NORMALIZE);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -r, r, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
}

static void gears_frame(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glCallList(gear_list[0]);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear_list[1]);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear_list[2]);
    glPopMatrix();
    glPopMatrix();
    angle += 2.0f;
}

/* q3: a grid of quads in front of the camera, split into STATES batches,
   each batch with its own texture, drawn twice (texture, then lightmap) */
#define Q3_TEX      8
#define Q3_STATES   64
static GLuint q3_tex[Q3_TEX], q3_lm;
static GLfloat *q3_xyz, *q3_st, *q3_lst;
static GLubyte *q3_rgba;
static GLushort *q3_idx;
static int q3_tris;

static void q3_init(int w, int h)
{
    static GLubyte img[64 * 64 * 4];
    int i, x, y, gw, n = 0, v;
    GLfloat r = (GLfloat)h / (GLfloat)w;

    glGenTextures(Q3_TEX, q3_tex);
    for (i = 0; i < Q3_TEX; i++) {
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                GLubyte *p = img + (y * 64 + x) * 4;
                p[0] = (GLubyte)(x * 4 + i * 30);
                p[1] = (GLubyte)(((x ^ y) & 8) ? 200 : 60);
                p[2] = (GLubyte)(y * 4);
                p[3] = 255;
            }
        glBindTexture(GL_TEXTURE_2D, q3_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    }
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            GLubyte *p = img + (y * 64 + x) * 4;
            p[0] = p[1] = p[2] = (GLubyte)(128 + 127 * sinf(x * 0.2f) * cosf(y * 0.15f));
            p[3] = 255;
        }
    glGenTextures(1, &q3_lm);
    glBindTexture(GL_TEXTURE_2D, q3_lm);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);

    /* gw x gw quads = 2 gw^2 triangles */
    for (gw = 1; 2 * (gw + 1) * (gw + 1) <= osb_q3_tris; gw++)
        ;
    q3_tris = 2 * gw * gw;
    q3_xyz = malloc((gw + 1) * (gw + 1) * 3 * sizeof(GLfloat));
    q3_st = malloc((gw + 1) * (gw + 1) * 2 * sizeof(GLfloat));
    q3_lst = malloc((gw + 1) * (gw + 1) * 2 * sizeof(GLfloat));
    q3_rgba = malloc((gw + 1) * (gw + 1) * 4);
    q3_idx = malloc(q3_tris * 3 * sizeof(GLushort));
    for (y = 0; y <= gw; y++)
        for (x = 0; x <= gw; x++) {
            v = y * (gw + 1) + x;
            q3_xyz[v * 3] = -1.0f + 2.0f * x / gw;
            q3_xyz[v * 3 + 1] = -1.0f + 2.0f * y / gw;
            q3_xyz[v * 3 + 2] = 0.1f * sinf(x * 0.7f + y * 0.3f);
            q3_st[v * 2] = x * 0.25f;
            q3_st[v * 2 + 1] = y * 0.25f;
            q3_lst[v * 2] = (float)x / gw;
            q3_lst[v * 2 + 1] = (float)y / gw;
            q3_rgba[v * 4] = q3_rgba[v * 4 + 1] = q3_rgba[v * 4 + 2] = q3_rgba[v * 4 + 3] = 255;
        }
    for (y = 0; y < gw; y++)
        for (x = 0; x < gw; x++) {
            v = y * (gw + 1) + x;
            q3_idx[n++] = (GLushort)v; q3_idx[n++] = (GLushort)(v + 1); q3_idx[n++] = (GLushort)(v + gw + 1);
            q3_idx[n++] = (GLushort)(v + 1); q3_idx[n++] = (GLushort)(v + gw + 2); q3_idx[n++] = (GLushort)(v + gw + 1);
        }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -r, r, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -1.2f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, q3_xyz);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, q3_rgba);
}

static void q3_frame(void)
{
    int b, per = (q3_tris / Q3_STATES) * 3;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(angle * 0.2f, 0.0f, 0.0f, 1.0f);
    for (b = 0; b < Q3_STATES; b++) {
        const GLushort *idx = q3_idx + b * per;

        glDisable(GL_BLEND);
        glDepthFunc(GL_LEQUAL);
        glBindTexture(GL_TEXTURE_2D, q3_tex[b % Q3_TEX]);
        glTexCoordPointer(2, GL_FLOAT, 0, q3_st);
        glDrawElements(GL_TRIANGLES, per, GL_UNSIGNED_SHORT, idx);
        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        glDepthFunc(GL_EQUAL);
        glBindTexture(GL_TEXTURE_2D, q3_lm);
        glTexCoordPointer(2, GL_FLOAT, 0, q3_lst);
        glDrawElements(GL_TRIANGLES, per, GL_UNSIGNED_SHORT, idx);
    }
    glPopMatrix();
    angle += 2.0f;
}

/* fluid: a GLSL fragment shader on a quad over the whole picture (domain
   warping: the coordinates bent by sines of each other, then coloured) */
static GLuint fluid_prog;
static GLint fluid_t;

static const char *fluid_vs =
    "varying vec2 p;\n"
    "void main() {\n"
    "    p = gl_Vertex.xy;\n"
    "    gl_Position = gl_Vertex;\n"
    "}\n";

static const char *fluid_fs =
    "uniform float t;\n"
    "varying vec2 p;\n"
    "void main() {\n"
    "    vec2 q = p * vec2(3.0, 2.25);\n"
    "    for (int i = 1; i < 5; i++) {\n"
    "        float f = float(i);\n"
    "        q += vec2(0.7 / f * sin(f * q.y + t + 0.3 * f),\n"
    "                  0.5 / f * sin(f * q.x + 0.8 * t + 0.3 * f + 1.6));\n"
    "    }\n"
    "    vec3 c = 0.5 + 0.5 * sin(vec3(q.x, q.y, q.x + q.y) * 0.9 + vec3(0.0, 2.0, 4.0) + 0.5 * t);\n"
    "    gl_FragColor = vec4(c * c, 1.0);\n"
    "}\n";

static GLuint fluid_shader(GLenum type, const char *src, char *err)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, 120, NULL, err);
        return 0;
    }
    return s;
}

static int fluid_init(int w, int h, char *err)
{
    GLuint vs = fluid_shader(GL_VERTEX_SHADER, fluid_vs, err), fs;
    GLint ok = 0;

    if (!vs || !(fs = fluid_shader(GL_FRAGMENT_SHADER, fluid_fs, err)))
        return -1;
    fluid_prog = glCreateProgram();
    glAttachShader(fluid_prog, vs);
    glAttachShader(fluid_prog, fs);
    glLinkProgram(fluid_prog);
    glGetProgramiv(fluid_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(fluid_prog, 120, NULL, err);
        return -1;
    }
    glUseProgram(fluid_prog);
    fluid_t = glGetUniformLocation(fluid_prog, "t");
    glViewport(0, 0, w, h);
    return 0;
}

static void fluid_frame(void)
{
    glUniform1f(fluid_t, angle * 0.02f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    angle += 2.0f;
}

/* q3mt: the q3 grid in one pass, texture and lightmap as two texture units
   (GL_MODULATE), as ioquake3's GL1 renderer draws with multitexture */
static void q3mt_frame(void)
{
    int b, per = (q3_tris / Q3_STATES) * 3;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(angle * 0.2f, 0.0f, 0.0f, 1.0f);
    for (b = 0; b < Q3_STATES; b++) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, q3_tex[b % Q3_TEX]);
        glDrawElements(GL_TRIANGLES, per, GL_UNSIGNED_SHORT, q3_idx + b * per);
    }
    glPopMatrix();
    angle += 2.0f;
}

static void q3mt_init(int w, int h)
{
    q3_init(w, h);
    glDepthFunc(GL_LEQUAL);
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, q3_lm);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, q3_lst);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    glTexCoordPointer(2, GL_FLOAT, 0, q3_st);
}

int osb_q3_tris = 8000;
void (*osb_frame_hook)(const void *buffer, int w, int h);

double osb_run(enum osb_scene scene, void *buffer, int w, int h, int frames, char *err)
{
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    unsigned long long t0;
    int i;

    if (!ctx) {
        strcpy(err, "OSMesaCreateContextExt failed");
        return -1.0;
    }
    if (!OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, w, h)) {
        strcpy(err, "OSMesaMakeCurrent failed");
        return -1.0;
    }
    angle = 0.0f;
    if (scene == OSB_GEARS)
        gears_init(w, h);
    else if (scene == OSB_Q3)
        q3_init(w, h);
    else if (scene == OSB_Q3MT)
        q3mt_init(w, h);
    else if (fluid_init(w, h, err) < 0) {
        OSMesaDestroyContext(ctx);
        return -1.0;
    }
    /* one frame outside the timing: first use compiles shaders/variants */
    if (scene == OSB_GEARS)
        gears_frame();
    else if (scene == OSB_Q3)
        q3_frame();
    else if (scene == OSB_Q3MT)
        q3mt_frame();
    else
        fluid_frame();
    glFinish();
    t0 = osb_now_us();
    for (i = 0; i < frames; i++) {
        if (scene == OSB_GEARS)
            gears_frame();
        else if (scene == OSB_Q3)
            q3_frame();
        else if (scene == OSB_Q3MT)
            q3mt_frame();
        else
            fluid_frame();
        glFinish();
        if (osb_frame_hook)
            osb_frame_hook(buffer, w, h);
    }
    t0 = osb_now_us() - t0;
    OSMesaDestroyContext(ctx);
    return t0 / 1000.0 / frames;
}

unsigned long osb_checksum(const void *buffer, int w, int h)
{
    const unsigned char *p = buffer;
    unsigned long s = 0;
    long i;

    for (i = 0; i < (long)w * h * 4; i++)
        s = s * 31 + p[i];
    return s;
}
