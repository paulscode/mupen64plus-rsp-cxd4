#include "vu.h"

INLINE void do_xor(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] ^ VT[i];
    vector_copy(VD, VACC_L);
    return;
}

static void VXOR(void)
{
    short ST[N];
    const int vd = inst.R.sa;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_xor(VR[vd], VR[vs], ST);
    return;
}
