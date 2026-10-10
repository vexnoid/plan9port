#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

void
_frdrawtext(Frame *f, Point pt, Image *text, Image *back)
{
	Frbox *b;
	int nb;

	for(nb=0,b=f->box; nb<f->nbox; nb++, b++){
		_frcklinewrap(f, &pt, b);
		if(!f->noredraw && b->nrune >= 0)
			stringbg(f->b, pt, text, ZP, f->font, (char*)b->ptr, back, ZP);
		pt.x += b->wid;
	}
}

static int
nbytes(char *s0, int nr)
{
	char *s;
	Rune r;

	s = s0;
	while(--nr >= 0)
		s += chartorune(&r, s);
	return s-s0;
}

void
frdrawsel(Frame *f, Point pt, ulong p0, ulong p1, int issel)
{
	Image *back, *text;

	if(f->ticked)
		frtick(f, frptofchar(f, f->p0), 0);

	if(p0 == p1){
		frtick(f, pt, issel);
		return;
	}

	if(issel){
		back = f->cols[HIGH];
		text = f->cols[HTEXT];
	}else{
		back = f->cols[BACK];
		text = f->cols[TEXT];
	}

	frdrawsel0(f, pt, p0, p1, back, text);
}

Point
frdrawsel0(Frame *f, Point pt, ulong p0, ulong p1, Image *back, Image *text)
{
	Frbox *b;
	int nb, nr, w, x, trim;
	Point qt;
	uint p;
	char *ptr;

	if(p0 > p1)
		sysfatal("libframe: frdrawsel0 p0=%lud > p1=%lud", p0, p1);

	_franchorhide(f);
	p = 0;
	b = f->box;
	trim = 0;
	for(nb=0; nb<f->nbox && p<p1; nb++){
		nr = b->nrune;
		if(nr < 0)
			nr = 1;
		if(p+nr <= p0)
			goto Continue;
		if(p >= p0){
			qt = pt;
			_frcklinewrap(f, &pt, b);
			/* fill in the end of a wrapped line */
			if(pt.y > qt.y)
				draw(f->b, Rect(qt.x, qt.y, f->r.max.x, pt.y), back, nil, qt);
		}
		ptr = (char*)b->ptr;
		if(p < p0){	/* beginning of region: advance into box */
			ptr += nbytes(ptr, p0-p);
			nr -= (p0-p);
			p = p0;
		}
		trim = 0;
		if(p+nr > p1){	/* end of region: trim box */
			nr -= (p+nr)-p1;
			trim = 1;
		}
		if(b->nrune<0 || nr==b->nrune)
			w = b->wid;
		else
			w = stringnwidth(f->font, ptr, nr);
		x = pt.x+w;
		if(x > f->r.max.x)
			x = f->r.max.x;
		draw(f->b, Rect(pt.x, pt.y, x, pt.y+f->font->height), back, nil, pt);
		if(b->nrune >= 0)
			stringnbg(f->b, pt, text, ZP, f->font, ptr, nr, back, ZP);
		pt.x += w;
	    Continue:
		b++;
		p += nr;
	}
	/* if this is end of last plain text box on wrapped line, fill to end of line */
	if(p1>p0 &&  b>f->box && b<f->box+f->nbox && b[-1].nrune>0 && !trim){
		qt = pt;
		_frcklinewrap(f, &pt, b);
		if(pt.y > qt.y)
			draw(f->b, Rect(qt.x, qt.y, f->r.max.x, pt.y), back, nil, qt);
	}
	_franchorshow(f);
	return pt;
}

void
frredraw(Frame *f)
{
	int ticked;
	Point pt;

	if(f->p0 == f->p1){
		ticked = f->ticked;
		if(ticked)
			frtick(f, frptofchar(f, f->p0), 0);
		frdrawsel0(f, frptofchar(f, 0), 0, f->nchars, f->cols[BACK], f->cols[TEXT]);
		if(ticked)
			frtick(f, frptofchar(f, f->p0), 1);
		return;
	}

	pt = frptofchar(f, 0);
	pt = frdrawsel0(f, pt, 0, f->p0, f->cols[BACK], f->cols[TEXT]);
	pt = frdrawsel0(f, pt, f->p0, f->p1, f->cols[HIGH], f->cols[HTEXT]);
	pt = frdrawsel0(f, pt, f->p1, f->nchars, f->cols[BACK], f->cols[TEXT]);
}

