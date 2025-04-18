"""
A full working spin-orbital CCSD code generated with pdaggerq

If you want to run the example here you should install pyscf openfermion and openfermion-pyscf
The actual CCSD code (ccsd_energy, singles_residual, doubles_res, kernel)
do not depend on those packages but you gotta get integrals frome somehwere.

We also check the code by using pyscfs functionality for generating spin-orbital
t-amplitudes from RCCSD.  the main() function is fairly straightforward.
"""
import numpy as np
from numpy import einsum


def ccsd_energy(t1, t2, f, g, o, v):
    """
    < 0 | e(-T) H e(T) | 0> :

    :param f:
    :param g:
    :param t1:
    :param t2:
    :param o:
    :param v:
    :return:
    """
    #	  1.0000 f(i,i)
    energy = 1.0 * einsum('ii', f[o, o])

    #	  1.0000 f(i,a)*t1(a,i)
    energy += 1.0 * einsum('ia,ai', f[o, v], t1)

    #	 -0.5000 <j,i||j,i>
    energy += -0.5 * einsum('jiji', g[o, o, o, o])

    #	  0.2500 <j,i||a,b>*t2(a,b,j,i)
    energy += 0.25 * einsum('jiab,abji', g[o, o, v, v], t2)

    #	 -0.5000 <j,i||a,b>*t1(a,i)*t1(b,j)
    energy += -0.5 * einsum('jiab,ai,bj', g[o, o, v, v], t1, t1,
                            optimize=['einsum_path', (0, 1), (0, 1)])

    return energy

def integral_maps(f, eri, o, v):
    eri_map = {}

    eri_map["oooo"] = eri[o,o,o,o]
    eri_map["oovo"] = eri[o,o,v,o]
    eri_map["oovv"] = eri[o,o,v,v]
    eri_map["vooo"] = eri[v,o,o,o]
    eri_map["vovo"] = eri[v,o,v,o]
    eri_map["vovv"] = eri[v,o,v,v]
    eri_map["vvoo"] = eri[v,v,o,o]
    eri_map["vvvo"] = eri[v,v,v,o]
    eri_map["vvvv"] = eri[v,v,v,v]

    f_map = {}
    f_map["oo"] = f[o,o]
    f_map["ov"] = f[o,v]
    f_map["vo"] = f[v,o]
    f_map["vv"] = f[v,v]

    return f_map, eri_map

