#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"

extern char Ebadctl[];

typedef struct Fontcache Fontcache;
struct Fontcache
{
	char		*name;
	Font		*f;
	Fontcache	*next;
};
static Fontcache *fontcache;

static Font*
lookupfont(char *name)
{
	Fontcache *c;
	Font *f;

	for(c=fontcache; c; c=c->next)
		if(strcmp(c->name, name) == 0)
			return c->f;
	f = openfont(display, name);
	if(f == nil)
		return nil;
	c = emalloc(sizeof(Fontcache));
	c->name = estrdup(name);
	c->f = f;
	c->next = fontcache;
	fontcache = c;
	return f;
}

static char*
fontname(Font *f)
{
	if(f == nil)
		return "-";
	return f->name;
}

typedef struct Colorcache Colorcache;
struct Colorcache
{
	ulong		rgba;
	Image		*i;
	Colorcache	*next;
};
static Colorcache *colorcache;

static Image*
lookupcolor(ulong rgb)
{
	Colorcache *c;
	ulong rgba;

	rgba = (rgb<<8) | 0xFF;
	for(c=colorcache; c; c=c->next)
		if(c->rgba == rgba)
			return c->i;
	c = emalloc(sizeof(Colorcache));
	c->i = allocimage(display, Rect(0,0,1,1), screen->chan, 1, rgba);
	if(c->i == nil){
		free(c);
		return nil;
	}
	c->rgba = rgba;
	c->next = colorcache;
	colorcache = c;
	return c->i;
}

/* buf needs 7 bytes */
static char*
colorname(Image *img, char *buf)
{
	Colorcache *c;

	if(img == nil){
		strcpy(buf, "-");
		return buf;
	}
	for(c=colorcache; c; c=c->next)
		if(c->i == img){
			sprint(buf, "%06lux", c->rgba>>8);
			return buf;
		}
	strcpy(buf, "-");
	return buf;
}

/* interns (font,col) pairs so equal markup shares one pointer */
typedef struct Frmarkupcache Frmarkupcache;
struct Frmarkupcache
{
	Font		*font;
	Image		*col;
	Frmarkup	fm;
	Frmarkupcache	*next;
};
static Frmarkupcache *frmarkupcache;

static Frmarkup*
lookupfrmarkup(Font *font, Image *col)
{
	Frmarkupcache *c;

	if(font==nil && col==nil)	/* "no override" is not a miss */
		return nil;
	for(c=frmarkupcache; c; c=c->next)
		if(c->font==font && c->col==col)
			return &c->fm;
	c = emalloc(sizeof(Frmarkupcache));
	c->font = font;
	c->col = col;
	c->fm.font = font;
	c->fm.col = col;
	c->next = frmarkupcache;
	frmarkupcache = c;
	return &c->fm;
}

/* spans persist across scrolling; the frame's own boxes don't */
struct Fgspan
{
	uint q0, q1;
	Frmarkup *fg;
	Fgspan *next;
};
struct Bgspan
{
	uint q0, q1;
	Image *bg;
	Bgspan *next;
};

static void
filemarkupfg(File *f, uint q0, uint q1, Frmarkup *fg)
{
	Fgspan *s, *next, **pp, *r;

	if(q0 >= q1)
		return;
	pp = &f->fgspans;
	while((s = *pp) != nil){
		next = s->next;
		if(s->q1<=q0 || s->q0>=q1){
			pp = &s->next;
			continue;
		}
		*pp = next;
		if(s->q0 < q0){
			r = emalloc(sizeof(Fgspan));
			r->q0 = s->q0;
			r->q1 = q0;
			r->fg = s->fg;
			r->next = f->fgspans;
			f->fgspans = r;
		}
		if(s->q1 > q1){
			r = emalloc(sizeof(Fgspan));
			r->q0 = q1;
			r->q1 = s->q1;
			r->fg = s->fg;
			r->next = f->fgspans;
			f->fgspans = r;
		}
		free(s);
	}
	if(fg != nil){
		s = emalloc(sizeof(Fgspan));
		s->q0 = q0;
		s->q1 = q1;
		s->fg = fg;
		s->next = f->fgspans;
		f->fgspans = s;
	}
}

