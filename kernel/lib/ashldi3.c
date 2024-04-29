#include <alinix/kernel.h>
#include <alinix/libgcc.h>

long long  __ashldi3(long long u, int b){
    DWunion uu, w;
    int bm;

    if (b == 0)
        return u;
    uu.ll = u;
    bm = 32 - b;
    if (bm <= 0) {
		w.s.low = 0;
		w.s.high = (unsigned int) uu.s.low << -bm;
	} else {
		const unsigned int carries = (unsigned int) uu.s.low >> bm;

		w.s.low = (unsigned int) uu.s.low << b;
		w.s.high = ((unsigned int) uu.s.high << b) | carries;
	}

	return w.ll;
}