def residuals(t1, t2, f, eri):
    tmps_ = {}

    # rt2  = +1.00 <a,b||i,j>
    rt2  = 1.00 * np.einsum('abij->abij',eri["vvoo"])

    # rt1  = +1.00 f(a,i)
    rt1  = 1.00 * np.einsum('ai->ai',f["vo"])

    # rt1 += -1.00 f(j,i) t1(a,j)
    # flops: o1v1 += o2v1
    #  mems: o1v1 += o1v1
    rt1 -= einsum('ia->ai', np.einsum('ji,aj->ia',f["oo"],t1) )

    # rt1 += +1.00 f(a,b) t1(b,i)
    # flops: o1v1 += o1v2
    #  mems: o1v1 += o1v1
    rt1 += np.einsum('ab,bi->ai',f["vv"],t1)

    # rt1 += -1.00 f(j,b) t2(b,a,i,j)
    # flops: o1v1 += o2v2
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('jb,baij->ai',f["ov"],t2)

    # rt1 += +1.00 <j,a||b,i> t1(b,j)
    # flops: o1v1 += o2v2
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('ajbi,bj->ai',eri["vovo"],t1)

    # rt1 += -0.50 <k,j||b,i> t2(b,a,k,j)
    # flops: o1v1 += o3v2
    #  mems: o1v1 += o1v1
    rt1 += 0.50 * einsum('ia->ai', np.einsum('jkbi,bakj->ia',eri["oovo"],t2) )

    # rt1 += -0.50 <j,a||b,c> t2(b,c,i,j)
    # flops: o1v1 += o2v3
    #  mems: o1v1 += o1v1
    rt1 += 0.50 * np.einsum('ajbc,bcij->ai',eri["vovv"],t2)

    # rt1 += +0.50 <k,j||b,c> t1(a,j) t2(b,c,i,k)
    # flops: o1v1 += o3v2 o2v1
    #  mems: o1v1 += o2v0 o1v1
    rt1 -= 0.50 * einsum('ia->ai', np.einsum('jkbc,bcik,aj->ia',eri["oovv"],t2,t1,optimize='optimal') )

    # rt2 += +1.00 P(a,b) <k,a||i,j> t1(b,k)
    # flops: o2v2 += o3v2
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('aijb->abij', np.einsum('akij,bk->aijb',eri["vooo"],t1) )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += -1.00 P(i,j) f(k,j) t2(a,b,i,k)
    # flops: o2v2 += o3v2
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('jabi->abij', np.einsum('kj,abik->jabi',f["oo"],t2) )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +1.00 P(i,j) <a,b||c,j> t1(c,i)
    # flops: o2v2 += o2v3
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('abji->abij', np.einsum('abcj,ci->abji',eri["vvvo"],t1) )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +1.00 P(a,b) f(a,c) t2(c,b,i,j)
    # flops: o2v2 += o2v3
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * np.einsum('ac,cbij->abij',f["vv"],t2)
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +0.50 <l,k||i,j> t2(a,b,l,k)
    # flops: o2v2 += o4v2
    #  mems: o2v2 += o2v2
    rt2 -= 0.50 * einsum('ijab->abij', np.einsum('klij,ablk->ijab',eri["oooo"],t2) )

    # rt2 += +1.00 P(i,j) P(a,b) <k,a||c,j> t2(c,b,i,k)
    # flops: o2v2 += o3v3
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajbi->abij', np.einsum('akcj,cbik->ajbi',eri["vovo"],t2) )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    rt2 -= einsum('baji->abij', np.einsum('baji->baji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +0.50 <a,b||c,d> t2(c,d,i,j)
    # flops: o2v2 += o2v4
    #  mems: o2v2 += o2v2
    rt2 += 0.50 * np.einsum('abcd,cdij->abij',eri["vvvv"],t2)

    # rt2 += -1.00 <l,k||i,j> t1(a,k) t1(b,l)
    # flops: o2v2 += o4v1 o3v2
    #  mems: o2v2 += o3v1 o2v2
    rt2 += einsum('bija->abij', np.einsum('bl,klij,ak->bija',t1,eri["oooo"],t1,optimize='optimal') )

    # rt2 += -0.50 P(i,j) <l,k||c,d> t2(a,b,i,l) t2(c,d,j,k)
    # flops: o2v2 += o3v2 o3v2
    #  mems: o2v2 += o2v0 o2v2
    tmps_["perm_vvoo"]  = 0.50 * einsum('jabi->abij', np.einsum('klcd,cdjk,abil->jabi',eri["oovv"],t2,t2,optimize='optimal') )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += -1.00 P(a,b) f(k,c) t1(a,k) t2(c,b,i,j)
    # flops: o2v2 += o3v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('bija->abij', np.einsum('kc,cbij,ak->bija',f["ov"],t2,t1,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +1.00 P(i,j) P(a,b) <k,a||c,j> t1(b,k) t1(c,i)
    # flops: o2v2 += o3v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajib->abij', np.einsum('akcj,ci,bk->ajib',eri["vovo"],t1,t1,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    rt2 -= einsum('baji->abij', np.einsum('baji->baji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += -0.50 <l,k||c,d> t2(c,a,l,k) t2(d,b,i,j)
    # flops: o2v2 += o2v3 o2v3
    #  mems: o2v2 += o0v2 o2v2
    rt2 += 0.50 * np.einsum('klcd,calk,dbij->abij',eri["oovv"],t2,t2,optimize='optimal')

    # rt2 += -0.50 <l,k||c,d> t2(c,a,i,j) t2(d,b,l,k)
    # flops: o2v2 += o2v3 o2v3
    #  mems: o2v2 += o0v2 o2v2
    rt2 += 0.50 * einsum('baij->abij', np.einsum('klcd,dblk,caij->baij',eri["oovv"],t2,t2,optimize='optimal') )

    # rt2 += -1.00 <a,b||c,d> t1(c,j) t1(d,i)
    # flops: o2v2 += o1v4 o2v3
    #  mems: o2v2 += o1v3 o2v2
    rt2 -= einsum('abji->abij', np.einsum('abcd,cj,di->abji',eri["vvvv"],t1,t1,optimize='optimal') )

    # rt2 += -1.00 P(i,j) P(a,b) <l,k||c,j> t1(a,k) t2(c,b,i,l)
    # flops: o2v2 += o4v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('jbia->abij', np.einsum('klcj,cbil,ak->jbia',eri["oovo"],t2,t1,optimize='optimal') )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    rt2 -= einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    rt2 += einsum('baji->abij', np.einsum('baji->baji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +0.50 P(a,b) <k,a||c,d> t1(b,k) t2(c,d,i,j)
    # flops: o2v2 += o3v3 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 0.50 * einsum('aijb->abij', np.einsum('akcd,cdij,bk->aijb',eri["vovv"],t2,t1,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += +1.00 P(i,j) <l,k||c,d> t2(c,a,j,k) t2(d,b,i,l)
    # flops: o2v2 += o3v3 o3v3
    #  mems: o2v2 += o2v2 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajbi->abij', np.einsum('klcd,cajk,dbil->ajbi',eri["oovv"],t2,t2,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # flops: o1v1  = o2v2
    #  mems: o1v1  = o1v1
    tmps_["1_ov"]  = 1.00 * np.einsum('jkbc,bj->kc',eri["oovv"],t1)

    # rt1 += +1.00 <k,j||b,c> t2(c,a,i,k) t1(b,j)
    # flops: o1v1 += o2v2
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('caik,kc->ai',t2,tmps_["1_ov"])

    # rt2 += +1.00 P(a,b) <l,k||c,d> t1(a,l) t2(d,b,i,j) t1(c,k)
    # flops: o2v2 += o3v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('bija->abij', np.einsum('dbij,ld,al->bija',t2,tmps_["1_ov"],t1,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # flops: o3v1  = o3v2
    #  mems: o3v1  = o3v1
    tmps_["2_ooov"]  = 1.00 * np.einsum('bi,jkbc->ijkc',t1,eri["oovv"])

    # rt1 += +0.50 <k,j||b,c> t2(c,a,k,j) t1(b,i)
    # flops: o1v1 += o3v2
    #  mems: o1v1 += o1v1
    rt1 -= 0.50 * np.einsum('cakj,ijkc->ai',t2,tmps_["2_ooov"])

    # rt2 += +1.00 P(i,j) P(a,b) <l,k||c,d> t1(a,k) t2(d,b,i,l) t1(c,j)
    # flops: o2v2 += o4v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('bija->abij', np.einsum('dbil,jkld,ak->bija',t2,tmps_["2_ooov"],t1,optimize='optimal') )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    rt2 -= einsum('baji->abij', np.einsum('baji->baji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # flops: o0v2  = o1v3
    #  mems: o0v2  = o0v2
    tmps_["3_vv"]  = 1.00 * np.einsum('ajbc,bj->ac',eri["vovv"],t1)

    # rt1 += +1.00 <j,a||b,c> t1(b,j) t1(c,i)
    # flops: o1v1 += o1v2
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('ac,ci->ai',tmps_["3_vv"],t1)

    # rt2 += +1.00 P(a,b) <k,a||c,d> t2(d,b,i,j) t1(c,k)
    # flops: o2v2 += o2v3
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * np.einsum('ad,dbij->abij',tmps_["3_vv"],t2)
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]
    del tmps_["3_vv"]

    # flops: o2v0  = o2v1 o3v1 o2v0
    #  mems: o2v0  = o2v0 o2v0 o2v0
    tmps_["4_oo"]  = 1.00 * np.einsum('ci,kc->ik',t1,tmps_["1_ov"])
    tmps_["4_oo"] += einsum('ki->ik', np.einsum('jkbi,bj->ki',eri["oovo"],t1) )
    del tmps_["1_ov"]

    # rt1 += +1.00 <k,j||b,c> t1(a,k) t1(b,j) t1(c,i)
    #     += +1.00 <k,j||b,i> t1(a,k) t1(b,j)
    # flops: o1v1 += o2v1
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('ak,ik->ai',t1,tmps_["4_oo"])

    # rt2 += +1.00 P(i,j) <l,k||c,d> t2(a,b,i,l) t1(c,k) t1(d,j)
    #     += +1.00 P(i,j) <l,k||c,j> t2(a,b,i,l) t1(c,k)
    # flops: o2v2 += o3v2
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * np.einsum('abil,jl->abij',t2,tmps_["4_oo"])
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]
    del tmps_["4_oo"]

    # flops: o2v0  = o2v1
    #  mems: o2v0  = o2v0
    tmps_["5_oo"]  = 1.00 * np.einsum('bi,jb->ij',t1,f["ov"])

    # rt1 += -1.00 f(j,b) t1(a,j) t1(b,i)
    # flops: o1v1 += o2v1
    #  mems: o1v1 += o1v1
    rt1 -= np.einsum('aj,ij->ai',t1,tmps_["5_oo"])

    # rt2 += -1.00 P(i,j) f(k,c) t2(a,b,i,k) t1(c,j)
    # flops: o2v2 += o3v2
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * np.einsum('abik,jk->abij',t2,tmps_["5_oo"])
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]
    del tmps_["5_oo"]

    # flops: o4v0  = o4v1
    #  mems: o4v0  = o4v0
    tmps_["6_oooo"]  = 1.00 * np.einsum('di,jkld->ijkl',t1,tmps_["2_ooov"])
    del tmps_["2_ooov"]

    # rt2 += -0.50 <l,k||c,d> t2(a,b,l,k) t1(c,j) t1(d,i)
    # flops: o2v2 += o4v2
    #  mems: o2v2 += o2v2
    rt2 += 0.50 * np.einsum('ablk,ijkl->abij',t2,tmps_["6_oooo"])

    # rt2 += +1.00 <l,k||c,d> t1(a,k) t1(b,l) t1(c,j) t1(d,i)
    # flops: o2v2 += o4v1 o3v2
    #  mems: o2v2 += o3v1 o2v2
    rt2 -= einsum('aijb->abij', np.einsum('ak,ijkl,bl->aijb',t1,tmps_["6_oooo"],t1,optimize='optimal') )
    del tmps_["6_oooo"]

    # flops: o4v0  = o4v2
    #  mems: o4v0  = o4v0
    tmps_["7_oooo"]  = 1.00 * np.einsum('cdij,klcd->ijkl',t2,eri["oovv"])

    # rt2 += +0.25 <l,k||c,d> t2(a,b,l,k) t2(c,d,i,j)
    # flops: o2v2 += o4v2
    #  mems: o2v2 += o2v2
    rt2 -= 0.25 * np.einsum('ablk,ijkl->abij',t2,tmps_["7_oooo"])

    # rt2 += -0.50 <l,k||c,d> t1(a,k) t1(b,l) t2(c,d,i,j)
    # flops: o2v2 += o4v1 o3v2
    #  mems: o2v2 += o3v1 o2v2
    rt2 += 0.50 * einsum('aijb->abij', np.einsum('ak,ijkl,bl->aijb',t1,tmps_["7_oooo"],t1,optimize='optimal') )
    del tmps_["7_oooo"]

    # flops: o2v2  = o2v3
    #  mems: o2v2  = o2v2
    tmps_["8_vovo"]  = 1.00 * np.einsum('akcd,cj->akdj',eri["vovv"],t1)

    # rt2 += -1.00 P(i,j) P(a,b) <k,a||c,d> t2(d,b,i,k) t1(c,j)
    # flops: o2v2 += o3v3
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajbi->abij', np.einsum('akdj,dbik->ajbi',tmps_["8_vovo"],t2) )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    rt2 -= einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    rt2 += einsum('baji->abij', np.einsum('baji->baji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += -1.00 P(a,b) <k,a||c,d> t1(b,k) t1(c,j) t1(d,i)
    # flops: o2v2 += o3v2 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajib->abij', np.einsum('akdj,di,bk->ajib',tmps_["8_vovo"],t1,t1,optimize='optimal') )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('baij->abij', np.einsum('baij->baij',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]
    del tmps_["8_vovo"]

    # flops: o4v0  = o4v1
    #  mems: o4v0  = o4v0
    tmps_["9_oooo"]  = 1.00 * np.einsum('klcj,ci->klji',eri["oovo"],t1)

    # rt2 += +0.50 P(i,j) <l,k||c,j> t2(a,b,l,k) t1(c,i)
    # flops: o2v2 += o4v2
    #  mems: o2v2 += o2v2
    tmps_["perm_vvoo"]  = 0.50 * einsum('abji->abij', np.einsum('ablk,klji->abji',t2,tmps_["9_oooo"]) )
    rt2 -= np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 += einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]

    # rt2 += -1.00 P(i,j) <l,k||c,j> t1(a,k) t1(b,l) t1(c,i)
    # flops: o2v2 += o4v1 o3v2
    #  mems: o2v2 += o3v1 o2v2
    tmps_["perm_vvoo"]  = 1.00 * einsum('ajib->abij', np.einsum('ak,klji,bl->ajib',t1,tmps_["9_oooo"],t1,optimize='optimal') )
    rt2 += np.einsum('abij->abij',tmps_["perm_vvoo"])
    rt2 -= einsum('abji->abij', np.einsum('abji->abji',tmps_["perm_vvoo"]) )
    del tmps_["perm_vvoo"]
    del tmps_["9_oooo"]

    return rt1, rt2