static void
filemarkupbg(File *f, uint q0, uint q1, Image *bg)
{
	Bgspan *s, *next, **pp, *r;

	if(q0 >= q1)
		return;
	pp = &f->bgspans;
	while((s = *pp) != nil){
		next = s->next;
		if(s->q1<=q0 || s->q0>=q1){
			pp = &s->next;
			continue;
		}
		*pp = next;
		if(s->q0 < q0){
			r = emalloc(sizeof(Bgspan));
			r->q0 = s->q0;
			r->q1 = q0;
			r->bg = s->bg;
			r->next = f->bgspans;
			f->bgspans = r;
		}
		if(s->q1 > q1){
			r = emalloc(sizeof(Bgspan));
			r->q0 = q1;
			r->q1 = s->q1;
			r->bg = s->bg;
			r->next = f->bgspans;
			f->bgspans = r;
		}
		free(s);
	}
	if(bg != nil){
		s = emalloc(sizeof(Bgspan));
		s->q0 = q0;
		s->q1 = q1;
		s->bg = bg;
		s->next = f->bgspans;
		f->bgspans = s;
	}
}

/*
 * Inserted text is plain, as it is in the frame, so a span that
 * the insertion lands inside is split around it.
 */
void
filemarkupins(File *f, uint q0, uint n)
{
	Fgspan *fs, *fx;
	Bgspan *bs, *bx;

	if(n == 0)
		return;
	for(fs=f->fgspans; fs; fs=fs->next){
		if(q0 <= fs->q0){
			fs->q0 += n;
			fs->q1 += n;
		}else if(q0 < fs->q1){
			fx = emalloc(sizeof(Fgspan));
			fx->q0 = q0+n;
			fx->q1 = fs->q1+n;
			fx->fg = fs->fg;
			fx->next = fs->next;
			fs->q1 = q0;
			fs->next = fx;
			fs = fx;
		}
	}
	for(bs=f->bgspans; bs; bs=bs->next){
		if(q0 <= bs->q0){
			bs->q0 += n;
			bs->q1 += n;
		}else if(q0 < bs->q1){
			bx = emalloc(sizeof(Bgspan));
			bx->q0 = q0+n;
			bx->q1 = bs->q1+n;
			bx->bg = bs->bg;
			bx->next = bs->next;
			bs->q1 = q0;
			bs->next = bx;
			bs = bx;
		}
	}
}

void
filemarkupdel(File *f, uint q0, uint q1)
{
	Fgspan *fs, **fpp;
	Bgspan *bs, **bpp;
	uint n;

	if(q1 <= q0)
		return;
	n = q1-q0;
	fpp = &f->fgspans;
	while((fs = *fpp) != nil){
		if(q0 < fs->q0)
			fs->q0 -= min(n, fs->q0-q0);
		if(q0 < fs->q1)
			fs->q1 -= min(n, fs->q1-q0);
		if(fs->q0 == fs->q1){
			*fpp = fs->next;
			free(fs);
		}else
			fpp = &fs->next;
	}
	bpp = &f->bgspans;
	while((bs = *bpp) != nil){
		if(q0 < bs->q0)
			bs->q0 -= min(n, bs->q0-q0);
		if(q0 < bs->q1)
			bs->q1 -= min(n, bs->q1-q0);
		if(bs->q0 == bs->q1){
			*bpp = bs->next;
			free(bs);
		}else
			bpp = &bs->next;
	}
}

