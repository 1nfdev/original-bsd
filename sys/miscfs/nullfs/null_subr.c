/*
 * Copyright (c) 1992 The Regents of the University of California
 * Copyright (c) 1990, 1992 Jan-Simon Pendry
 * All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)lofs_subr.c	1.2 (Berkeley) 6/18/92
 *
 * $Id: lofs_subr.c,v 1.11 1992/05/30 10:05:43 jsp Exp jsp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <nullfs/null.h>

#define LOG2_SIZEVNODE 7		/* log2(sizeof struct vnode) */
#define	NNULLNODECACHE 16
#define	NULL_NHASH(vp) ((((u_long)vp)>>LOG2_SIZEVNODE) & (NNULLNODECACHE-1))

/*
 * Null layer cache:
 * Each cache entry holds a reference to the lower vnode
 * along with a pointer to the alias vnode.  When an
 * entry is added the lower vnode is VREF'd.  When the
 * alias is removed the lower vnode is vrele'd.
 */

/*
 * Cache head
 */
struct null_node_cache {
	struct null_node	*ac_forw;
	struct null_node	*ac_back;
};

static struct null_node_cache null_node_cache[NNULLNODECACHE];

/*
 * Initialise cache headers
 */
nullfs_init()
{
	struct null_node_cache *ac;
#ifdef NULLFS_DIAGNOSTIC
	printf("nullfs_init\n");		/* printed during system boot */
#endif

	for (ac = null_node_cache; ac < null_node_cache + NNULLNODECACHE; ac++)
		ac->ac_forw = ac->ac_back = (struct null_node *) ac;
}

/*
 * Compute hash list for given lower vnode
 */
static struct null_node_cache *
null_node_hash(lowervp)
struct vnode *lowervp;
{
	return (&null_node_cache[NULL_NHASH(lowervp)]);
}

/*
 * Make a new null_node node.
 * Vp is the alias vnode, lofsvp is the lower vnode.
 * Maintain a reference to (lowervp).
 */
static void
null_node_alloc(vp, lowervp)
	struct vnode *vp;
	struct vnode *lowervp;
{
	struct null_node_cache *hd;
	struct null_node *a;

#ifdef NULLFS_DIAGNOSTIC
	printf("null_node_alloc(%x, %x)\n", vp, lowervp);
#endif

	MALLOC(a, struct null_node *, sizeof(struct null_node), M_TEMP, M_WAITOK);
	vp->v_type = lowervp->v_type;
	a->null_vnode = vp;
	vp->v_data = a;
	VREF(lowervp);   /* Extra VREF will be vrele'd in null_node_create */
	a->null_lowervp = lowervp;
	hd = null_node_hash(lowervp);
	insque(a, hd);

#ifdef NULLFS_DIAGNOSTIC
	vprint("null_node_alloc vp", vp);
	vprint("null_node_alloc lowervp", lowervp);
#endif
}

#ifdef NULLFS_DIAGNOSTIC
/*
 * NEEDSWORK:  The ability to set lowervp to null here
 * implies that one can never count on lowervp staying null
 * (even if vp is locked).  This seems quite bad.  Think
 * about these things.
 */
void
null_node_flushmp (mp)
	struct mount *mp;
{
	struct null_node_cache *ac;
	int i = 0;
	struct null_node *roota;

	printf("null_node_flushmp (%x)\n", mp);

	roota = VTONULL(MOUNTTONULLMOUNT(mp)->nullm_rootvp);

	for (ac = null_node_cache; ac < null_node_cache + NNULLNODECACHE; ac++) {
		struct null_node *a = ac->ac_forw;
		while (a != (struct null_node *) ac) {
			if (a != roota && NULLTOV(a)->v_mount == mp) {
				struct vnode *vp = a->null_lowervp;
				if (vp) {
					a->null_lowervp = 0;
					vprint("null_flushmp: would vrele", vp);
					/*vrele(vp);*/
					i++;
				}
			}
			a = a->null_forw;
		}
	}
	if (i > 0)
		printf("null_node: vrele'd %d aliases\n", i);
}
#endif

/*
 * XXX - this should go elsewhere.
 * Just like vget, but with no lock at the end.
 */
int
vget_nolock(vp)
	register struct vnode *vp;
{
	extern struct vnode *vfreeh, **vfreet;
	register struct vnode *vq;

	if (vp->v_flag & VXLOCK) {
		vp->v_flag |= VXWANT;
		sleep((caddr_t)vp, PINOD);
		return (1);
	}
	if (vp->v_usecount == 0) {
		if (vq = vp->v_freef)
			vq->v_freeb = vp->v_freeb;
		else
			vfreet = vp->v_freeb;
		*vp->v_freeb = vq;
		vp->v_freef = NULL;
		vp->v_freeb = NULL;
	}
	VREF(vp);
	/* VOP_LOCK(vp); */
	return (0);
}


/*
 * Return a VREF'ed alias for lower vnode if already exists, else 0.
 */