def kernel(t1, t2, fock, g, o, v, e_ai, e_abij, max_iter=100, stopping_eps=1.0E-12,
           diis_size=None, diis_start_cycle=4):

    # initialize diis if diis_size is not None
    # else normal scf iterate
    if diis_size is not None:
        from diis import DIIS
        diis_update = DIIS(diis_size, start_iter=diis_start_cycle)
        t1_dim = t1.size
        old_vec = np.hstack((t1.flatten(), t2.flatten()))

    fock_e_ai = np.reciprocal(e_ai)
    fock_e_abij = np.reciprocal(e_abij)
    old_energy = ccsd_energy(t1, t2, fock, g, o, v)
    f_map, g_map = integral_maps(fock, g, o, v)

    for idx in range(max_iter):

        singles_res, doubles_res = residuals(t1, t2, f_map, g_map)

        singles_res += fock_e_ai * t1
        doubles_res += fock_e_abij * t2

        new_singles = singles_res * e_ai
        new_doubles = doubles_res * e_abij

        # diis update
        if diis_size is not None:
            vectorized_iterate = np.hstack(
                (new_singles.flatten(), new_doubles.flatten()))
            error_vec = old_vec - vectorized_iterate
            new_vectorized_iterate = diis_update.compute_new_vec(vectorized_iterate,
                                                                 error_vec)
            new_singles = new_vectorized_iterate[:t1_dim].reshape(t1.shape)
            new_doubles = new_vectorized_iterate[t1_dim:].reshape(t2.shape)
            old_vec = new_vectorized_iterate

        current_energy = ccsd_energy(new_singles, new_doubles, fock, g, o, v)
        delta_e = np.abs(old_energy - current_energy)

        if delta_e < stopping_eps:
            return new_singles, new_doubles
        else:
            t1 = new_singles
            t2 = new_doubles
            old_energy = current_energy
            print("\tIteration {: 5d}\t{: 5.15f}\t{: 5.15f}\t{: 5.15f}\t{: 5.15f}".format(idx, old_energy, delta_e, np.linalg.norm(singles_res), np.linalg.norm(doubles_res)))
    else:
        print("Did not converge")
        return new_singles, new_doubles


