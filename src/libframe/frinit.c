#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

void
frinit(Frame *f, Rectangle r, Font *ft, Image *b, Image *cols[NCOL])
{
	f->font = ft;
	f->display = b->display;
	f->maxtab = 8*stringwidth(ft, "0");
	f->nbox = 0;
	f->nalloc = 0;
	f->nchars = 0;
	f->nlines = 0;
	f->p0 = 0;
	f->p1 = 0;
	f->box = 0;
	f->lastlinefull = 0;
	f->lmargin = 0;
	f->rmargin = 0;
	if(cols != 0)
		memmove(f->cols, cols, sizeof f->cols);
	frsetrects(f, r, b);
	if(f->tick==nil && f->cols[BACK]!=0)
		frinittick(f);
}

void
frsetmargins(Frame *f, int l, int r)
{
	/* frinit resets both to 0, so call this after it */
	f->lmargin = l;
	f->rmargin = r;
}

void
_frtickalloc(Frame *f, int ht)
{
	Image *b;

	if(f->cols[BACK] == nil || f->display == nil)
		return;
	b = f->display->screenimage;
	/* the tick must be off the screen */
	freeimage(f->tick);
	freeimage(f->tickback);
	f->tickback = nil;
	f->tick = allocimage(f->display, Rect(0, 0, f->tickscale*FRTICKW, ht), RGBA32, 0, DTransparent);
	if(f->tick == nil)
		return;
	f->tickback = allocimage(f->display, f->tick->r, b->chan, 0, DWhite);
	if(f->tickback == nil){
		freeimage(f->tick);
		f->tick = nil;
		return;
	}
	/* vertical line */
	draw(f->tick, Rect(f->tickscale*(FRTICKW/2), 0, f->tickscale*(FRTICKW/2+1), ht), f->cols[TEXT], nil, ZP);
	/* box on each end */
	draw(f->tick, Rect(0, 0, f->tickscale*FRTICKW, f->tickscale*FRTICKW), f->cols[TEXT], nil, ZP);
	draw(f->tick, Rect(0, ht-f->tickscale*FRTICKW, f->tickscale*FRTICKW, ht), f->cols[TEXT], nil, ZP);
}

void
frinittick(Frame *f)
{
	if(f->cols[BACK] == nil || f->display == nil)
		return;
	f->tickscale = scalesize(f->display, 1);
	_frtickalloc(f, f->font->height);
}

void
frsetrects(Frame *f, Rectangle r, Image *b)
{
	f->b = b;
	f->entire = r;
	f->r = r;
	f->r.max.y -= (r.max.y-r.min.y)%f->font->height;
	f->maxlines = (r.max.y-r.min.y)/f->font->height;
}

void
frclear(Frame *f, int freeall)
{
	if(f->nbox)
		_frdelbox(f, 0, f->nbox-1);
	if(f->box)
		free(f->box);
	if(freeall){
		freeimage(f->tick);
		freeimage(f->tickback);
		f->tick = 0;
		f->tickback = 0;
	}
	f->box = 0;
	f->ticked = 0;
}
