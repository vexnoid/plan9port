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

static Image *scrtmp;

static
Rectangle
scrpos(Rectangle r, uint p0, uint p1, uint tot)
{
	Rectangle q;
	int h;

	q = r;
	h = q.max.y-q.min.y;
	if(tot == 0)
		return q;
	if(tot > 1024*1024){
		tot>>=10;
		p0>>=10;
		p1>>=10;
	}
	if(p0 > 0)
		q.min.y += h*p0/tot;
	if(p1 < tot)
		q.max.y -= h*(tot-p1)/tot;
	if(q.max.y < q.min.y+2){
		if(q.min.y+2 <= r.max.y)
			q.max.y = q.min.y+2;
		else
			q.min.y = q.max.y-2;
	}
	return q;
}

void
scrlresize(void)
{
	freeimage(scrtmp);
	scrtmp = allocimage(display, Rect(0, 0, 32, screen->r.max.y), screen->chan, 0, DNofill);
	if(scrtmp == nil)
		error("scroll alloc");
}

void
textscrdraw(Text *t)
{
	Rectangle r, r1, r2;
	Image *b;

	if(t->w==nil || t!=&t->w->body)
		return;
	if(scrtmp == nil)
		scrlresize();
	r = t->scrollr;
	b = scrtmp;
	r1 = r;
	r1.min.x = 0;
	r1.max.x = Dx(r);
	r2 = scrpos(r1, t->org, t->org+t->fr.nchars, t->file->b.nc);
	if(!eqrect(r2, t->lastsr)){
		t->lastsr = r2;
		draw(b, r1, t->fr.cols[BORD], nil, ZP);
		draw(b, r2, t->fr.cols[BACK], nil, ZP);
		r2.min.x = r2.max.x-1;
		draw(b, r2, t->fr.cols[BORD], nil, ZP);
		draw(t->fr.b, r, b, nil, Pt(0, r1.min.y));
	}
}

void
scrsleep(uint dt)
{
	Timer	*timer;
	static Alt alts[3];

	timer = timerstart(dt);
	alts[0].c = timer->c;
	alts[0].v = nil;
	alts[0].op = CHANRCV;
	alts[1].c = mousectl->c;
	alts[1].v = &mousectl->m;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;
	for(;;)
		switch(alt(alts)){
		case 0:
			timerstop(timer);
			return;
		case 1:
			timercancel(timer);
			return;
		}
}

void
textscroll(Text *t)
{
	uint p0, oldp0, o;
	Rectangle s;
	int x, y, my, h;

	s = insetrect(t->scrollr, 1);
	h = s.max.y-s.min.y;
	x = (s.min.x+s.max.x)/2;
	oldp0 = ~0;
	o = t->org;
	do{
		flushimage(display, 1);
		my = mouse->xy.y;
		if(my < s.min.y)
			my = s.min.y;
		if(my >= s.max.y)
			my = s.max.y;
		if(!eqpt(mouse->xy, Pt(x, my))){
			moveto(mousectl, Pt(x, my));
			readmouse(mousectl);
		}
		y = my;
		p0 = (vlong)t->file->b.nc*(y-s.min.y)/h;
		if(p0 >= t->q1)
			p0 = textbacknl(t, p0, 2);
		if(oldp0 != p0)
			textsetorigin(t, p0, FALSE);
		oldp0 = p0;
		readmouse(mousectl);
	}while(mouse->buttons & 2);
	while(mouse->buttons)
		readmouse(mousectl);
	textscrollmark(t, o);	/* one undo step for the whole drag */
}

void
textscroll4(Text *t, int up)
{
	uint p0;
	Rectangle s;
	int my;

	s = insetrect(t->scrollr, 1);
	my = mouse->xy.y;
	if(my < s.min.y)
		my = s.min.y;
	if(my >= s.max.y)
		my = s.max.y;
	if(up)
		p0 = textbacknl(t, t->org, (my-s.min.y)/t->fr.font->height);
	else
		p0 = t->org+frcharofpt(&t->fr, Pt(s.max.x, my));
	textsetorigin(t, p0, TRUE);
}

static void
scrpush(uint **s, int *n, uint v)
{
	*s = erealloc(*s, (*n+1)*sizeof(uint));
	(*s)[(*n)++] = v;
}

/* record that the view jumped from "from" to t->org */
void
textscrollmark(Text *t, uint from)
{
	if(t->w==nil || t!=&t->w->body || from==t->org)
		return;
	scrpush(&t->scundo, &t->nscundo, from);
	t->nscredo = 0;
}

void
textscrollundo(Text *t, int isundo)
{
	uint org;

	if(t->w==nil || t!=&t->w->body)
		return;
	if(isundo){
		if(t->nscundo == 0)
			return;
		scrpush(&t->scredo, &t->nscredo, t->org);
		org = t->scundo[--t->nscundo];
	}else{
		if(t->nscredo == 0)
			return;
		scrpush(&t->scundo, &t->nscundo, t->org);
		org = t->scredo[--t->nscredo];
	}
	if(org > t->file->b.nc)
		org = t->file->b.nc;
	textsetorigin(t, org, TRUE);
}