def main():
    from itertools import product
    import pyscf
    import openfermion as of
    from openfermion.chem.molecular_data import spinorb_from_spatial
    from openfermionpyscf import run_pyscf
    from pyscf.cc.addons import spatial2spin
    import numpy as np


    basis = 'cc-pvdz'
    mol = pyscf.M(
        atom='H 0 0 0; B 0 0 {}'.format(1.6),
        basis=basis)

    mf = mol.RHF().run()
    mycc = mf.CCSD().run()
    print('CCSD correlation energy', mycc.e_corr)

    molecule = of.MolecularData(geometry=[['H', (0, 0, 0)], ['B', (0, 0, 1.6)]],
                                basis=basis, charge=0, multiplicity=1)
    molecule = run_pyscf(molecule, run_ccsd=True)
    oei, tei = molecule.get_integrals()
    norbs = int(mf.mo_coeff.shape[1])
    occ = mf.mo_occ
    nele = int(sum(occ))
    nocc = nele // 2
    assert np.allclose(np.transpose(mycc.t2, [1, 0, 3, 2]), mycc.t2)

    soei, stei = spinorb_from_spatial(oei, tei)
    astei = np.einsum('ijkl', stei) - np.einsum('ijlk', stei)

    # put in physics notation. OpenFermion stores <12|2'1'>
    gtei = astei.transpose(0, 1, 3, 2)

    eps = np.kron(molecule.orbital_energies, np.ones(2))
    n = np.newaxis
    o = slice(None, 2 * nocc)
    v = slice(2 * nocc, None)

    e_abij = 1 / (-eps[v, n, n, n] - eps[n, v, n, n] + eps[n, n, o, n] + eps[
        n, n, n, o])
    e_ai = 1 / (-eps[v, n] + eps[n, o])

    fock = soei + np.einsum('piiq->pq', astei[:, o, o, :])
    hf_energy = 0.5 * np.einsum('ii', (fock + soei)[o, o])
    hf_energy_test = 1.0 * einsum('ii', fock[o, o]) -0.5 * einsum('ijij', gtei[o, o, o, o])
    print(hf_energy_test, hf_energy)
    assert np.isclose(hf_energy + molecule.nuclear_repulsion, molecule.hf_energy)

    g = gtei
    nsvirt = 2 * (norbs - nocc)
    nsocc = 2 * nocc
    t1f, t2f = kernel(np.zeros((nsvirt, nsocc)), np.zeros((nsvirt, nsvirt, nsocc, nsocc)), fock, g, o, v, e_ai, e_abij,
                      diis_size=8, diis_start_cycle=4)
    print(ccsd_energy(t1f, t2f, fock, g, o, v) - hf_energy)




if __name__ == "__main__":
    main()