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
			_string(f->b, pt, text, ZP, f->font, (char*)b->ptr, nil, 1<<24, f->r, back, ZP, SoverD);
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

/*
 * Redrawing a glyph over its own ink darkens its antialiased edges, so
 * clip at r.min.x, the start of the fill, and redraw the overhang of the
 * char after p1, which the fill erased.
 */
Point
frdrawsel0(Frame *f, Point pt, ulong p0, ulong p1, Image *back, Image *text)
{
	Frbox *b;
	int nb, nr, w, x, trim;
	Point qt;
	uint p;
	char *ptr;
	Rectangle r;

	if(p0 > p1)
		sysfatal("libframe: frdrawsel0 p0=%lud > p1=%lud", p0, p1);

	p = 0;
	b = f->box;
	trim = 0;
	r = f->r;
	r.min.x = pt.x;
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
			if(pt.y > qt.y){
				draw(f->b, Rect(qt.x, qt.y, f->r.max.x, pt.y), back, nil, qt);
				r.min.x = f->r.min.x;
			}
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
			_string(f->b, pt, text, ZP, f->font, ptr, nil, nr, r, back, ZP, SoverD);
		pt.x += w;
	    Continue:
		b++;
		p += nr;
	}
	if(p1>p0 && (trim || b<f->box+f->nbox && b->nrune>0)){
		qt = pt;
		if(trim)
			ptr += nbytes(ptr, nr);
		else{
			_frcklinewrap(f, &qt, b);
			ptr = (char*)b->ptr;
		}
		if(qt.y == pt.y){
			r.max.x = pt.x;
			r.min.y = pt.y;
			r.max.y = pt.y+f->font->height;
			if(p1>=f->p0 && p1<f->p1)
				_string(f->b, pt, f->cols[HTEXT], ZP, f->font, ptr, nil, 1, r, nil, ZP, SoverD);
			else
				_string(f->b, pt, f->cols[TEXT], ZP, f->font, ptr, nil, 1, r, nil, ZP, SoverD);
		}
	}
	/* if this is end of last plain text box on wrapped line, fill to end of line */
	if(p1>p0 &&  b>f->box && b<f->box+f->nbox && b[-1].nrune>0 && !trim){
		qt = pt;
		_frcklinewrap(f, &pt, b);
		if(pt.y > qt.y)
			draw(f->b, Rect(qt.x, qt.y, f->r.max.x, pt.y), back, nil, qt);
	}
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
		_frfixmargin(f);
		if(ticked)
			frtick(f, frptofchar(f, f->p0), 1);
		return;
	}

	pt = frptofchar(f, 0);
	pt = frdrawsel0(f, pt, 0, f->p0, f->cols[BACK], f->cols[TEXT]);
	pt = frdrawsel0(f, pt, f->p0, f->p1, f->cols[HIGH], f->cols[HTEXT]);
	pt = frdrawsel0(f, pt, f->p1, f->nchars, f->cols[BACK], f->cols[TEXT]);
	_frfixmargin(f);
}

/*
 * Redraw q0 to q1 as frredraw would.  frinsert and frdelete move boxes by
 * copying pixels, which also copies any overhang from the old neighbours.
 * They start at the char before the change, since the overhang of a glyph
 * that was removed or moved lies in the cell to its left.
 */
void
_frrepaint(Frame *f, ulong q0, ulong q1)
{
	Point pt;
	ulong p0, p1;

	if(q1 > f->nchars)
		q1 = f->nchars;
	if(q0 >= q1)
		return;
	p0 = f->p0;
	if(p0 < q0)
		p0 = q0;
	if(p0 > q1)
		p0 = q1;
	p1 = f->p1;
	if(p1 < p0)
		p1 = p0;
	if(p1 > q1)
		p1 = q1;
	pt = frptofchar(f, q0);
	pt = frdrawsel0(f, pt, q0, p0, f->cols[BACK], f->cols[TEXT]);
	pt = frdrawsel0(f, pt, p0, p1, f->cols[HIGH], f->cols[HTEXT]);
	frdrawsel0(f, pt, p1, q1, f->cols[BACK], f->cols[TEXT]);
}

/*
 * Glyphs are drawn clipped to f->r, so the margins are never disturbed by
 * ordinary drawing.  Erase them, then draw the overhang of the boxes at the
 * ends of rows into them.  The ink is always in the text colour.
 */
void
_frfixmargin(Frame *f)
{
	Frbox *b;
	Point pt;
	Rectangle lr, rr;
	int nb, h, ticked;

	if((f->lmargin<=0 && f->rmargin<=0) || f->b==nil || f->noredraw)
		return;
	ticked = f->ticked;	/* the tick can lie in the left margin */
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	lr = f->r;
	lr.max.x = lr.min.x;
	lr.min.x -= f->lmargin;
	draw(f->b, lr, f->cols[BACK], nil, ZP);
	rr = f->r;
	rr.min.x = rr.max.x;
	rr.max.x += f->rmargin;
	draw(f->b, rr, f->cols[BACK], nil, ZP);
	h = f->font->height;
	pt = f->r.min;
	for(nb=0,b=f->box; nb<f->nbox; nb++,b++){
		_frcklinewrap(f, &pt, b);
		if(pt.y >= f->r.max.y)
			break;
		if(b->nrune > 0){
			if(pt.x < f->r.min.x+f->lmargin)
				_string(f->b, pt, f->cols[TEXT], ZP, f->font, (char*)b->ptr, nil, 1<<24,
					Rect(lr.min.x, pt.y, lr.max.x, pt.y+h), nil, ZP, SoverD);
			if(pt.x+b->wid+f->rmargin > f->r.max.x)
				_string(f->b, pt, f->cols[TEXT], ZP, f->font, (char*)b->ptr, nil, 1<<24,
					Rect(rr.min.x, pt.y, rr.max.x, pt.y+h), nil, ZP, SoverD);
		}
		_fradvance(f, &pt, b);
	}
	/* the server flushes only the area a string names, not its overhang */
	draw(f->b, lr, f->b, nil, lr.min);
	draw(f->b, rr, f->b, nil, rr.min);
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
}

static void
_frtick(Frame *f, Point pt, int ticked)
{
	Rectangle r;

	if(f->ticked==ticked || f->tick==0 || !ptinrect(pt, f->r))
		return;
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
}

void
frtick(Frame *f, Point pt, int ticked)
{
	if(f->tickscale != scalesize(f->display, 1)) {
		if(f->ticked)
			_frtick(f, pt, 0);
		frinittick(f);
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