/* the anchor tick stays on top: lift it while painting under it */
static void
_fratick(Frame *f, int ticked)
{
	Point pt;
	Rectangle r;

	if(f->aticked==ticked || f->atick==0 || f->noredraw)
		return;
	pt = frptofchar(f, f->apos);
	if(!ptinrect(pt, f->r))
		return;
	pt.x -= f->tickscale;
	r = Rect(pt.x, pt.y, pt.x+FRTICKW*f->tickscale, pt.y+f->font->height);
	if(r.max.x > f->r.max.x)
		r.max.x = f->r.max.x;
	if(ticked){
		draw(f->atickback, f->atickback->r, f->b, nil, pt);
		draw(f->b, r, f->cols[BORD], f->atick, ZP);
	}else
		draw(f->b, r, f->atickback, nil, ZP);
	f->aticked = ticked;
}

void
_franchorhide(Frame *f)
{
	_fratick(f, 0);
}

void
_franchorshow(Frame *f)
{
	if(f->aset && f->apos <= f->nchars)
		_fratick(f, 1);
}

void
frsetanchor(Frame *f, int set, ulong p)
{
	if(f->aset==set && (!set || f->apos==p))
		return;
	_franchorhide(f);
	f->aset = set;
	f->apos = p;
	_franchorshow(f);
}

static void
_frtick(Frame *f, Point pt, int ticked)
{
	Rectangle r;
	int anchored;

	if(f->ticked==ticked || f->tick==0 || !ptinrect(pt, f->r))
		return;
	anchored = f->aticked;
	_franchorhide(f);
	pt.x -= f->tickscale;	/* looks best just left of where requested */
	r = Rect(pt.x, pt.y, pt.x+FRTICKW*f->tickscale, pt.y+f->font->height);
	/* can go into left border but not right */
	if(r.max.x > f->r.max.x)
		r.max.x = f->r.max.x;
	if(ticked){
		draw(f->tickback, f->tickback->r, f->b, nil, pt);
		draw(f->b, r, f->tick, nil, ZP);
	}else
		draw(f->b, r, f->tickback, nil, ZP);
	f->ticked = ticked;
	if(anchored)
		_franchorshow(f);
}

void
frtick(Frame *f, Point pt, int ticked)
{
	int anchored;

	if(f->tickscale != scalesize(f->display, 1)) {
		anchored = f->aticked;
		_franchorhide(f);
		if(f->ticked)
			_frtick(f, pt, 0);
		frinittick(f);
		if(anchored)
			_franchorshow(f);
	}
	_frtick(f, pt, ticked);
}

Point
_frdraw(Frame *f, Point pt)
{
	Frbox *b;
	int nb, n;

	for(b=f->box,nb=0; nb<f->nbox; nb++, b++){
		_frcklinewrap0(f, &pt, b);
		if(pt.y == f->r.max.y){
			f->nchars -= _frstrlen(f, nb);
			_frdelbox(f, nb, f->nbox-1);
			break;
		}
		if(b->nrune > 0){
			n = _frcanfit(f, pt, b);
			if(n == 0)
				break;
			if(n != b->nrune){
				_frsplitbox(f, nb, n);
				b = &f->box[nb];
			}
			pt.x += b->wid;
		}else{
			if(b->bc == '\n'){
				pt.x = f->r.min.x;
				pt.y+=f->font->height;
			}else
				pt.x += _frnewwid(f, pt, b);
		}
	}
	return pt;
}

int
_frstrlen(Frame *f, int nb)
{
	int n;

	for(n=0; nb<f->nbox; nb++)
		n += NRUNE(&f->box[nb]);
	return n;
}