/* fg/bg in effect at p, plus next span boundary >= p */
Frmarkup*
filemarkupfgat(File *f, uint p, uint *nextp)
{
	Fgspan *s, *hit;
	uint next;

	hit = nil;
	next = Infinity;
	for(s=f->fgspans; s; s=s->next){
		if(s->q0<=p && p<s->q1)
			hit = s;
		else if(s->q0>p && s->q0<next)
			next = s->q0;
	}
	if(hit!=nil && hit->q1<next)
		next = hit->q1;
	*nextp = next;
	return hit!=nil? hit->fg : nil;
}

Image*
filemarkupbgat(File *f, uint p, uint *nextp)
{
	Bgspan *s, *hit;
	uint next;

	hit = nil;
	next = Infinity;
	for(s=f->bgspans; s; s=s->next){
		if(s->q0<=p && p<s->q1)
			hit = s;
		else if(s->q0>p && s->q0<next)
			next = s->q0;
	}
	if(hit!=nil && hit->q1<next)
		next = hit->q1;
	*nextp = next;
	return hit!=nil? hit->bg : nil;
}

void
filemarkupfree(File *f)
{
	Fgspan *fs, *fn;
	Bgspan *bs, *bn;

	for(fs=f->fgspans; fs; fs=fn){
		fn = fs->next;
		free(fs);
	}
	f->fgspans = nil;
	for(bs=f->bgspans; bs; bs=bn){
		bn = bs->next;
		free(bs);
	}
	f->bgspans = nil;
}

/* maps a File-relative range onto one Text's own frame coordinates */
static void
rangetotextfr(Text *t, ulong q0, ulong q1, ulong *p0p, ulong *p1p)
{
	ulong org, n;

	org = t->org;
	n = t->fr.nchars;
	if(q0 < org)
		q0 = org;
	if(q1 < org)
		q1 = org;
	q0 -= org;
	q1 -= org;
	if(q0 > n)
		q0 = n;
	if(q1 > n)
		q1 = n;
	*p0p = q0;
	*p1p = q1;
}

static void
filemarkupsetfg(File *f, ulong q0, ulong q1, Frmarkup *fg)
{
	Text *t;
	ulong p0, p1;
	int i;

	filemarkupfg(f, q0, q1, fg);
	for(i=0; i<f->ntext; i++){
		t = f->text[i];
		rangetotextfr(t, q0, q1, &p0, &p1);
		frmarkupfg(&t->fr, p0, p1, fg);
		/* a new font can change widths, and so how much text fits */
		textfill(t);
		textscrdraw(t);
		textsetselect(t, t->q0, t->q1);
	}
}

static void
filemarkupsetbg(File *f, ulong q0, ulong q1, Image *bg)
{
	Text *t;
	ulong p0, p1;
	int i;

	filemarkupbg(f, q0, q1, bg);
	for(i=0; i<f->ntext; i++){
		t = f->text[i];
		rangetotextfr(t, q0, q1, &p0, &p1);
		frmarkupbg(&t->fr, p0, p1, bg);
	}
}

static int
parsecolor(char *s, Image **ip)
{
	char *end;
	ulong rgb;

	if(strcmp(s, "-") == 0){
		*ip = nil;
		return 1;
	}
	if(strlen(s)!=6 || strspn(s, "0123456789abcdefABCDEF")!=6)
		return 0;
	rgb = strtoul(s, &end, 16);
	if(*end != 0)
		return 0;
	*ip = lookupcolor(rgb);
	return *ip != nil;
}

/* mixed range collapses to markup in effect at q0 */
static Frmarkup*
curfg(Window *w, ulong q0)
{
	uint nx;

	return filemarkupfgat(w->body.file, q0, &nx);
}

/*
 * One line of the markup file:
 *	clear q0 q1
 *	fg q0 q1 rrggbb
 *	bg q0 q1 rrggbb
 *	font q0 q1 name
 * "-" for a color or font means the frame's default.
 */
