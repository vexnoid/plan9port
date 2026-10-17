#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

Point
_frptofcharptb(Frame *f, ulong p, Point pt, int bn)
{
	uchar *s;
	Frbox *b;
	int w, l;
	Rune r;
	Font *bfont;

	f->curlineht = f->font->height;
	for(b = &f->box[bn]; bn<f->nbox; bn++,b++){
		_frcklinewrap(f, &pt, b);
		if(p < (l=NRUNE(b))){
			if(b->nrune > 0){
				bfont = BOXFONT(f, b);
				for(s=b->ptr; p>0; s+=w, p--){
					if((r = *s) < Runeself)
						w = 1;
					else
						w = chartorune(&r, (char*)s);
					pt.x += stringnwidth(bfont, (char*)s, 1);
					if(r==0 || pt.x>f->r.max.x)
						drawerror(f->display, "frptofchar");
				}
			}
			break;
		}
		p -= l;
		_fradvance(f, &pt, b);
	}
	return pt;
}

Point
frptofchar(Frame *f, ulong p)
{
	return _frptofcharptb(f, p, f->r.min, 0);
}

Point
_frptofcharnb(Frame *f, ulong p, int nb)	/* doesn't do final _fradvance to next line */
{
	Point pt;
	int nbox;

	nbox = f->nbox;
	f->nbox = nb;
	pt = _frptofcharptb(f, p, f->r.min, 0);
	f->nbox = nbox;
	return pt;
}

ulong
frcharofpt(Frame *f, Point pt)
{
	Point qt;
	int w, bn;
	uchar *s;
	Frbox *b;
	ulong p;
	Rune r;
	Font *bfont;
	Rectangle lr;

	if(pt.y < f->r.min.y)
		return 0;
	if(pt.x > f->r.max.x)
		pt.x = f->r.max.x;
	lr = _frfindline(f, pt, &bn, &p);
	qt = lr.min;
	f->curlineht = Dy(lr);
	b = &f->box[bn];
	for(; bn<f->nbox && qt.x<=pt.x; bn++,b++){
		_frcklinewrap(f, &qt, b);
		if(qt.y > pt.y)
			break;
		if(qt.x+b->wid > pt.x){
			if(b->nrune < 0)
				_fradvance(f, &qt, b);
			else{
				bfont = BOXFONT(f, b);
				s = b->ptr;
				for(;;){
					if((r = *s) < Runeself)
						w = 1;
					else
						w = chartorune(&r, (char*)s);
					if(r == 0)
						drawerror(f->display, "end of string in frcharofpt");
					qt.x += stringnwidth(bfont, (char*)s, 1);
					s += w;
					if(qt.x > pt.x)
						break;
					p++;
				}
			}
		}else{
			p += NRUNE(b);
			_fradvance(f, &qt, b);
		}
	}
	return p;
}

int
frliney(Frame *f, int n)
{
	int bn, y;

	y = f->r.min.y;
	for(bn=0; n>0 && bn<f->nbox; n--)
		y += _frlinescan(f, bn, &bn);
	/* lines below the text are one font tall */
	return y+n*f->font->height;
}

int
frlineofy(Frame *f, int y)
{
	int bn, n, h;

	y -= f->r.min.y;
	for(n=0,bn=0; bn<f->nbox; n++){
		h = _frlinescan(f, bn, &bn);
		if(y < h)
			return n;
		y -= h;
	}
	return n+y/f->font->height;
}

int
frtextlines(Frame *f)
{
	int h;

	h = f->font->height;
	/* round up to whole lines of the frame font's height */
	return (frliney(f, f->nlines)-f->r.min.y+h-1)/h;
}
