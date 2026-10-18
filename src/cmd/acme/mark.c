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

/* same layout as both, so one set of routines serves them; lists stay sorted and disjoint */
typedef struct Span Span;
struct Span
{
	uint q0, q1;
	void *v;
	Span *next;
};

/* last span a search passed, so in-order access doesn't rescan */
typedef struct Hint Hint;
struct Hint
{
	Span **head;
	Span *prev;
};
static Hint hint[4];

/* link to the first span ending after p */
static Span**
spanfind(Span **head, uint p)
{
	Hint *h;
	Span *s, *prev, **pp;

	h = &hint[((uintptr)head>>3) % nelem(hint)];
	prev = nil;
	pp = head;
	if(h->head==head && h->prev!=nil && h->prev->q1<=p){
		prev = h->prev;
		pp = &prev->next;
	}
	while((s = *pp)!=nil && s->q1<=p){
		prev = s;
		pp = &s->next;
	}
	h->head = head;
	h->prev = prev;
	return pp;
}

/* v==nil clears */
static void
spanset(Span **head, uint q0, uint q1, void *v)
{
	Span *s, *r, **pp;

	if(q0 >= q1)
		return;
	pp = spanfind(head, q0);
	s = *pp;
	if(s!=nil && s->q0<q0){
		if(s->q1 > q1){
			r = emalloc(sizeof(Span));
			*r = *s;
			r->q0 = q1;
			s->next = r;
		}
		s->q1 = q0;
		pp = &s->next;
		s = *pp;
	}
	while(s!=nil && s->q1<=q1){
		*pp = s->next;
		free(s);
		s = *pp;
	}
	if(s!=nil && s->q0<q1)
		s->q0 = q1;
	if(v != nil){
		r = emalloc(sizeof(Span));
		r->q0 = q0;
		r->q1 = q1;
		r->v = v;
		r->next = s;
		*pp = r;
	}
}

static void*
spanat(Span **head, uint p, uint *nextp)
{
	Span *s;

	s = *spanfind(head, p);
	if(s==nil || s->q0>p){
		*nextp = s!=nil? s->q0 : Infinity;
		return nil;
	}
	*nextp = s->q1;
	return s->v;
}

/* inserted text is plain; split a span it lands inside */
static void
spanins(Span *s, uint q0, uint n)
{
	Span *x;

	for(; s; s=s->next){
		if(q0 <= s->q0){
			s->q0 += n;
			s->q1 += n;
		}else if(q0 < s->q1){
			x = emalloc(sizeof(Span));
			*x = *s;
			x->q0 = q0+n;
			x->q1 += n;
			s->q1 = q0;
			s->next = x;
			s = x;
		}
	}
}

static void
spandel(Span **pp, uint q0, uint q1)
{
	Span *s;
	uint n;

	n = q1-q0;
	while((s = *pp) != nil){
		if(q0 < s->q0)
			s->q0 -= min(n, s->q0-q0);
		if(q0 < s->q1)
			s->q1 -= min(n, s->q1-q0);
		if(s->q0 == s->q1){
			*pp = s->next;
			free(s);
		}else
			pp = &s->next;
	}
}

void
filemarkupins(File *f, uint q0, uint n)
{
	if(n == 0)
		return;
	spanins((Span*)f->fgspans, q0, n);
	spanins((Span*)f->bgspans, q0, n);
}

void
filemarkupdel(File *f, uint q0, uint q1)
{
	if(q1 <= q0)
		return;
	memset(hint, 0, sizeof hint);
	spandel((Span**)&f->fgspans, q0, q1);
	spandel((Span**)&f->bgspans, q0, q1);
}

/* fg/bg in effect at p, plus next span boundary >= p */
Frmarkup*
filemarkupfgat(File *f, uint p, uint *nextp)
{
	return spanat((Span**)&f->fgspans, p, nextp);
}

Image*
filemarkupbgat(File *f, uint p, uint *nextp)
{
	return spanat((Span**)&f->bgspans, p, nextp);
}

void
filemarkupfree(File *f)
{
	filemarkupdel(f, 0, Infinity);
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

	spanset((Span**)&f->fgspans, q0, q1, fg);
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

	spanset((Span**)&f->bgspans, q0, q1, bg);
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