char*
markupcmd(Window *w, char *line)
{
	char *av[10], *end;
	int nf;
	Font *font;
	Frmarkup *cur;
	Image *bg, *col;
	ulong q0, q1, nc;

	nf = tokenize(line, av, nelem(av));
	if(nf == 0)
		return nil;
	if(nf < 3)
		return Ebadctl;
	q0 = strtoul(av[1], &end, 10);
	if(end==av[1] || *end!='\0')
		return Ebadctl;
	q1 = strtoul(av[2], &end, 10);
	if(end==av[2] || *end!='\0')
		return Ebadctl;
	if(q0 > q1)
		return Ebadctl;
	nc = w->body.file->b.nc;
	if(q0 > nc)
		q0 = nc;
	if(q1 > nc)
		q1 = nc;

	if(strcmp(av[0], "clear") == 0){
		if(nf != 3)
			return Ebadctl;
		filemarkupsetfg(w->body.file, q0, q1, nil);
		filemarkupsetbg(w->body.file, q0, q1, nil);
		return nil;
	}
	if(strcmp(av[0], "bg") == 0){
		if(nf != 4)
			return Ebadctl;
		if(!parsecolor(av[3], &bg))
			return "bad color";
		filemarkupsetbg(w->body.file, q0, q1, bg);
		return nil;
	}
	if(strcmp(av[0], "fg") == 0){
		if(nf != 4)
			return Ebadctl;
		if(!parsecolor(av[3], &col))
			return "bad color";
		cur = curfg(w, q0);
		filemarkupsetfg(w->body.file, q0, q1, lookupfrmarkup(cur!=nil? cur->font : nil, col));
		return nil;
	}
	if(strcmp(av[0], "font") == 0){
		if(nf != 4)
			return Ebadctl;
		if(strcmp(av[3], "-") == 0)
			font = nil;
		else{
			font = lookupfont(av[3]);
			if(font == nil)
				return "can't open font";
			if(font->height != w->body.fr.font->height)
				return "markup font must match body font height";
		}
		cur = curfg(w, q0);
		filemarkupsetfg(w->body.file, q0, q1, lookupfrmarkup(font, cur!=nil? cur->col : nil));
		return nil;
	}
	return Ebadctl;
}

typedef struct Sbuf Sbuf;
struct Sbuf
{
	char	*s;
	int		n;
	int		max;
};

static void
sbappend(Sbuf *b, char *s)
{
	int n;

	n = strlen(s);
	if(b->n+n+1 > b->max){
		b->max = 2*(b->n+n+1)+256;
		b->s = erealloc(b->s, b->max);
	}
	memmove(b->s+b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void
sbnum(Sbuf *b, char *word, uint q0, uint q1)
{
	char buf[64];

	snprint(buf, sizeof buf, "%s %ud %ud ", word, q0, q1);
	sbappend(b, buf);
}

/* dumps markup in markupcmd's own syntax; caller frees the n-byte result */
char*
filemarkupdump(File *f, int *np)
{
	Fgspan *fs;
	Bgspan *bs;
	Sbuf sb;
	char cbuf[8];

	memset(&sb, 0, sizeof sb);
	for(fs=f->fgspans; fs; fs=fs->next){
		if(fs->fg->font != nil){
			sbnum(&sb, "font", fs->q0, fs->q1);
			sbappend(&sb, fontname(fs->fg->font));
			sbappend(&sb, "\n");
		}
		if(fs->fg->col != nil){
			sbnum(&sb, "fg", fs->q0, fs->q1);
			sbappend(&sb, colorname(fs->fg->col, cbuf));
			sbappend(&sb, "\n");
		}
	}
	for(bs=f->bgspans; bs; bs=bs->next){
		sbnum(&sb, "bg", bs->q0, bs->q1);
		sbappend(&sb, colorname(bs->bg, cbuf));
		sbappend(&sb, "\n");
	}
	if(sb.s == nil)
		sb.s = estrdup("");
	*np = sb.n;
	return sb.s;
}
