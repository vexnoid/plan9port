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

static char*
colorname(Image *img, char *buf)
{
	Colorcache *c;

	for(c=colorcache; c->i!=img; c=c->next)	/* img came from lookupcolor */
		;
	sprint(buf, "%06lux", c->rgba>>8);	/* buf needs 7 bytes */
	return buf;
}

typedef struct Frmarkupcache Frmarkupcache;
struct Frmarkupcache
{
	Frmarkup	fm;
	Frmarkupcache	*next;
};
static Frmarkupcache *frmarkupcache;

static Frmarkup*
lookupfrmarkup(Frmarkup *m)
{
	Frmarkupcache *c;
	static Frmarkup none;

	if(memcmp(m, &none, sizeof none) == 0)	/* "no override" is not a miss */
		return nil;
	/* intern, so equal markup shares one pointer and frame boxes can merge */
	for(c=frmarkupcache; c; c=c->next)
		if(memcmp(&c->fm, m, sizeof *m) == 0)
			return &c->fm;
	c = emalloc(sizeof(Frmarkupcache));
	c->fm = *m;
	c->next = frmarkupcache;
	frmarkupcache = c;
	return &c->fm;
}

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

typedef struct Span Span;
struct Span
{
	uint q0, q1;
	void *v;
	Span *next;
};

typedef struct Hint Hint;
struct Hint
{
	Span **head;
	Span *prev;
};
static Hint hint[4];

static Span**
spanfind(Span **head, uint p)
{
	Hint *h;
	Span *s, *prev, **pp;

	h = &hint[((uintptr)head>>3) % nelem(hint)];
	prev = nil;
	pp = head;
	/* resume after the last span passed, so in-order access doesn't rescan */
	if(h->head==head && h->prev!=nil && h->prev->q1<=p){
		prev = h->prev;
		pp = &prev->next;
	}
	/* lists are sorted and disjoint; stop at the first span ending after p */
	while((s = *pp)!=nil && s->q1<=p){
		prev = s;
		pp = &s->next;
	}
	h->head = head;
	h->prev = prev;
	return pp;
}

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
	/* nil v just clears the range */
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
	/* nextp is where the markup next changes */
	if(s==nil || s->q0>p){
		*nextp = s!=nil? s->q0 : Infinity;
		return nil;
	}
	*nextp = s->q1;
	return s->v;
}

static void
spanins(Span *s, uint q0, uint n)
{
	Span *x;

	for(; s; s=s->next){
		if(q0 <= s->q0){
			s->q0 += n;
			s->q1 += n;
		}else if(q0 < s->q1){	/* inserted text is plain, so split the span */
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
	/* Fgspan and Bgspan have Span's layout, so one set of routines serves both */
	spanins((Span*)f->fgspans, q0, n);
	spanins((Span*)f->bgspans, q0, n);
}

void
filemarkupdel(File *f, uint q0, uint q1)
{
	if(q1 <= q0)
		return;
	memset(hint, 0, sizeof hint);	/* spans are about to be freed */
	spandel((Span**)&f->fgspans, q0, q1);
	spandel((Span**)&f->bgspans, q0, q1);
}

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

static void
rangetotextfr(Text *t, ulong q0, ulong q1, ulong *p0p, ulong *p1p)
{
	ulong org, n;

	/* frame offsets are relative to org and clipped to what the frame holds */
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

	/* the frame forgets scrolled-out boxes, so the spans are kept here too */
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
	if(strcmp(s, "default") == 0){
		*ip = nil;
		return 1;
	}
	if(strlen(s)!=6 || strspn(s, "0123456789abcdefABCDEF")!=6)
		return 0;
	*ip = lookupcolor(strtoul(s, nil, 16));
	return *ip != nil;
}

char*
markupcmd(Window *w, char *line)
{
	char *av[5], *end;
	int nf;
	uint nx;
	Frmarkup *cur, m;
	Image *bg;
	ulong q0, q1, nc;

	nf = tokenize(line, av, nelem(av));
	if(nf == 0)
		return nil;
	if(nf != 4)
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

	if(strcmp(av[0], "bg") == 0){
		if(!parsecolor(av[3], &bg))
			return "bad color";
		filemarkupsetbg(w->body.file, q0, q1, bg);
		return nil;
	}
	/* a mixed range takes the other attributes from q0 */
	cur = filemarkupfgat(w->body.file, q0, &nx);
	memset(&m, 0, sizeof m);
	if(cur != nil)
		m = *cur;
	if(strcmp(av[0], "fg") == 0){
		if(!parsecolor(av[3], &m.col))
			return "bad color";
	}else if(strcmp(av[0], "underline") == 0){
		if(!parsecolor(av[3], &m.underline))
			return "bad color";
	}else if(strcmp(av[0], "strikethrough") == 0){
		if(!parsecolor(av[3], &m.strikethrough))
			return "bad color";
	}else if(strcmp(av[0], "font") == 0){
		if(strcmp(av[3], "default") == 0)
			m.font = nil;
		else{
			m.font = lookupfont(av[3]);
			if(m.font == nil)
				return "can't open font";
		}
	}else
		return Ebadctl;
	filemarkupsetfg(w->body.file, q0, q1, lookupfrmarkup(&m));
	return nil;
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
sbcmd(Sbuf *b, char *word, uint q0, uint q1, char *arg)
{
	char buf[64];

	snprint(buf, sizeof buf, "%s %ud %ud ", word, q0, q1);
	sbappend(b, buf);
	sbappend(b, arg);
	sbappend(b, "\n");
}

char*
filemarkupdump(File *f, int *np)
{
	Fgspan *fs;
	Bgspan *bs;
	Sbuf sb;
	char cbuf[8];

	/* written in the syntax markupcmd reads */
	memset(&sb, 0, sizeof sb);
	for(fs=f->fgspans; fs; fs=fs->next){
		if(fs->fg->font != nil)
			sbcmd(&sb, "font", fs->q0, fs->q1, fs->fg->font->name);
		if(fs->fg->col != nil)
			sbcmd(&sb, "fg", fs->q0, fs->q1, colorname(fs->fg->col, cbuf));
		if(fs->fg->underline != nil)
			sbcmd(&sb, "underline", fs->q0, fs->q1, colorname(fs->fg->underline, cbuf));
		if(fs->fg->strikethrough != nil)
			sbcmd(&sb, "strikethrough", fs->q0, fs->q1, colorname(fs->fg->strikethrough, cbuf));
	}
	for(bs=f->bgspans; bs; bs=bs->next)
		sbcmd(&sb, "bg", bs->q0, bs->q1, colorname(bs->bg, cbuf));
	if(sb.s == nil)
		sb.s = estrdup("");
	*np = sb.n;
	return sb.s;	/* caller frees */
}
