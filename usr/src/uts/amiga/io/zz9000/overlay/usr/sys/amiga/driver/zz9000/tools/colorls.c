/*
 * colorls.c - tiny ANSI-color ls for AMIX/ZZ9000 console testing.
 *
 * This is deliberately small and old-cc friendly.  It is not GNU ls.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define C_RESET "\033[0m"
#define C_DIR   "\033[34m"
#define C_EXEC  "\033[32m"
#define C_LINK  "\033[36m"
#define C_DEV   "\033[33m"

static int show_all;
static int long_format;
static char c_reset[16] = "\033[0m";
static char c_file[16] = "";
static char c_dir[16] = "\033[34m";
static char c_exec[16] = "\033[32m";
static char c_link[16] = "\033[36m";
static char c_dev[16] = "\033[33m";
static char c_sock[16] = "\033[35m";
static char c_fifo[16] = "\033[33m";

#define MAX_SUFFIX 32
struct suffix_color {
    char suffix[24];
    char color[16];
};
static struct suffix_color suffix_colors[MAX_SUFFIX];
static int nsuffix_colors;

static int isdotopt();
static void init_colors();
static void set_color();
static char *mode_color();
static char *suffix_color();
static void color_name();
static void list_path();
static void list_dir();
static void list_one();
static void long_one();
static void mode_string();
static void user_name();
static void group_name();
static void time_string();
static char *base_name();

int
main(argc, argv)
int argc;
char **argv;
{
    int i, paths;

    init_colors();
    paths = 0;
    for (i = 1; i < argc; ++i) {
        if (isdotopt(argv[i]))
            continue;
        ++paths;
    }

    if (paths == 0) {
        list_path(".", 0);
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (isdotopt(argv[i]))
            continue;
        if (paths > 1)
            printf("%s:\n", argv[i]);
        list_path(argv[i], paths > 1);
        if (paths > 1)
            printf("\n");
    }
    return 0;
}

static int
isdotopt(s)
char *s;
{
    char *p;

    if (s[0] != '-' || s[1] == '\0')
        return 0;
    for (p = s + 1; *p; ++p) {
        if (*p == 'a')
            show_all = 1;
        else if (*p == 'l')
            long_format = 1;
        else
            return 0;
    }
    return 1;
}

static void
init_colors()
{
    char *env, *p, *q;
    char token[64];
    int len;

    env = getenv("LS_COLORS");
    if (!env)
        env = getenv("ZZ9K_LS_COLORS");
    if (!env)
        return;

    p = env;
    while (*p) {
        q = strchr(p, ':');
        if (q)
            len = q - p;
        else
            len = strlen(p);
        if (len > 0 && len < sizeof token) {
            strncpy(token, p, len);
            token[len] = '\0';
            if (token[0] && token[1] && token[2] == '=')
                set_color(token, token + 3);
            else if (token[0] == '*' && token[1] == '.' && strchr(token, '='))
                set_color(token, strchr(token, '=') + 1);
        }
        if (!q)
            break;
        p = q + 1;
    }
}

static void
set_color(key, val)
char *key;
char *val;
{
    char buf[16];
    char *dst;

    if (key[0] == '*' && key[1] == '.') {
        char *eq;

        if (nsuffix_colors >= MAX_SUFFIX)
            return;
        eq = strchr(key, '=');
        if (!eq || eq - key - 1 >= sizeof suffix_colors[0].suffix)
            return;
        strncpy(suffix_colors[nsuffix_colors].suffix, key + 1, eq - key - 1);
        suffix_colors[nsuffix_colors].suffix[eq - key - 1] = '\0';
        dst = suffix_colors[nsuffix_colors].color;
        ++nsuffix_colors;
    } else if (strcmp(key, "di") == 0)
        dst = c_dir;
    else if (strcmp(key, "ex") == 0)
        dst = c_exec;
    else if (strcmp(key, "ln") == 0)
        dst = c_link;
    else if (strcmp(key, "ch") == 0 || strcmp(key, "bd") == 0)
        dst = c_dev;
#ifdef S_IFSOCK
    else if (strcmp(key, "so") == 0)
        dst = c_sock;
#endif
#ifdef S_IFIFO
    else if (strcmp(key, "pi") == 0)
        dst = c_fifo;
#endif
    else if (strcmp(key, "fi") == 0)
        dst = c_file;
    else
        return;

    if (val[0] == '\0' || strcmp(val, "0") == 0) {
        dst[0] = '\0';
        return;
    }
    if (strlen(val) > 10)
        return;
    strcpy(buf, "\033[");
    strcat(buf, val);
    strcat(buf, "m");
    strcpy(dst, buf);
}

static void
list_path(path, multi)
char *path;
int multi;
{
    struct stat st;

    if (lstat(path, &st) < 0) {
        perror(path);
        return;
    }
    if ((st.st_mode & S_IFMT) == S_IFDIR)
        list_dir(path);
    else {
        list_one(path, base_name(path));
        if (!multi || long_format)
            putchar('\n');
    }
}

static void
list_dir(path)
char *path;
{
    DIR *dp;
    struct dirent *de;
    char full[512];
    int col, len;

    dp = opendir(path);
    if (!dp) {
        perror(path);
        return;
    }

    col = 0;
    while ((de = readdir(dp)) != (struct dirent *)0) {
        if (!show_all && de->d_name[0] == '.')
            continue;

        if (strlen(path) + strlen(de->d_name) + 2 >= sizeof full)
            continue;
        strcpy(full, path);
        if (strcmp(path, "/") != 0)
            strcat(full, "/");
        strcat(full, de->d_name);

        len = strlen(de->d_name);
        if (long_format) {
            list_one(full, de->d_name);
            putchar('\n');
            continue;
        }
        if (col && col + len + 2 > 78) {
            putchar('\n');
            col = 0;
        }
        list_one(full, de->d_name);
        if (len < 14) {
            printf("%*s", 16 - len, "");
            col += 16;
        } else {
            printf("  ");
            col += len + 2;
        }
    }
    if (col)
        putchar('\n');
    closedir(dp);
}

static void
list_one(path, name)
char *path;
char *name;
{
    struct stat st;

    if (lstat(path, &st) < 0) {
        printf("%s", name);
        return;
    }
    if (long_format) {
        long_one(path, name, &st);
        return;
    }
    color_name(name, st.st_mode);
}

static void
long_one(path, name, st)
char *path;
char *name;
struct stat *st;
{
    char mbuf[12], ubuf[32], gbuf[32], tbuf[32];

    (void)path;
    mode_string(st->st_mode, mbuf);
    user_name(st->st_uid, ubuf);
    group_name(st->st_gid, gbuf);
    time_string(st->st_mtime, tbuf);

    printf("%s %3u %-8s %-8s %8ld %s ",
        mbuf, (unsigned int)st->st_nlink, ubuf, gbuf,
        (long)st->st_size, tbuf);
    color_name(name, st->st_mode);
}

static void
mode_string(mode, buf)
mode_t mode;
char *buf;
{
    static char rwx[] = "rwx";
    int i;

    switch (mode & S_IFMT) {
    case S_IFDIR: buf[0] = 'd'; break;
#ifdef S_IFLNK
    case S_IFLNK: buf[0] = 'l'; break;
#endif
#ifdef S_IFSOCK
    case S_IFSOCK: buf[0] = 's'; break;
#endif
#ifdef S_IFIFO
    case S_IFIFO: buf[0] = 'p'; break;
#endif
    case S_IFCHR: buf[0] = 'c'; break;
    case S_IFBLK: buf[0] = 'b'; break;
    default: buf[0] = '-'; break;
    }

    for (i = 0; i < 9; ++i)
        buf[i + 1] = (mode & (0400 >> i)) ? rwx[i % 3] : '-';
    buf[10] = '\0';
}

static void
user_name(uid, buf)
uid_t uid;
char *buf;
{
    struct passwd *pw;

    pw = getpwuid(uid);
    if (pw)
        strncpy(buf, pw->pw_name, 31);
    else
        sprintf(buf, "%u", (unsigned int)uid);
    buf[31] = '\0';
}

static void
group_name(gid, buf)
gid_t gid;
char *buf;
{
    struct group *gr;

    gr = getgrgid(gid);
    if (gr)
        strncpy(buf, gr->gr_name, 31);
    else
        sprintf(buf, "%u", (unsigned int)gid);
    buf[31] = '\0';
}

static void
time_string(t, buf)
time_t t;
char *buf;
{
    char *s;

    s = ctime(&t);
    if (!s) {
        strcpy(buf, "??? ?? ??:??");
        return;
    }
    strncpy(buf, s + 4, 12);
    buf[12] = '\0';
}

static char *
mode_color(mode)
mode_t mode;
{
    switch (mode & S_IFMT) {
    case S_IFDIR:
        return c_dir;
#ifdef S_IFLNK
    case S_IFLNK:
        return c_link;
#endif
    case S_IFCHR:
    case S_IFBLK:
        return c_dev;
#ifdef S_IFSOCK
    case S_IFSOCK:
        return c_sock;
#endif
#ifdef S_IFIFO
    case S_IFIFO:
        return c_fifo;
#endif
    default:
        if (mode & 0111)
            return c_exec;
        return c_file;
    }
}

static char *
suffix_color(name)
char *name;
{
    int i;
    int nlen, slen;

    nlen = strlen(name);
    for (i = 0; i < nsuffix_colors; ++i) {
        slen = strlen(suffix_colors[i].suffix);
        if (slen <= nlen &&
            strcmp(name + nlen - slen, suffix_colors[i].suffix) == 0)
            return suffix_colors[i].color;
    }
    return (char *)0;
}

static void
color_name(name, mode)
char *name;
mode_t mode;
{
    char *c;

    c = mode_color(mode);
    if (c == c_file) {
        char *sc;

        sc = suffix_color(name);
        if (sc)
            c = sc;
    }
    if (c[0])
        printf("%s%s%s", c, name, c_reset);
    else
        printf("%s", name);
}

static char *
base_name(path)
char *path;
{
    char *p;

    p = strrchr(path, '/');
    if (p && p[1])
        return p + 1;
    return path;
}