static struct vnode *
null_node_find(mp, lowervp)
	struct mount *mp;
	struct vnode *lowervp;
{
	struct null_node_cache *hd;
	struct null_node *a;
	struct vnode *vp;

#ifdef NULLFS_DIAGNOSTIC
	printf("null_node_find(mp = %x, lower = %x)\n", mp, lowervp);
#endif

	/*
	 * Find hash base, and then search the (two-way) linked
	 * list looking for a null_node structure which is referencing
	 * the lower vnode.  If found, the increment the null_node
	 * reference count (but NOT the lower vnode's VREF counter).
	 */
	hd = null_node_hash(lowervp);
loop:
	for (a = hd->ac_forw; a != (struct null_node *) hd; a = a->null_forw) {
		if (a->null_lowervp == lowervp && NULLTOV(a)->v_mount == mp) {
#ifdef NULLFS_DIAGNOSTIC
			printf("null_node_find(%x): found (%x,%x)->%x\n",
				lowervp, mp, NULLTOV(a), lowervp);
#endif
			vp = NULLTOV(a);
			/*
			 * NEEDSWORK: Don't call the normal vget,
			 * it will do a VOP_LOCK which is bypassed
			 * and will lock against ourselves.
			 * Unfortunately, we need vget for the VXLOCK
			 * stuff.
			 */
			if (vget_nolock(vp)) {
				printf ("null_node_find: vget failed.\n");
				goto loop;
			};
			return (vp);
		}
	}

#ifdef NULLFS_DIAGNOSTIC
	printf("null_node_find(%x, %x): NOT found\n", mp, lowervp);
#endif

	return NULL;
}

#if 1
int null_node_create_barrier = 1;
#endif

/*
 * Try to find an existing null_node vnode refering
 * to it, otherwise make a new null_node vnode which
 * contains a reference to the lower vnode.
 */
int
null_node_create(mp, lowervp, newvpp)
	struct mount *mp;
	struct vnode *lowervp;
	struct vnode **newvpp;
{
	struct vnode *aliasvp;

	if (aliasvp = null_node_find(mp, lowervp)) {
		/*
		 * null_node_find has taken another reference
		 * to the alias vnode.
		 */
#ifdef NULLFS_DIAGNOSTIC
		vprint("null_node_create: exists", NULLTOV(ap));
#endif
		/* VREF(aliasvp); --- done in null_node_find */
	} else {
		int error;

		/*
		 * Get new vnode.
		 */
#ifdef NULLFS_DIAGNOSTIC
		printf("null_node_create: create new alias vnode\n");
#endif
		if (error = getnewvnode(VT_UFS, mp, null_vnodeop_p, &aliasvp))
			return (error);	/* XXX: VT_NULL above */

		/*
		 * Make new vnode reference the null_node.
		 */
		null_node_alloc(aliasvp, lowervp);

		/*
		 * aliasvp is already VREF'd by getnewvnode()
		 */
	}

	vrele(lowervp);

	if (lowervp->v_usecount < 1) {
		vprint ("null_node_create: alias ");
		vprint ("null_node_create: lower ");
		printf ("null_node_create: lower has 0 usecount.\n");
		while (null_node_create_barrier) /* wait */ ;
		panic ("null_node_create: lower has 0 usecount.");
	};

#ifdef NULLFS_DIAGNOSTIC
	vprint("null_node_create: alias", aliasvp);
	vprint("null_node_create: lower", lowervp);
#endif

	*newvpp = aliasvp;
	return (0);
}
#ifdef NULLFS_DIAGNOSTIC
int null_checkvp_barrier = 1;
struct vnode *
null_checkvp(vp, fil, lno)
	struct vnode *vp;
	char *fil;
	int lno;
{
	struct null_node *a = VTONULL(vp);
#if 0
	/*
	 * Can't do this check because vop_reclaim runs
	 * with a funny vop vector.
	 */
	if (vp->v_op != null_vnodeop_p) {
		printf ("null_checkvp: on non-null-node\n");
		while (null_checkvp_barrier) /*WAIT*/ ;
		panic("null_checkvp");
	};
#endif
	if (a->null_lowervp == NULL) {
		/* Should never happen */
		int i; u_long *p;
		printf("vp = %x, ZERO ptr\n", vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %x", p[i]);
		printf("\n");
		/* wait for debugger */
		while (null_checkvp_barrier) /*WAIT*/ ;
		panic("null_checkvp");
	}
	if (a->null_lowervp->v_usecount < 1) {
		int i; u_long *p;
		printf("vp = %x, unref'ed lowervp\n", vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %x", p[i]);
		printf("\n");
		/* wait for debugger */
		while (null_checkvp_barrier) /*WAIT*/ ;
		panic ("null with unref'ed lowervp");
	};
#if 0
	printf("null %x/%d -> %x/%d [%s, %d]\n",
	        NULLTOV(a), NULLTOV(a)->v_usecount,
		a->null_lowervp, a->null_lowervp->v_usecount,
		fil, lno);
#endif
	return a->null_lowervp;
}
#endif
