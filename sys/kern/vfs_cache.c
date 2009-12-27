/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Poul-Henning Kamp of the FreeBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_cache.c	8.5 (Berkeley) 3/22/95
 * $FreeBSD: src/sys/kern/vfs_cache.c,v 1.42.2.6 2001/10/05 20:07:03 dillon Exp $
 * $DragonFly: src/sys/kern/vfs_cache.c,v 1.91 2008/06/14 05:34:06 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/globaldata.h>
#include <sys/kern_syscall.h>
#include <sys/dirent.h>
#include <ddb/ddb.h>

#include <sys/sysref2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#define MAX_RECURSION_DEPTH	64

/*
 * Random lookups in the cache are accomplished with a hash table using
 * a hash key of (nc_src_vp, name).
 *
 * Negative entries may exist and correspond to structures where nc_vp
 * is NULL.  In a negative entry, NCF_WHITEOUT will be set if the entry
 * corresponds to a whited-out directory entry (verses simply not finding the
 * entry at all).
 *
 * Upon reaching the last segment of a path, if the reference is for DELETE,
 * or NOCACHE is set (rewrite), and the name is located in the cache, it
 * will be dropped.
 */

/*
 * Structures associated with name cacheing.
 */
#define NCHHASH(hash)	(&nchashtbl[(hash) & nchash])
#define MINNEG		1024

MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

LIST_HEAD(nchash_list, namecache);

struct nchash_head {
       struct nchash_list      list;
       struct spinlock         spin;
};

static struct nchash_head	*nchashtbl;
static struct namecache_list	ncneglist;
static struct spinlock		ncspin;
struct lwkt_token		vfs_token;

/*
 * ncvp_debug - debug cache_fromvp().  This is used by the NFS server
 * to create the namecache infrastructure leading to a dangling vnode.
 *
 * 0	Only errors are reported
 * 1	Successes are reported
 * 2	Successes + the whole directory scan is reported
 * 3	Force the directory scan code run as if the parent vnode did not
 *	have a namecache record, even if it does have one.
 */
static int	ncvp_debug;
SYSCTL_INT(_debug, OID_AUTO, ncvp_debug, CTLFLAG_RW, &ncvp_debug, 0, "");

static u_long	nchash;			/* size of hash table */
SYSCTL_ULONG(_debug, OID_AUTO, nchash, CTLFLAG_RD, &nchash, 0, "");

static int	ncnegfactor = 16;	/* ratio of negative entries */
SYSCTL_INT(_debug, OID_AUTO, ncnegfactor, CTLFLAG_RW, &ncnegfactor, 0, "");

static int	nclockwarn;		/* warn on locked entries in ticks */
SYSCTL_INT(_debug, OID_AUTO, nclockwarn, CTLFLAG_RW, &nclockwarn, 0, "");

static int	numneg;		/* number of cache entries allocated */
SYSCTL_INT(_debug, OID_AUTO, numneg, CTLFLAG_RD, &numneg, 0, "");

static int	numcache;		/* number of cache entries allocated */
SYSCTL_INT(_debug, OID_AUTO, numcache, CTLFLAG_RD, &numcache, 0, "");

static int	numunres;		/* number of unresolved entries */
SYSCTL_INT(_debug, OID_AUTO, numunres, CTLFLAG_RD, &numunres, 0, "");

SYSCTL_INT(_debug, OID_AUTO, vnsize, CTLFLAG_RD, 0, sizeof(struct vnode), "");
SYSCTL_INT(_debug, OID_AUTO, ncsize, CTLFLAG_RD, 0, sizeof(struct namecache), "");

static int cache_resolve_mp(struct mount *mp);
static struct vnode *cache_dvpref(struct namecache *ncp);
static void _cache_rehash(struct namecache *ncp);
static void _cache_lock(struct namecache *ncp);
static void _cache_setunresolved(struct namecache *ncp);

/*
 * The new name cache statistics
 */
SYSCTL_NODE(_vfs, OID_AUTO, cache, CTLFLAG_RW, 0, "Name cache statistics");
#define STATNODE(mode, name, var) \
	SYSCTL_ULONG(_vfs_cache, OID_AUTO, name, mode, var, 0, "");
STATNODE(CTLFLAG_RD, numneg, &numneg);
STATNODE(CTLFLAG_RD, numcache, &numcache);
static u_long numcalls; STATNODE(CTLFLAG_RD, numcalls, &numcalls);
static u_long dothits; STATNODE(CTLFLAG_RD, dothits, &dothits);
static u_long dotdothits; STATNODE(CTLFLAG_RD, dotdothits, &dotdothits);
static u_long numchecks; STATNODE(CTLFLAG_RD, numchecks, &numchecks);
static u_long nummiss; STATNODE(CTLFLAG_RD, nummiss, &nummiss);
static u_long nummisszap; STATNODE(CTLFLAG_RD, nummisszap, &nummisszap);
static u_long numposzaps; STATNODE(CTLFLAG_RD, numposzaps, &numposzaps);
static u_long numposhits; STATNODE(CTLFLAG_RD, numposhits, &numposhits);
static u_long numnegzaps; STATNODE(CTLFLAG_RD, numnegzaps, &numnegzaps);
static u_long numneghits; STATNODE(CTLFLAG_RD, numneghits, &numneghits);

struct nchstats nchstats[SMP_MAXCPU];
/*
 * Export VFS cache effectiveness statistics to user-land.
 *
 * The statistics are left for aggregation to user-land so
 * neat things can be achieved, like observing per-CPU cache
 * distribution.
 */
static int
sysctl_nchstats(SYSCTL_HANDLER_ARGS)
{
	struct globaldata *gd;
	int i, error;

	error = 0;
	for (i = 0; i < ncpus; ++i) {
		gd = globaldata_find(i);
		if ((error = SYSCTL_OUT(req, (void *)&(*gd->gd_nchstats),
			sizeof(struct nchstats))))
			break;
	}

	return (error);
}
SYSCTL_PROC(_vfs_cache, OID_AUTO, nchstats, CTLTYPE_OPAQUE|CTLFLAG_RD,
  0, 0, sysctl_nchstats, "S,nchstats", "VFS cache effectiveness statistics");

static struct namecache *cache_zap(struct namecache *ncp);

/*
 * Namespace locking.  The caller must already hold a reference to the
 * namecache structure in order to lock/unlock it.  This function prevents
 * the namespace from being created or destroyed by accessors other then
 * the lock holder.
 *
 * Note that holding a locked namecache structure prevents other threads
 * from making namespace changes (e.g. deleting or creating), prevents
 * vnode association state changes by other threads, and prevents the
 * namecache entry from being resolved or unresolved by other threads.
 *
 * The lock owner has full authority to associate/disassociate vnodes
 * and resolve/unresolve the locked ncp.
 *
 * WARNING!  Holding a locked ncp will prevent a vnode from being destroyed
 *	     or recycled, but it does NOT help you if the vnode had already
 *	     initiated a recyclement.  If this is important, use cache_get()
 *	     rather then cache_lock() (and deal with the differences in the
 *	     way the refs counter is handled).  Or, alternatively, make an
 *	     unconditional call to cache_validate() or cache_resolve()
 *	     after cache_lock() returns.
 */
static
void
_cache_lock(struct namecache *ncp)
{
	thread_t td;
	thread_t xtd;
	int didwarn;
	int error;

	KKASSERT(ncp->nc_refs != 0);
	didwarn = 0;
	td = curthread;

	for (;;) {
		xtd = ncp->nc_locktd;

		if (xtd == td) {
			++ncp->nc_exlocks;
			break;
		}
		if (xtd == NULL) {
			if (atomic_cmpset_ptr(&ncp->nc_locktd, NULL, td)) {
				KKASSERT(ncp->nc_exlocks == 0);
				ncp->nc_exlocks = 1;

				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 */
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);	/* MPSAFE */
				break;
			}
			continue;
		}

		/*
		 * Memory interlock (XXX)
		 */
		ncp->nc_lockreq = 1;
		tsleep_interlock(ncp, 0);
		cpu_mfence();
		if (xtd != ncp->nc_locktd)
			continue;
		error = tsleep(ncp, PINTERLOCKED, "clock", nclockwarn);
		if (error == EWOULDBLOCK) {
			if (didwarn)
				continue;
			didwarn = 1;
			kprintf("[diagnostic] cache_lock: blocked on %p", ncp);
			kprintf(" \"%*.*s\"\n",
				ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		}
	}

	if (didwarn == 1) {
		kprintf("[diagnostic] cache_lock: unblocked %*.*s\n",
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
	}
}

static
int
_cache_lock_nonblock(struct namecache *ncp)
{
	thread_t td;
	thread_t xtd;

	KKASSERT(ncp->nc_refs != 0);
	td = curthread;

	for (;;) {
		xtd = ncp->nc_locktd;

		if (xtd == td) {
			++ncp->nc_exlocks;
			break;
		}
		if (xtd == NULL) {
			if (atomic_cmpset_ptr(&ncp->nc_locktd, NULL, td)) {
				KKASSERT(ncp->nc_exlocks == 0);
				ncp->nc_exlocks = 1;

				/*
				 * The vp associated with a locked ncp must
				 * be held to prevent it from being recycled.
				 *
				 * WARNING!  If VRECLAIMED is set the vnode
				 * could already be in the middle of a recycle.
				 * Callers must use cache_vref() or
				 * cache_vget() on the locked ncp to
				 * validate the vp or set the cache entry
				 * to unresolved.
				 */
				if (ncp->nc_vp)
					vhold(ncp->nc_vp);	/* MPSAFE */
				break;
			}
			continue;
		}
		return(EWOULDBLOCK);
	}
	return(0);
}

/*
 * Helper function
 *
 * NOTE: nc_refs can be 0 (degenerate case during _cache_drop).
 */
static
void
_cache_unlock(struct namecache *ncp)
{
	thread_t td __debugvar = curthread;

	KKASSERT(ncp->nc_refs >= 0);
	KKASSERT(ncp->nc_exlocks > 0);
	KKASSERT(ncp->nc_locktd == td);

	if (--ncp->nc_exlocks == 0) {
		if (ncp->nc_vp)
			vdrop(ncp->nc_vp);
		ncp->nc_locktd = NULL;
		cpu_mfence();
		if (ncp->nc_lockreq) {
			ncp->nc_lockreq = 0;
			wakeup(ncp);
		}
	}
}


/*
 * cache_hold() and cache_drop() prevent the premature deletion of a
 * namecache entry but do not prevent operations (such as zapping) on
 * that namecache entry.
 *
 * This routine may only be called from outside this source module if
 * nc_refs is already at least 1.
 *
 * This is a rare case where callers are allowed to hold a spinlock,
 * so we can't ourselves.
 *
 * MPSAFE
 */
static __inline
struct namecache *
_cache_hold(struct namecache *ncp)
{
	atomic_add_int(&ncp->nc_refs, 1);
	return(ncp);
}

/*
 * Drop a cache entry, taking care to deal with races.
 *
 * For potential 1->0 transitions we must hold the ncp lock to safely
 * test its flags.  An unresolved entry with no children must be zapped
 * to avoid leaks.
 *
 * The call to cache_zap() itself will handle all remaining races and
 * will decrement the ncp's refs regardless.  If we are resolved or
 * have children nc_refs can safely be dropped to 0 without having to
 * zap the entry.
 *
 * NOTE: cache_zap() will re-check nc_refs and nc_list in a MPSAFE fashion.
 *
 * NOTE: cache_zap() may return a non-NULL referenced parent which must
 *	 be dropped in a loop.
 */
static __inline
void
_cache_drop(struct namecache *ncp)
{
	int refs;

	while (ncp) {
		KKASSERT(ncp->nc_refs > 0);
		refs = ncp->nc_refs;

		if (refs == 1) {
			if (_cache_lock_nonblock(ncp) == 0) {
				if ((ncp->nc_flag & NCF_UNRESOLVED) &&
				    TAILQ_EMPTY(&ncp->nc_list)) {
					ncp = cache_zap(ncp);
					continue;
				}
				if (atomic_cmpset_int(&ncp->nc_refs, 1, 0)) {
					_cache_unlock(ncp);
					break;
				}
				_cache_unlock(ncp);
			}
		} else {
			if (atomic_cmpset_int(&ncp->nc_refs, refs, refs - 1))
				break;
		}
	}
}

/*
 * Link a new namecache entry to its parent.  Be careful to avoid races
 * if vhold() blocks in the future.
 *
 * MPSAFE - ncp must be locked and vfs_token must be held.
 */
static void
_cache_link_parent(struct namecache *ncp, struct namecache *par)
{
	KKASSERT(ncp->nc_parent == NULL);
	ncp->nc_parent = par;
	if (TAILQ_EMPTY(&par->nc_list)) {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
		/*
		 * Any vp associated with an ncp which has children must
		 * be held to prevent it from being recycled.
		 */
		if (par->nc_vp)
			vhold(par->nc_vp);	/* MPSAFE */
	} else {
		TAILQ_INSERT_HEAD(&par->nc_list, ncp, nc_entry);
	}
}

/*
 * Remove the parent association from a namecache structure.  If this is
 * the last child of the parent the cache_drop(par) will attempt to
 * recursively zap the parent.
 *
 * MPSAFE - ncp must be locked and vfs_token must be held.
 */
static void
_cache_unlink_parent(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *dropvp;

	if ((par = ncp->nc_parent) != NULL) {
		ncp->nc_parent = NULL;
		_cache_hold(par);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		dropvp = NULL;
		if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
			dropvp = par->nc_vp;
		_cache_drop(par);

		/*
		 * We can only safely vdrop with no spinlocks held.
		 */
		if (dropvp)
			vdrop(dropvp);
	}
}

/*
 * Allocate a new namecache structure.  Most of the code does not require
 * zero-termination of the string but it makes vop_compat_ncreate() easier.
 */
static struct namecache *
cache_alloc(int nlen)
{
	struct namecache *ncp;

	ncp = kmalloc(sizeof(*ncp), M_VFSCACHE, M_WAITOK|M_ZERO);
	if (nlen)
		ncp->nc_name = kmalloc(nlen + 1, M_VFSCACHE, M_WAITOK);
	ncp->nc_nlen = nlen;
	ncp->nc_flag = NCF_UNRESOLVED;
	ncp->nc_error = ENOTCONN;	/* needs to be resolved */
	ncp->nc_refs = 1;

	TAILQ_INIT(&ncp->nc_list);
	_cache_lock(ncp);
	return(ncp);
}

/*
 * Can only be called for the case where the ncp has never been
 * associated with anything (so no spinlocks are needed).
 */
static void
_cache_free(struct namecache *ncp)
{
	KKASSERT(ncp->nc_refs == 1 && ncp->nc_exlocks == 1);
	if (ncp->nc_name)
		kfree(ncp->nc_name, M_VFSCACHE);
	kfree(ncp, M_VFSCACHE);
}

void
cache_zero(struct nchandle *nch)
{
	nch->ncp = NULL;
	nch->mount = NULL;
}

/*
 * Ref and deref a namecache structure.
 *
 * Warning: caller may hold an unrelated read spinlock, which means we can't
 * use read spinlocks here.
 *
 * MPSAFE if nch is
 */
struct nchandle *
cache_hold(struct nchandle *nch)
{
	_cache_hold(nch->ncp);
	atomic_add_int(&nch->mount->mnt_refs, 1);
	return(nch);
}

/*
 * Create a copy of a namecache handle for an already-referenced
 * entry.
 *
 * MPSAFE if nch is
 */
void
cache_copy(struct nchandle *nch, struct nchandle *target)
{
	*target = *nch;
	if (target->ncp)
		_cache_hold(target->ncp);
	atomic_add_int(&nch->mount->mnt_refs, 1);
}

/*
 * MPSAFE if nch is
 */
void
cache_changemount(struct nchandle *nch, struct mount *mp)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
	nch->mount = mp;
	atomic_add_int(&nch->mount->mnt_refs, 1);
}

void
cache_drop(struct nchandle *nch)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
	_cache_drop(nch->ncp);
	nch->ncp = NULL;
	nch->mount = NULL;
}

void
cache_lock(struct nchandle *nch)
{
	_cache_lock(nch->ncp);
}

int
cache_lock_nonblock(struct nchandle *nch)
{
	return(_cache_lock_nonblock(nch->ncp));
}


void
cache_unlock(struct nchandle *nch)
{
	_cache_unlock(nch->ncp);
}

/*
 * ref-and-lock, unlock-and-deref functions.
 *
 * This function is primarily used by nlookup.  Even though cache_lock
 * holds the vnode, it is possible that the vnode may have already
 * initiated a recyclement.
 *
 * We want cache_get() to return a definitively usable vnode or a
 * definitively unresolved ncp.
 */
static
struct namecache *
_cache_get(struct namecache *ncp)
{
	_cache_hold(ncp);
	_cache_lock(ncp);
	if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
		_cache_setunresolved(ncp);
	return(ncp);
}

/*
 * This is a special form of _cache_get() which only succeeds if
 * it can get a pristine, non-recursive lock.  The caller must have
 * already ref'd the ncp.
 *
 * On success the ncp will be locked, on failure it will not.  The
 * ref count does not change either way.
 *
 * We want _cache_get_nonblock() (on success) to return a definitively
 * usable vnode or a definitively unresolved ncp.
 */
static int
_cache_get_nonblock(struct namecache *ncp)
{
	if (_cache_lock_nonblock(ncp) == 0) {
		if (ncp->nc_exlocks == 1) {
			if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
				_cache_setunresolved(ncp);
			return(0);
		}
		_cache_unlock(ncp);
	}
	return(EWOULDBLOCK);
}


/*
 * NOTE: The same nchandle can be passed for both arguments.
 */
void
cache_get(struct nchandle *nch, struct nchandle *target)
{
	KKASSERT(nch->ncp->nc_refs > 0);
	target->mount = nch->mount;
	target->ncp = _cache_get(nch->ncp);
	atomic_add_int(&target->mount->mnt_refs, 1);
}

#if 0
int
cache_get_nonblock(struct nchandle *nch)
{
	int error;

	if ((error = _cache_get_nonblock(nch->ncp)) == 0)
		atomic_add_int(&nch->mount->mnt_refs, 1);
	return (error);
}
#endif

static __inline
void
_cache_put(struct namecache *ncp)
{
	_cache_unlock(ncp);
	_cache_drop(ncp);
}

void
cache_put(struct nchandle *nch)
{
	atomic_add_int(&nch->mount->mnt_refs, -1);
	_cache_put(nch->ncp);
	nch->ncp = NULL;
	nch->mount = NULL;
}

/*
 * Resolve an unresolved ncp by associating a vnode with it.  If the
 * vnode is NULL, a negative cache entry is created.
 *
 * The ncp should be locked on entry and will remain locked on return.
 */
static
void
_cache_setvp(struct mount *mp, struct namecache *ncp, struct vnode *vp)
{
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	if (vp != NULL) {
		/*
		 * Any vp associated with an ncp which has children must
		 * be held.  Any vp associated with a locked ncp must be held.
		 */
		if (!TAILQ_EMPTY(&ncp->nc_list))
			vhold(vp);
		spin_lock_wr(&vp->v_spinlock);
		ncp->nc_vp = vp;
		TAILQ_INSERT_HEAD(&vp->v_namecache, ncp, nc_vnode);
		spin_unlock_wr(&vp->v_spinlock);
		if (ncp->nc_exlocks)
			vhold(vp);

		/*
		 * Set auxiliary flags
		 */
		switch(vp->v_type) {
		case VDIR:
			ncp->nc_flag |= NCF_ISDIR;
			break;
		case VLNK:
			ncp->nc_flag |= NCF_ISSYMLINK;
			/* XXX cache the contents of the symlink */
			break;
		default:
			break;
		}
		atomic_add_int(&numcache, 1);
		ncp->nc_error = 0;
	} else {
		/*
		 * When creating a negative cache hit we set the
		 * namecache_gen.  A later resolve will clean out the
		 * negative cache hit if the mount point's namecache_gen
		 * has changed.  Used by devfs, could also be used by
		 * other remote FSs.
		 */
		ncp->nc_vp = NULL;
		spin_lock_wr(&ncspin);
		lwkt_token_init(&vfs_token);
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		++numneg;
		spin_unlock_wr(&ncspin);
		ncp->nc_error = ENOENT;
		if (mp)
			ncp->nc_namecache_gen = mp->mnt_namecache_gen;
	}
	ncp->nc_flag &= ~NCF_UNRESOLVED;
}

void
cache_setvp(struct nchandle *nch, struct vnode *vp)
{
	_cache_setvp(nch->mount, nch->ncp, vp);
}

void
cache_settimeout(struct nchandle *nch, int nticks)
{
	struct namecache *ncp = nch->ncp;

	if ((ncp->nc_timeout = ticks + nticks) == 0)
		ncp->nc_timeout = 1;
}

/*
 * Disassociate the vnode or negative-cache association and mark a
 * namecache entry as unresolved again.  Note that the ncp is still
 * left in the hash table and still linked to its parent.
 *
 * The ncp should be locked and refd on entry and will remain locked and refd
 * on return.
 *
 * This routine is normally never called on a directory containing children.
 * However, NFS often does just that in its rename() code as a cop-out to
 * avoid complex namespace operations.  This disconnects a directory vnode
 * from its namecache and can cause the OLDAPI and NEWAPI to get out of
 * sync.
 */
static
void
_cache_setunresolved(struct namecache *ncp)
{
	struct vnode *vp;

	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		ncp->nc_flag |= NCF_UNRESOLVED;
		ncp->nc_timeout = 0;
		ncp->nc_error = ENOTCONN;
		atomic_add_int(&numunres, 1);
		if ((vp = ncp->nc_vp) != NULL) {
			atomic_add_int(&numcache, -1);
			spin_lock_wr(&vp->v_spinlock);
			ncp->nc_vp = NULL;
			TAILQ_REMOVE(&vp->v_namecache, ncp, nc_vnode);
			spin_unlock_wr(&vp->v_spinlock);

			/*
			 * Any vp associated with an ncp with children is
			 * held by that ncp.  Any vp associated with a locked
			 * ncp is held by that ncp.  These conditions must be
			 * undone when the vp is cleared out from the ncp.
			 */
			if (!TAILQ_EMPTY(&ncp->nc_list))
				vdrop(vp);
			if (ncp->nc_exlocks)
				vdrop(vp);
		} else {
			spin_lock_wr(&ncspin);
			TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
			--numneg;
			spin_unlock_wr(&ncspin);
		}
		ncp->nc_flag &= ~(NCF_WHITEOUT|NCF_ISDIR|NCF_ISSYMLINK);
	}
}

/*
 * The cache_nresolve() code calls this function to automatically
 * set a resolved cache element to unresolved if it has timed out
 * or if it is a negative cache hit and the mount point namecache_gen
 * has changed.
 */
static __inline void
_cache_auto_unresolve(struct mount *mp, struct namecache *ncp)
{
	/*
	 * Already in an unresolved state, nothing to do.
	 */
	if (ncp->nc_flag & NCF_UNRESOLVED)
		return;

	/*
	 * Try to zap entries that have timed out.  We have
	 * to be careful here because locked leafs may depend
	 * on the vnode remaining intact in a parent, so only
	 * do this under very specific conditions.
	 */
	if (ncp->nc_timeout && (int)(ncp->nc_timeout - ticks) < 0 &&
	    TAILQ_EMPTY(&ncp->nc_list)) {
		_cache_setunresolved(ncp);
		return;
	}

	/*
	 * If a resolved negative cache hit is invalid due to
	 * the mount's namecache generation being bumped, zap it.
	 */
	if (ncp->nc_vp == NULL &&
	    ncp->nc_namecache_gen != mp->mnt_namecache_gen) {
		_cache_setunresolved(ncp);
		return;
	}
}

void
cache_setunresolved(struct nchandle *nch)
{
	_cache_setunresolved(nch->ncp);
}

/*
 * Determine if we can clear NCF_ISMOUNTPT by scanning the mountlist
 * looking for matches.  This flag tells the lookup code when it must
 * check for a mount linkage and also prevents the directories in question
 * from being deleted or renamed.
 */
static
int
cache_clrmountpt_callback(struct mount *mp, void *data)
{
	struct nchandle *nch = data;

	if (mp->mnt_ncmounton.ncp == nch->ncp)
		return(1);
	if (mp->mnt_ncmountpt.ncp == nch->ncp)
		return(1);
	return(0);
}

void
cache_clrmountpt(struct nchandle *nch)
{
	int count;

	count = mountlist_scan(cache_clrmountpt_callback, nch,
			       MNTSCAN_FORWARD|MNTSCAN_NOBUSY);
	if (count == 0)
		nch->ncp->nc_flag &= ~NCF_ISMOUNTPT;
}

/*
 * Invalidate portions of the namecache topology given a starting entry.
 * The passed ncp is set to an unresolved state and:
 *
 * The passed ncp must be locked.
 *
 * CINV_DESTROY		- Set a flag in the passed ncp entry indicating
 *			  that the physical underlying nodes have been 
 *			  destroyed... as in deleted.  For example, when
 *			  a directory is removed.  This will cause record
 *			  lookups on the name to no longer be able to find
 *			  the record and tells the resolver to return failure
 *			  rather then trying to resolve through the parent.
 *
 *			  The topology itself, including ncp->nc_name,
 *			  remains intact.
 *
 *			  This only applies to the passed ncp, if CINV_CHILDREN
 *			  is specified the children are not flagged.
 *
 * CINV_CHILDREN	- Set all children (recursively) to an unresolved
 *			  state as well.
 *
 *			  Note that this will also have the side effect of
 *			  cleaning out any unreferenced nodes in the topology
 *			  from the leaves up as the recursion backs out.
 *
 * Note that the topology for any referenced nodes remains intact.
 *
 * It is possible for cache_inval() to race a cache_resolve(), meaning that
 * the namecache entry may not actually be invalidated on return if it was
 * revalidated while recursing down into its children.  This code guarentees
 * that the node(s) will go through an invalidation cycle, but does not 
 * guarentee that they will remain in an invalidated state. 
 *
 * Returns non-zero if a revalidation was detected during the invalidation
 * recursion, zero otherwise.  Note that since only the original ncp is
 * locked the revalidation ultimately can only indicate that the original ncp
 * *MIGHT* no have been reresolved.
 *
 * DEEP RECURSION HANDLING - If a recursive invalidation recurses deeply we
 * have to avoid blowing out the kernel stack.  We do this by saving the
 * deep namecache node and aborting the recursion, then re-recursing at that
 * node using a depth-first algorithm in order to allow multiple deep
 * recursions to chain through each other, then we restart the invalidation
 * from scratch.
 */

struct cinvtrack {
	struct namecache *resume_ncp;
	int depth;
};

static int _cache_inval_internal(struct namecache *, int, struct cinvtrack *);

static
int
_cache_inval(struct namecache *ncp, int flags)
{
	struct cinvtrack track;
	struct namecache *ncp2;
	int r;

	track.depth = 0;
	track.resume_ncp = NULL;

	for (;;) {
		r = _cache_inval_internal(ncp, flags, &track);
		if (track.resume_ncp == NULL)
			break;
		kprintf("Warning: deep namecache recursion at %s\n",
			ncp->nc_name);
		_cache_unlock(ncp);
		while ((ncp2 = track.resume_ncp) != NULL) {
			track.resume_ncp = NULL;
			_cache_lock(ncp2);
			_cache_inval_internal(ncp2, flags & ~CINV_DESTROY,
					     &track);
			_cache_put(ncp2);
		}
		_cache_lock(ncp);
	}
	return(r);
}

int
cache_inval(struct nchandle *nch, int flags)
{
	return(_cache_inval(nch->ncp, flags));
}

static int
_cache_inval_internal(struct namecache *ncp, int flags, struct cinvtrack *track)
{
	struct namecache *kid;
	struct namecache *nextkid;
	lwkt_tokref nlock;
	int rcnt = 0;

	KKASSERT(ncp->nc_exlocks);

	_cache_setunresolved(ncp);
	lwkt_gettoken(&nlock, &vfs_token);
	if (flags & CINV_DESTROY)
		ncp->nc_flag |= NCF_DESTROYED;
	if ((flags & CINV_CHILDREN) && 
	    (kid = TAILQ_FIRST(&ncp->nc_list)) != NULL
	) {
		_cache_hold(kid);
		if (++track->depth > MAX_RECURSION_DEPTH) {
			track->resume_ncp = ncp;
			_cache_hold(ncp);
			++rcnt;
		}
		_cache_unlock(ncp);
		while (kid) {
			if (track->resume_ncp) {
				_cache_drop(kid);
				break;
			}
			if ((nextkid = TAILQ_NEXT(kid, nc_entry)) != NULL)
				_cache_hold(nextkid);
			if ((kid->nc_flag & NCF_UNRESOLVED) == 0 ||
			    TAILQ_FIRST(&kid->nc_list)
			) {
				_cache_lock(kid);
				rcnt += _cache_inval_internal(kid, flags & ~CINV_DESTROY, track);
				_cache_unlock(kid);
			}
			_cache_drop(kid);
			kid = nextkid;
		}
		--track->depth;
		_cache_lock(ncp);
	}
	lwkt_reltoken(&nlock);

	/*
	 * Someone could have gotten in there while ncp was unlocked,
	 * retry if so.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0)
		++rcnt;
	return (rcnt);
}

/*
 * Invalidate a vnode's namecache associations.  To avoid races against
 * the resolver we do not invalidate a node which we previously invalidated
 * but which was then re-resolved while we were in the invalidation loop.
 *
 * Returns non-zero if any namecache entries remain after the invalidation
 * loop completed.
 *
 * NOTE: Unlike the namecache topology which guarentees that ncp's will not
 *	 be ripped out of the topology while held, the vnode's v_namecache
 *	 list has no such restriction.  NCP's can be ripped out of the list
 *	 at virtually any time if not locked, even if held.
 *
 *	 In addition, the v_namecache list itself must be locked via
 *	 the vnode's spinlock.
 */
int
cache_inval_vp(struct vnode *vp, int flags)
{
	struct namecache *ncp;
	struct namecache *next;

restart:
	spin_lock_wr(&vp->v_spinlock);
	ncp = TAILQ_FIRST(&vp->v_namecache);
	if (ncp)
		_cache_hold(ncp);
	while (ncp) {
		/* loop entered with ncp held and vp spin-locked */
		if ((next = TAILQ_NEXT(ncp, nc_vnode)) != NULL)
			_cache_hold(next);
		spin_unlock_wr(&vp->v_spinlock);
		_cache_lock(ncp);
		if (ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-A detected on "
				"%s\n", ncp->nc_name);
			_cache_put(ncp);
			if (next)
				_cache_drop(next);
			goto restart;
		}
		_cache_inval(ncp, flags);
		_cache_put(ncp);		/* also releases reference */
		ncp = next;
		if (ncp && ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-B detected on "
				"%s\n", ncp->nc_name);
			_cache_drop(ncp);
			goto restart;
		}
		spin_lock_wr(&vp->v_spinlock);
	}
	spin_unlock_wr(&vp->v_spinlock);
	return(TAILQ_FIRST(&vp->v_namecache) != NULL);
}

/*
 * This routine is used instead of the normal cache_inval_vp() when we
 * are trying to recycle otherwise good vnodes.
 *
 * Return 0 on success, non-zero if not all namecache records could be
 * disassociated from the vnode (for various reasons).
 */
int
cache_inval_vp_nonblock(struct vnode *vp)
{
	struct namecache *ncp;
	struct namecache *next;

	spin_lock_wr(&vp->v_spinlock);
	ncp = TAILQ_FIRST(&vp->v_namecache);
	if (ncp)
		_cache_hold(ncp);
	while (ncp) {
		/* loop entered with ncp held */
		if ((next = TAILQ_NEXT(ncp, nc_vnode)) != NULL)
			_cache_hold(next);
		spin_unlock_wr(&vp->v_spinlock);
		if (_cache_lock_nonblock(ncp)) {
			_cache_drop(ncp);
			if (next)
				_cache_drop(next);
			break;
		}
		if (ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-A detected on "
				"%s\n", ncp->nc_name);
			_cache_put(ncp);
			if (next)
				_cache_drop(next);
			break;
		}
		_cache_inval(ncp, 0);
		_cache_put(ncp);		/* also releases reference */
		ncp = next;
		if (ncp && ncp->nc_vp != vp) {
			kprintf("Warning: cache_inval_vp: race-B detected on "
				"%s\n", ncp->nc_name);
			_cache_drop(ncp);
			break;
		}
		spin_lock_wr(&vp->v_spinlock);
	}
	spin_unlock_wr(&vp->v_spinlock);
	return(TAILQ_FIRST(&vp->v_namecache) != NULL);
}

/*
 * The source ncp has been renamed to the target ncp.  Both fncp and tncp
 * must be locked.  The target ncp is destroyed (as a normal rename-over
 * would destroy the target file or directory).
 *
 * Because there may be references to the source ncp we cannot copy its
 * contents to the target.  Instead the source ncp is relinked as the target
 * and the target ncp is removed from the namecache topology.
 */
void
cache_rename(struct nchandle *fnch, struct nchandle *tnch)
{
	struct namecache *fncp = fnch->ncp;
	struct namecache *tncp = tnch->ncp;
	char *oname;
	lwkt_tokref nlock;

	lwkt_gettoken(&nlock, &vfs_token);
	_cache_setunresolved(tncp);
	_cache_unlink_parent(fncp);
	_cache_link_parent(fncp, tncp->nc_parent);
	_cache_unlink_parent(tncp);
	oname = fncp->nc_name;
	fncp->nc_name = tncp->nc_name;
	fncp->nc_nlen = tncp->nc_nlen;
	tncp->nc_name = NULL;
	tncp->nc_nlen = 0;
	if (fncp->nc_head)
		_cache_rehash(fncp);
	if (tncp->nc_head)
		_cache_rehash(tncp);
	lwkt_reltoken(&nlock);

	if (oname)
		kfree(oname, M_VFSCACHE);
}

/*
 * vget the vnode associated with the namecache entry.  Resolve the namecache
 * entry if necessary and deal with namecache/vp races.  The passed ncp must
 * be referenced and may be locked.  The ncp's ref/locking state is not 
 * effected by this call.
 *
 * lk_type may be LK_SHARED, LK_EXCLUSIVE.  A ref'd, possibly locked
 * (depending on the passed lk_type) will be returned in *vpp with an error
 * of 0, or NULL will be returned in *vpp with a non-0 error code.  The
 * most typical error is ENOENT, meaning that the ncp represents a negative
 * cache hit and there is no vnode to retrieve, but other errors can occur
 * too.
 *
 * The main race we have to deal with are namecache zaps.  The ncp itself
 * will not disappear since it is referenced, and it turns out that the
 * validity of the vp pointer can be checked simply by rechecking the
 * contents of ncp->nc_vp.
 */
int
cache_vget(struct nchandle *nch, struct ucred *cred,
	   int lk_type, struct vnode **vpp)
{
	struct namecache *ncp;
	struct vnode *vp;
	int error;

	ncp = nch->ncp;
again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED) {
		_cache_lock(ncp);
		error = cache_resolve(nch, cred);
		_cache_unlock(ncp);
	} else {
		error = 0;
	}
	if (error == 0 && (vp = ncp->nc_vp) != NULL) {
		/*
		 * Accessing the vnode from the namecache is a bit 
		 * dangerous.  Because there are no refs on the vnode, it
		 * could be in the middle of a reclaim.
		 */
		if (vp->v_flag & VRECLAIMED) {
			kprintf("Warning: vnode reclaim race detected in cache_vget on %p (%s)\n", vp, ncp->nc_name);
			_cache_lock(ncp);
			_cache_setunresolved(ncp);
			_cache_unlock(ncp);
			goto again;
		}
		error = vget(vp, lk_type);
		if (error) {
			if (vp != ncp->nc_vp)
				goto again;
			vp = NULL;
		} else if (vp != ncp->nc_vp) {
			vput(vp);
			goto again;
		} else if (vp->v_flag & VRECLAIMED) {
			panic("vget succeeded on a VRECLAIMED node! vp %p", vp);
		}
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;
	return(error);
}

int
cache_vref(struct nchandle *nch, struct ucred *cred, struct vnode **vpp)
{
	struct namecache *ncp;
	struct vnode *vp;
	int error;

	ncp = nch->ncp;

again:
	vp = NULL;
	if (ncp->nc_flag & NCF_UNRESOLVED) {
		_cache_lock(ncp);
		error = cache_resolve(nch, cred);
		_cache_unlock(ncp);
	} else {
		error = 0;
	}
	if (error == 0 && (vp = ncp->nc_vp) != NULL) {
		/*
		 * Since we did not obtain any locks, a cache zap 
		 * race can occur here if the vnode is in the middle
		 * of being reclaimed and has not yet been able to
		 * clean out its cache node.  If that case occurs,
		 * we must lock and unresolve the cache, then loop
		 * to retry.
		 */
		if ((error = vget(vp, LK_SHARED)) != 0) {
			if (error == ENOENT) {
				kprintf("Warning: vnode reclaim race detected on cache_vref %p (%s)\n", vp, ncp->nc_name);
				_cache_lock(ncp);
				_cache_setunresolved(ncp);
				_cache_unlock(ncp);
				goto again;
			}
			/* fatal error */
		} else {
			/* caller does not want a lock */
			vn_unlock(vp);
		}
	}
	if (error == 0 && vp == NULL)
		error = ENOENT;
	*vpp = vp;
	return(error);
}

/*
 * Return a referenced vnode representing the parent directory of
 * ncp.
 *
 * Because the caller has locked the ncp it should not be possible for
 * the parent ncp to go away.  However, the parent can unresolve its
 * dvp at any time so we must be able to acquire a lock on the parent
 * to safely access nc_vp.
 *
 * We have to leave par unlocked when vget()ing dvp to avoid a deadlock,
 * so use vhold()/vdrop() while holding the lock to prevent dvp from
 * getting destroyed.
 */
static struct vnode *
cache_dvpref(struct namecache *ncp)
{
	struct namecache *par;
	struct vnode *dvp;

	dvp = NULL;
	if ((par = ncp->nc_parent) != NULL) {
		_cache_hold(par);
		if (_cache_lock_nonblock(par) == 0) {
			if ((par->nc_flag & NCF_UNRESOLVED) == 0) {
				if ((dvp = par->nc_vp) != NULL)
					vhold(dvp);
			}
			_cache_unlock(par);
			if (dvp) {
				if (vget(dvp, LK_SHARED) == 0) {
					vn_unlock(dvp);
					vdrop(dvp);
					/* return refd, unlocked dvp */
				} else {
					vdrop(dvp);
					dvp = NULL;
				}
			}
		}
		_cache_drop(par);
	}
	return(dvp);
}

/*
 * Convert a directory vnode to a namecache record without any other 
 * knowledge of the topology.  This ONLY works with directory vnodes and
 * is ONLY used by the NFS server.  dvp must be refd but unlocked, and the
 * returned ncp (if not NULL) will be held and unlocked.
 *
 * If 'makeit' is 0 and dvp has no existing namecache record, NULL is returned.
 * If 'makeit' is 1 we attempt to track-down and create the namecache topology
 * for dvp.  This will fail only if the directory has been deleted out from
 * under the caller.  
 *
 * Callers must always check for a NULL return no matter the value of 'makeit'.
 *
 * To avoid underflowing the kernel stack each recursive call increments
 * the makeit variable.
 */

static int cache_inefficient_scan(struct nchandle *nch, struct ucred *cred,
				  struct vnode *dvp, char *fakename);
static int cache_fromdvp_try(struct vnode *dvp, struct ucred *cred, 
				  struct vnode **saved_dvp);

int
cache_fromdvp(struct vnode *dvp, struct ucred *cred, int makeit,
	      struct nchandle *nch)
{
	struct vnode *saved_dvp;
	struct vnode *pvp;
	char *fakename;
	int error;

	nch->ncp = NULL;
	nch->mount = dvp->v_mount;
	saved_dvp = NULL;
	fakename = NULL;

	/*
	 * Loop until resolution, inside code will break out on error.
	 */
	while (makeit) {
		/*
		 * Break out if we successfully acquire a working ncp.
		 */
		spin_lock_wr(&dvp->v_spinlock);
		nch->ncp = TAILQ_FIRST(&dvp->v_namecache);
		if (nch->ncp) {
			cache_hold(nch);
			spin_unlock_wr(&dvp->v_spinlock);
			break;
		}
		spin_unlock_wr(&dvp->v_spinlock);

		/*
		 * If dvp is the root of its filesystem it should already
		 * have a namecache pointer associated with it as a side 
		 * effect of the mount, but it may have been disassociated.
		 */
		if (dvp->v_flag & VROOT) {
			nch->ncp = _cache_get(nch->mount->mnt_ncmountpt.ncp);
			error = cache_resolve_mp(nch->mount);
			_cache_put(nch->ncp);
			if (ncvp_debug) {
				kprintf("cache_fromdvp: resolve root of mount %p error %d", 
					dvp->v_mount, error);
			}
			if (error) {
				if (ncvp_debug)
					kprintf(" failed\n");
				nch->ncp = NULL;
				break;
			}
			if (ncvp_debug)
				kprintf(" succeeded\n");
			continue;
		}

		/*
		 * If we are recursed too deeply resort to an O(n^2)
		 * algorithm to resolve the namecache topology.  The
		 * resolved pvp is left referenced in saved_dvp to
		 * prevent the tree from being destroyed while we loop.
		 */
		if (makeit > 20) {
			error = cache_fromdvp_try(dvp, cred, &saved_dvp);
			if (error) {
				kprintf("lookupdotdot(longpath) failed %d "
				       "dvp %p\n", error, dvp);
				nch->ncp = NULL;
				break;
			}
			continue;
		}

		/*
		 * Get the parent directory and resolve its ncp.
		 */
		if (fakename) {
			kfree(fakename, M_TEMP);
			fakename = NULL;
		}
		error = vop_nlookupdotdot(*dvp->v_ops, dvp, &pvp, cred,
					  &fakename);
		if (error) {
			kprintf("lookupdotdot failed %d dvp %p\n", error, dvp);
			break;
		}
		vn_unlock(pvp);

		/*
		 * Reuse makeit as a recursion depth counter.  On success
		 * nch will be fully referenced.
		 */
		cache_fromdvp(pvp, cred, makeit + 1, nch);
		vrele(pvp);
		if (nch->ncp == NULL)
			break;

		/*
		 * Do an inefficient scan of pvp (embodied by ncp) to look
		 * for dvp.  This will create a namecache record for dvp on
		 * success.  We loop up to recheck on success.
		 *
		 * ncp and dvp are both held but not locked.
		 */
		error = cache_inefficient_scan(nch, cred, dvp, fakename);
		if (error) {
			kprintf("cache_fromdvp: scan %p (%s) failed on dvp=%p\n",
				pvp, nch->ncp->nc_name, dvp);
			cache_drop(nch);
			/* nch was NULLed out, reload mount */
			nch->mount = dvp->v_mount;
			break;
		}
		if (ncvp_debug) {
			kprintf("cache_fromdvp: scan %p (%s) succeeded\n",
				pvp, nch->ncp->nc_name);
		}
		cache_drop(nch);
		/* nch was NULLed out, reload mount */
		nch->mount = dvp->v_mount;
	}

	/*
	 * If nch->ncp is non-NULL it will have been held already.
	 */
	if (fakename)
		kfree(fakename, M_TEMP);
	if (saved_dvp)
		vrele(saved_dvp);
	if (nch->ncp)
		return (0);
	return (EINVAL);
}

/*
 * Go up the chain of parent directories until we find something
 * we can resolve into the namecache.  This is very inefficient.
 */
static
int
cache_fromdvp_try(struct vnode *dvp, struct ucred *cred,
		  struct vnode **saved_dvp)
{
	struct nchandle nch;
	struct vnode *pvp;
	int error;
	static time_t last_fromdvp_report;
	char *fakename;

	/*
	 * Loop getting the parent directory vnode until we get something we
	 * can resolve in the namecache.
	 */
	vref(dvp);
	nch.mount = dvp->v_mount;
	nch.ncp = NULL;
	fakename = NULL;

	for (;;) {
		if (fakename) {
			kfree(fakename, M_TEMP);
			fakename = NULL;
		}
		error = vop_nlookupdotdot(*dvp->v_ops, dvp, &pvp, cred,
					  &fakename);
		if (error) {
			vrele(dvp);
			break;
		}
		vn_unlock(pvp);
		spin_lock_wr(&pvp->v_spinlock);
		if ((nch.ncp = TAILQ_FIRST(&pvp->v_namecache)) != NULL) {
			_cache_hold(nch.ncp);
			spin_unlock_wr(&pvp->v_spinlock);
			vrele(pvp);
			break;
		}
		spin_unlock_wr(&pvp->v_spinlock);
		if (pvp->v_flag & VROOT) {
			nch.ncp = _cache_get(pvp->v_mount->mnt_ncmountpt.ncp);
			error = cache_resolve_mp(nch.mount);
			_cache_unlock(nch.ncp);
			vrele(pvp);
			if (error) {
				_cache_drop(nch.ncp);
				nch.ncp = NULL;
				vrele(dvp);
			}
			break;
		}
		vrele(dvp);
		dvp = pvp;
	}
	if (error == 0) {
		if (last_fromdvp_report != time_second) {
			last_fromdvp_report = time_second;
			kprintf("Warning: extremely inefficient path "
				"resolution on %s\n",
				nch.ncp->nc_name);
		}
		error = cache_inefficient_scan(&nch, cred, dvp, fakename);

		/*
		 * Hopefully dvp now has a namecache record associated with
		 * it.  Leave it referenced to prevent the kernel from
		 * recycling the vnode.  Otherwise extremely long directory
		 * paths could result in endless recycling.
		 */
		if (*saved_dvp)
		    vrele(*saved_dvp);
		*saved_dvp = dvp;
		_cache_drop(nch.ncp);
	}
	if (fakename)
		kfree(fakename, M_TEMP);
	return (error);
}

/*
 * Do an inefficient scan of the directory represented by ncp looking for
 * the directory vnode dvp.  ncp must be held but not locked on entry and
 * will be held on return.  dvp must be refd but not locked on entry and
 * will remain refd on return.
 *
 * Why do this at all?  Well, due to its stateless nature the NFS server
 * converts file handles directly to vnodes without necessarily going through
 * the namecache ops that would otherwise create the namecache topology
 * leading to the vnode.  We could either (1) Change the namecache algorithms
 * to allow disconnect namecache records that are re-merged opportunistically,
 * or (2) Make the NFS server backtrack and scan to recover a connected
 * namecache topology in order to then be able to issue new API lookups.
 *
 * It turns out that (1) is a huge mess.  It takes a nice clean set of 
 * namecache algorithms and introduces a lot of complication in every subsystem
 * that calls into the namecache to deal with the re-merge case, especially
 * since we are using the namecache to placehold negative lookups and the
 * vnode might not be immediately assigned. (2) is certainly far less
 * efficient then (1), but since we are only talking about directories here
 * (which are likely to remain cached), the case does not actually run all
 * that often and has the supreme advantage of not polluting the namecache
 * algorithms.
 *
 * If a fakename is supplied just construct a namecache entry using the
 * fake name.
 */
static int
cache_inefficient_scan(struct nchandle *nch, struct ucred *cred, 
		       struct vnode *dvp, char *fakename)
{
	struct nlcomponent nlc;
	struct nchandle rncp;
	struct dirent *den;
	struct vnode *pvp;
	struct vattr vat;
	struct iovec iov;
	struct uio uio;
	int blksize;
	int eofflag;
	int bytes;
	char *rbuf;
	int error;

	vat.va_blocksize = 0;
	if ((error = VOP_GETATTR(dvp, &vat)) != 0)
		return (error);
	if ((error = cache_vref(nch, cred, &pvp)) != 0)
		return (error);
	if (ncvp_debug) {
		kprintf("inefficient_scan: directory iosize %ld "
			"vattr fileid = %lld\n",
			vat.va_blocksize,
			(long long)vat.va_fileid);
	}

	/*
	 * Use the supplied fakename if not NULL.  Fake names are typically
	 * not in the actual filesystem hierarchy.  This is used by HAMMER
	 * to glue @@timestamp recursions together.
	 */
	if (fakename) {
		nlc.nlc_nameptr = fakename;
		nlc.nlc_namelen = strlen(fakename);
		rncp = cache_nlookup(nch, &nlc);
		goto done;
	}

	if ((blksize = vat.va_blocksize) == 0)
		blksize = DEV_BSIZE;
	rbuf = kmalloc(blksize, M_TEMP, M_WAITOK);
	rncp.ncp = NULL;

	eofflag = 0;
	uio.uio_offset = 0;
again:
	iov.iov_base = rbuf;
	iov.iov_len = blksize;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = blksize;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = curthread;

	if (ncvp_debug >= 2)
		kprintf("cache_inefficient_scan: readdir @ %08x\n", (int)uio.uio_offset);
	error = VOP_READDIR(pvp, &uio, cred, &eofflag, NULL, NULL);
	if (error == 0) {
		den = (struct dirent *)rbuf;
		bytes = blksize - uio.uio_resid;

		while (bytes > 0) {
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: %*.*s\n",
					den->d_namlen, den->d_namlen, 
					den->d_name);
			}
			if (den->d_type != DT_WHT &&
			    den->d_ino == vat.va_fileid) {
				if (ncvp_debug) {
					kprintf("cache_inefficient_scan: "
					       "MATCHED inode %lld path %s/%*.*s\n",
					       (long long)vat.va_fileid,
					       nch->ncp->nc_name,
					       den->d_namlen, den->d_namlen,
					       den->d_name);
				}
				nlc.nlc_nameptr = den->d_name;
				nlc.nlc_namelen = den->d_namlen;
				rncp = cache_nlookup(nch, &nlc);
				KKASSERT(rncp.ncp != NULL);
				break;
			}
			bytes -= _DIRENT_DIRSIZ(den);
			den = _DIRENT_NEXT(den);
		}
		if (rncp.ncp == NULL && eofflag == 0 && uio.uio_resid != blksize)
			goto again;
	}
	kfree(rbuf, M_TEMP);
done:
	vrele(pvp);
	if (rncp.ncp) {
		if (rncp.ncp->nc_flag & NCF_UNRESOLVED) {
			_cache_setvp(rncp.mount, rncp.ncp, dvp);
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: setvp %s/%s = %p\n",
					nch->ncp->nc_name, rncp.ncp->nc_name, dvp);
			}
		} else {
			if (ncvp_debug >= 2) {
				kprintf("cache_inefficient_scan: setvp %s/%s already set %p/%p\n", 
					nch->ncp->nc_name, rncp.ncp->nc_name, dvp,
					rncp.ncp->nc_vp);
			}
		}
		if (rncp.ncp->nc_vp == NULL)
			error = rncp.ncp->nc_error;
		/* 
		 * Release rncp after a successful nlookup.  rncp was fully
		 * referenced.
		 */
		cache_put(&rncp);
	} else {
		kprintf("cache_inefficient_scan: dvp %p NOT FOUND in %s\n",
			dvp, nch->ncp->nc_name);
		error = ENOENT;
	}
	return (error);
}

/*
 * Zap a namecache entry.  The ncp is unconditionally set to an unresolved
 * state, which disassociates it from its vnode or ncneglist.
 *
 * Then, if there are no additional references to the ncp and no children,
 * the ncp is removed from the topology and destroyed.
 *
 * References and/or children may exist if the ncp is in the middle of the
 * topology, preventing the ncp from being destroyed.
 *
 * This function must be called with the ncp held and locked and will unlock
 * and drop it during zapping.
 *
 * This function may returned a held (but NOT locked) parent node which the
 * caller must drop.  We do this so _cache_drop() can loop, to avoid
 * blowing out the kernel stack.
 *
 * WARNING!  For MPSAFE operation this routine must acquire up to three
 *	     spin locks to be able to safely test nc_refs.  Lock order is
 *	     very important.
 *
 *	     hash spinlock if on hash list
 *	     parent spinlock if child of parent
 *	     (the ncp is unresolved so there is no vnode association)
 */
static struct namecache *
cache_zap(struct namecache *ncp)
{
	struct namecache *par;
	struct spinlock *hspin;
	struct vnode *dropvp;
	lwkt_tokref nlock;
	int refs;

	/*
	 * Disassociate the vnode or negative cache ref and set NCF_UNRESOLVED.
	 */
	_cache_setunresolved(ncp);

	/*
	 * Try to scrap the entry and possibly tail-recurse on its parent.
	 * We only scrap unref'd (other then our ref) unresolved entries,
	 * we do not scrap 'live' entries.
	 *
	 * Note that once the spinlocks are acquired if nc_refs == 1 no
	 * other references are possible.  If it isn't, however, we have
	 * to decrement but also be sure to avoid a 1->0 transition.
	 */
	KKASSERT(ncp->nc_flag & NCF_UNRESOLVED);
	KKASSERT(ncp->nc_refs > 0);

	/*
	 * Acquire locks
	 */
	lwkt_gettoken(&nlock, &vfs_token);
	hspin = NULL;
	if (ncp->nc_head) {
		hspin = &ncp->nc_head->spin;
		spin_lock_wr(hspin);
	}

	/*
	 * If someone other then us has a ref or we have children
	 * we cannot zap the entry.  The 1->0 transition and any
	 * further list operation is protected by the spinlocks
	 * we have acquired but other transitions are not.
	 */
	for (;;) {
		refs = ncp->nc_refs;
		if (refs == 1 && TAILQ_EMPTY(&ncp->nc_list))
			break;
		if (atomic_cmpset_int(&ncp->nc_refs, refs, refs - 1)) {
			if (hspin)
				spin_unlock_wr(hspin);
			lwkt_reltoken(&nlock);
			_cache_unlock(ncp);
			return(NULL);
		}
	}

	/*
	 * We are the only ref and with the spinlocks held no further
	 * refs can be acquired by others.
	 *
	 * Remove us from the hash list and parent list.  We have to
	 * drop a ref on the parent's vp if the parent's list becomes
	 * empty.
	 */
	if (ncp->nc_head) {
		LIST_REMOVE(ncp, nc_hash);
		ncp->nc_head = NULL;
	}
	dropvp = NULL;
	if ((par = ncp->nc_parent) != NULL) {
		par = _cache_hold(par);
		TAILQ_REMOVE(&par->nc_list, ncp, nc_entry);
		ncp->nc_parent = NULL;

		if (par->nc_vp && TAILQ_EMPTY(&par->nc_list))
			dropvp = par->nc_vp;
	}

	/*
	 * ncp should not have picked up any refs.  Physically
	 * destroy the ncp.
	 */
	if (hspin)
		spin_unlock_wr(hspin);
	lwkt_reltoken(&nlock);
	KKASSERT(ncp->nc_refs == 1);
	atomic_add_int(&numunres, -1);
	/* _cache_unlock(ncp) not required */
	ncp->nc_refs = -1;	/* safety */
	if (ncp->nc_name)
		kfree(ncp->nc_name, M_VFSCACHE);
	kfree(ncp, M_VFSCACHE);

	/*
	 * Delayed drop (we had to release our spinlocks)
	 *
	 * The refed parent (if not  NULL) must be dropped.  The
	 * caller is responsible for looping.
	 */
	if (dropvp)
		vdrop(dropvp);
	return(par);
}

static enum { CHI_LOW, CHI_HIGH } cache_hysteresis_state = CHI_LOW;

static __inline
void
_cache_hysteresis(void)
{
	/*
	 * Don't cache too many negative hits.  We use hysteresis to reduce
	 * the impact on the critical path.
	 */
	switch(cache_hysteresis_state) {
	case CHI_LOW:
		if (numneg > MINNEG && numneg * ncnegfactor > numcache) {
			cache_cleanneg(10);
			cache_hysteresis_state = CHI_HIGH;
		}
		break;
	case CHI_HIGH:
		if (numneg > MINNEG * 9 / 10 && 
		    numneg * ncnegfactor * 9 / 10 > numcache
		) {
			cache_cleanneg(10);
		} else {
			cache_hysteresis_state = CHI_LOW;
		}
		break;
	}
}

/*
 * NEW NAMECACHE LOOKUP API
 *
 * Lookup an entry in the cache.  A locked, referenced, non-NULL 
 * entry is *always* returned, even if the supplied component is illegal.
 * The resulting namecache entry should be returned to the system with
 * cache_put() or _cache_unlock() + cache_drop().
 *
 * namecache locks are recursive but care must be taken to avoid lock order
 * reversals.
 *
 * Nobody else will be able to manipulate the associated namespace (e.g.
 * create, delete, rename, rename-target) until the caller unlocks the
 * entry.
 *
 * The returned entry will be in one of three states:  positive hit (non-null
 * vnode), negative hit (null vnode), or unresolved (NCF_UNRESOLVED is set).
 * Unresolved entries must be resolved through the filesystem to associate the
 * vnode and/or determine whether a positive or negative hit has occured.
 *
 * It is not necessary to lock a directory in order to lock namespace under
 * that directory.  In fact, it is explicitly not allowed to do that.  A
 * directory is typically only locked when being created, renamed, or
 * destroyed.
 *
 * The directory (par) may be unresolved, in which case any returned child
 * will likely also be marked unresolved.  Likely but not guarenteed.  Since
 * the filesystem lookup requires a resolved directory vnode the caller is
 * responsible for resolving the namecache chain top-down.  This API 
 * specifically allows whole chains to be created in an unresolved state.
 */
struct nchandle
cache_nlookup(struct nchandle *par_nch, struct nlcomponent *nlc)
{
	struct nchandle nch;
	struct namecache *ncp;
	struct namecache *new_ncp;
	struct nchash_head *nchpp;
	struct mount *mp;
	u_int32_t hash;
	globaldata_t gd;
	lwkt_tokref nlock;

	numcalls++;
	gd = mycpu;
	mp = par_nch->mount;

	/*
	 * Try to locate an existing entry
	 */
	hash = fnv_32_buf(nlc->nlc_nameptr, nlc->nlc_namelen, FNV1_32_INIT);
	hash = fnv_32_buf(&par_nch->ncp, sizeof(par_nch->ncp), hash);
	new_ncp = NULL;
	nchpp = NCHHASH(hash);
restart:
	spin_lock_wr(&nchpp->spin);
	LIST_FOREACH(ncp, &nchpp->list, nc_hash) {
		numchecks++;

		/*
		 * Break out if we find a matching entry.  Note that
		 * UNRESOLVED entries may match, but DESTROYED entries
		 * do not.
		 */
		if (ncp->nc_parent == par_nch->ncp &&
		    ncp->nc_nlen == nlc->nlc_namelen &&
		    bcmp(ncp->nc_name, nlc->nlc_nameptr, ncp->nc_nlen) == 0 &&
		    (ncp->nc_flag & NCF_DESTROYED) == 0
		) {
			_cache_hold(ncp);
			spin_unlock_wr(&nchpp->spin);
			if (_cache_get_nonblock(ncp) == 0) {
				_cache_auto_unresolve(mp, ncp);
				if (new_ncp)
					_cache_free(new_ncp);
				goto found;
			}
			_cache_get(ncp);
			_cache_put(ncp);
			_cache_drop(ncp);
			goto restart;
		}
	}
	spin_unlock_wr(&nchpp->spin);

	/*
	 * We failed to locate an entry, create a new entry and add it to
	 * the cache.  We have to relookup after possibly blocking in
	 * malloc.
	 */
	if (new_ncp == NULL) {
		new_ncp = cache_alloc(nlc->nlc_namelen);
		goto restart;
	}

	ncp = new_ncp;

	/*
	 * Initialize as a new UNRESOLVED entry, lock (non-blocking),
	 * and link to the parent.  The mount point is usually inherited
	 * from the parent unless this is a special case such as a mount
	 * point where nlc_namelen is 0.   If nlc_namelen is 0 nc_name will
	 * be NULL.
	 */
	if (nlc->nlc_namelen) {
		bcopy(nlc->nlc_nameptr, ncp->nc_name, nlc->nlc_namelen);
		ncp->nc_name[nlc->nlc_namelen] = 0;
	}
	nchpp = NCHHASH(hash);		/* compiler optimization */
	spin_lock_wr(&nchpp->spin);
	LIST_INSERT_HEAD(&nchpp->list, ncp, nc_hash);
	ncp->nc_head = nchpp;
	spin_unlock_wr(&nchpp->spin);
	lwkt_gettoken(&nlock, &vfs_token);
	_cache_link_parent(ncp, par_nch->ncp);
	lwkt_reltoken(&nlock);
found:
	/*
	 * stats and namecache size management
	 */
	if (ncp->nc_flag & NCF_UNRESOLVED)
		++gd->gd_nchstats->ncs_miss;
	else if (ncp->nc_vp)
		++gd->gd_nchstats->ncs_goodhits;
	else
		++gd->gd_nchstats->ncs_neghits;
	_cache_hysteresis();
	nch.mount = mp;
	nch.ncp = ncp;
	atomic_add_int(&nch.mount->mnt_refs, 1);
	return(nch);
}

/*
 * The namecache entry is marked as being used as a mount point. 
 * Locate the mount if it is visible to the caller.
 */
struct findmount_info {
	struct mount *result;
	struct mount *nch_mount;
	struct namecache *nch_ncp;
};

static
int
cache_findmount_callback(struct mount *mp, void *data)
{
	struct findmount_info *info = data;

	/*
	 * Check the mount's mounted-on point against the passed nch.
	 */
	if (mp->mnt_ncmounton.mount == info->nch_mount &&
	    mp->mnt_ncmounton.ncp == info->nch_ncp
	) {
	    info->result = mp;
	    return(-1);
	}
	return(0);
}

struct mount *
cache_findmount(struct nchandle *nch)
{
	struct findmount_info info;

	info.result = NULL;
	info.nch_mount = nch->mount;
	info.nch_ncp = nch->ncp;
	mountlist_scan(cache_findmount_callback, &info,
			       MNTSCAN_FORWARD|MNTSCAN_NOBUSY);
	return(info.result);
}

/*
 * Resolve an unresolved namecache entry, generally by looking it up.
 * The passed ncp must be locked and refd. 
 *
 * Theoretically since a vnode cannot be recycled while held, and since
 * the nc_parent chain holds its vnode as long as children exist, the
 * direct parent of the cache entry we are trying to resolve should
 * have a valid vnode.  If not then generate an error that we can 
 * determine is related to a resolver bug.
 *
 * However, if a vnode was in the middle of a recyclement when the NCP
 * got locked, ncp->nc_vp might point to a vnode that is about to become
 * invalid.  cache_resolve() handles this case by unresolving the entry
 * and then re-resolving it.
 *
 * Note that successful resolution does not necessarily return an error
 * code of 0.  If the ncp resolves to a negative cache hit then ENOENT
 * will be returned.
 */
int
cache_resolve(struct nchandle *nch, struct ucred *cred)
{
	struct namecache *par;
	struct namecache *ncp;
	struct nchandle nctmp;
	struct mount *mp;
	struct vnode *dvp;
	int error;

	ncp = nch->ncp;
	mp = nch->mount;
restart:
	/*
	 * If the ncp is already resolved we have nothing to do.  However,
	 * we do want to guarentee that a usable vnode is returned when
	 * a vnode is present, so make sure it hasn't been reclaimed.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
			_cache_setunresolved(ncp);
		if ((ncp->nc_flag & NCF_UNRESOLVED) == 0)
			return (ncp->nc_error);
	}

	/*
	 * Mount points need special handling because the parent does not
	 * belong to the same filesystem as the ncp.
	 */
	if (ncp == mp->mnt_ncmountpt.ncp)
		return (cache_resolve_mp(mp));

	/*
	 * We expect an unbroken chain of ncps to at least the mount point,
	 * and even all the way to root (but this code doesn't have to go
	 * past the mount point).
	 */
	if (ncp->nc_parent == NULL) {
		kprintf("EXDEV case 1 %p %*.*s\n", ncp,
			ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		ncp->nc_error = EXDEV;
		return(ncp->nc_error);
	}

	/*
	 * The vp's of the parent directories in the chain are held via vhold()
	 * due to the existance of the child, and should not disappear. 
	 * However, there are cases where they can disappear:
	 *
	 *	- due to filesystem I/O errors.
	 *	- due to NFS being stupid about tracking the namespace and
	 *	  destroys the namespace for entire directories quite often.
	 *	- due to forced unmounts.
	 *	- due to an rmdir (parent will be marked DESTROYED)
	 *
	 * When this occurs we have to track the chain backwards and resolve
	 * it, looping until the resolver catches up to the current node.  We
	 * could recurse here but we might run ourselves out of kernel stack
	 * so we do it in a more painful manner.  This situation really should
	 * not occur all that often, or if it does not have to go back too
	 * many nodes to resolve the ncp.
	 */
	while ((dvp = cache_dvpref(ncp)) == NULL) {
		/*
		 * This case can occur if a process is CD'd into a
		 * directory which is then rmdir'd.  If the parent is marked
		 * destroyed there is no point trying to resolve it.
		 */
		if (ncp->nc_parent->nc_flag & NCF_DESTROYED)
			return(ENOENT);

		par = ncp->nc_parent;
		while (par->nc_parent && par->nc_parent->nc_vp == NULL)
			par = par->nc_parent;
		if (par->nc_parent == NULL) {
			kprintf("EXDEV case 2 %*.*s\n",
				par->nc_nlen, par->nc_nlen, par->nc_name);
			return (EXDEV);
		}
		kprintf("[diagnostic] cache_resolve: had to recurse on %*.*s\n",
			par->nc_nlen, par->nc_nlen, par->nc_name);
		/*
		 * The parent is not set in stone, ref and lock it to prevent
		 * it from disappearing.  Also note that due to renames it
		 * is possible for our ncp to move and for par to no longer
		 * be one of its parents.  We resolve it anyway, the loop 
		 * will handle any moves.
		 */
		_cache_get(par);
		if (par == nch->mount->mnt_ncmountpt.ncp) {
			cache_resolve_mp(nch->mount);
		} else if ((dvp = cache_dvpref(par)) == NULL) {
			kprintf("[diagnostic] cache_resolve: raced on %*.*s\n", par->nc_nlen, par->nc_nlen, par->nc_name);
			_cache_put(par);
			continue;
		} else {
			if (par->nc_flag & NCF_UNRESOLVED) {
				nctmp.mount = mp;
				nctmp.ncp = par;
				par->nc_error = VOP_NRESOLVE(&nctmp, dvp, cred);
			}
			vrele(dvp);
		}
		if ((error = par->nc_error) != 0) {
			if (par->nc_error != EAGAIN) {
				kprintf("EXDEV case 3 %*.*s error %d\n",
				    par->nc_nlen, par->nc_nlen, par->nc_name,
				    par->nc_error);
				_cache_put(par);
				return(error);
			}
			kprintf("[diagnostic] cache_resolve: EAGAIN par %p %*.*s\n",
				par, par->nc_nlen, par->nc_nlen, par->nc_name);
		}
		_cache_put(par);
		/* loop */
	}

	/*
	 * Call VOP_NRESOLVE() to get the vp, then scan for any disconnected
	 * ncp's and reattach them.  If this occurs the original ncp is marked
	 * EAGAIN to force a relookup.
	 *
	 * NOTE: in order to call VOP_NRESOLVE(), the parent of the passed
	 * ncp must already be resolved.
	 */
	if (dvp) {
		nctmp.mount = mp;
		nctmp.ncp = ncp;
		ncp->nc_error = VOP_NRESOLVE(&nctmp, dvp, cred);
		vrele(dvp);
	} else {
		ncp->nc_error = EPERM;
	}
	if (ncp->nc_error == EAGAIN) {
		kprintf("[diagnostic] cache_resolve: EAGAIN ncp %p %*.*s\n",
			ncp, ncp->nc_nlen, ncp->nc_nlen, ncp->nc_name);
		goto restart;
	}
	return(ncp->nc_error);
}

/*
 * Resolve the ncp associated with a mount point.  Such ncp's almost always
 * remain resolved and this routine is rarely called.  NFS MPs tends to force
 * re-resolution more often due to its mac-truck-smash-the-namecache
 * method of tracking namespace changes.
 *
 * The semantics for this call is that the passed ncp must be locked on
 * entry and will be locked on return.  However, if we actually have to
 * resolve the mount point we temporarily unlock the entry in order to
 * avoid race-to-root deadlocks due to e.g. dead NFS mounts.  Because of
 * the unlock we have to recheck the flags after we relock.
 */
static int
cache_resolve_mp(struct mount *mp)
{
	struct namecache *ncp = mp->mnt_ncmountpt.ncp;
	struct vnode *vp;
	int error;

	KKASSERT(mp != NULL);

	/*
	 * If the ncp is already resolved we have nothing to do.  However,
	 * we do want to guarentee that a usable vnode is returned when
	 * a vnode is present, so make sure it hasn't been reclaimed.
	 */
	if ((ncp->nc_flag & NCF_UNRESOLVED) == 0) {
		if (ncp->nc_vp && (ncp->nc_vp->v_flag & VRECLAIMED))
			_cache_setunresolved(ncp);
	}

	if (ncp->nc_flag & NCF_UNRESOLVED) {
		_cache_unlock(ncp);
		while (vfs_busy(mp, 0))
			;
		error = VFS_ROOT(mp, &vp);
		_cache_lock(ncp);

		/*
		 * recheck the ncp state after relocking.
		 */
		if (ncp->nc_flag & NCF_UNRESOLVED) {
			ncp->nc_error = error;
			if (error == 0) {
				_cache_setvp(mp, ncp, vp);
				vput(vp);
			} else {
				kprintf("[diagnostic] cache_resolve_mp: failed"
					" to resolve mount %p err=%d ncp=%p\n",
					mp, error, ncp);
				_cache_setvp(mp, ncp, NULL);
			}
		} else if (error == 0) {
			vput(vp);
		}
		vfs_unbusy(mp);
	}
	return(ncp->nc_error);
}

/*
 * MPSAFE
 */
void
cache_cleanneg(int count)
{
	struct namecache *ncp;

	/*
	 * Automode from the vnlru proc - clean out 10% of the negative cache
	 * entries.
	 */
	if (count == 0)
		count = numneg / 10 + 1;

	/*
	 * Attempt to clean out the specified number of negative cache
	 * entries.
	 */
	while (count) {
		spin_lock_wr(&ncspin);
		ncp = TAILQ_FIRST(&ncneglist);
		if (ncp == NULL) {
			spin_unlock_wr(&ncspin);
			break;
		}
		TAILQ_REMOVE(&ncneglist, ncp, nc_vnode);
		TAILQ_INSERT_TAIL(&ncneglist, ncp, nc_vnode);
		_cache_hold(ncp);
		spin_unlock_wr(&ncspin);
		if (_cache_get_nonblock(ncp) == 0) {
			ncp = cache_zap(ncp);
			if (ncp)
				_cache_drop(ncp);
		} else {
			_cache_drop(ncp);
		}
		--count;
	}
}

/*
 * Rehash a ncp.  Rehashing is typically required if the name changes (should
 * not generally occur) or the parent link changes.  This function will
 * unhash the ncp if the ncp is no longer hashable.
 */
static void
_cache_rehash(struct namecache *ncp)
{
	struct nchash_head *nchpp;
	u_int32_t hash;

	if ((nchpp = ncp->nc_head) != NULL) {
		spin_lock_wr(&nchpp->spin);
		LIST_REMOVE(ncp, nc_hash);
		ncp->nc_head = NULL;
		spin_unlock_wr(&nchpp->spin);
	}
	if (ncp->nc_nlen && ncp->nc_parent) {
		hash = fnv_32_buf(ncp->nc_name, ncp->nc_nlen, FNV1_32_INIT);
		hash = fnv_32_buf(&ncp->nc_parent, 
					sizeof(ncp->nc_parent), hash);
		nchpp = NCHHASH(hash);
		spin_lock_wr(&nchpp->spin);
		LIST_INSERT_HEAD(&nchpp->list, ncp, nc_hash);
		ncp->nc_head = nchpp;
		spin_unlock_wr(&nchpp->spin);
	}
}

/*
 * Name cache initialization, from vfsinit() when we are booting
 */
void
nchinit(void)
{
	int i;
	globaldata_t gd;

	/* initialise per-cpu namecache effectiveness statistics. */
	for (i = 0; i < ncpus; ++i) {
		gd = globaldata_find(i);
		gd->gd_nchstats = &nchstats[i];
	}
	TAILQ_INIT(&ncneglist);
	spin_init(&ncspin);
	nchashtbl = hashinit_ext(desiredvnodes*2, sizeof(struct nchash_head),
				 M_VFSCACHE, &nchash);
	for (i = 0; i <= (int)nchash; ++i) {
		LIST_INIT(&nchashtbl[i].list);
		spin_init(&nchashtbl[i].spin);
	}
	nclockwarn = 5 * hz;
}

/*
 * Called from start_init() to bootstrap the root filesystem.  Returns
 * a referenced, unlocked namecache record.
 */
void
cache_allocroot(struct nchandle *nch, struct mount *mp, struct vnode *vp)
{
	nch->ncp = cache_alloc(0);
	nch->mount = mp;
	atomic_add_int(&mp->mnt_refs, 1);
	if (vp)
		_cache_setvp(nch->mount, nch->ncp, vp);
}

/*
 * vfs_cache_setroot()
 *
 *	Create an association between the root of our namecache and
 *	the root vnode.  This routine may be called several times during
 *	booting.
 *
 *	If the caller intends to save the returned namecache pointer somewhere
 *	it must cache_hold() it.
 */
void
vfs_cache_setroot(struct vnode *nvp, struct nchandle *nch)
{
	struct vnode *ovp;
	struct nchandle onch;

	ovp = rootvnode;
	onch = rootnch;
	rootvnode = nvp;
	if (nch)
		rootnch = *nch;
	else
		cache_zero(&rootnch);
	if (ovp)
		vrele(ovp);
	if (onch.ncp)
		cache_drop(&onch);
}

/*
 * XXX OLD API COMPAT FUNCTION.  This really messes up the new namecache
 * topology and is being removed as quickly as possible.  The new VOP_N*()
 * API calls are required to make specific adjustments using the supplied
 * ncp pointers rather then just bogusly purging random vnodes.
 *
 * Invalidate all namecache entries to a particular vnode as well as 
 * any direct children of that vnode in the namecache.  This is a 
 * 'catch all' purge used by filesystems that do not know any better.
 *
 * Note that the linkage between the vnode and its namecache entries will
 * be removed, but the namecache entries themselves might stay put due to
 * active references from elsewhere in the system or due to the existance of
 * the children.   The namecache topology is left intact even if we do not
 * know what the vnode association is.  Such entries will be marked
 * NCF_UNRESOLVED.
 */
void
cache_purge(struct vnode *vp)
{
	cache_inval_vp(vp, CINV_DESTROY | CINV_CHILDREN);
}

/*
 * Flush all entries referencing a particular filesystem.
 *
 * Since we need to check it anyway, we will flush all the invalid
 * entries at the same time.
 */
#if 0

void
cache_purgevfs(struct mount *mp)
{
	struct nchash_head *nchpp;
	struct namecache *ncp, *nnp;

	/*
	 * Scan hash tables for applicable entries.
	 */
	for (nchpp = &nchashtbl[nchash]; nchpp >= nchashtbl; nchpp--) {
		spin_lock_wr(&nchpp->spin); XXX
		ncp = LIST_FIRST(&nchpp->list);
		if (ncp)
			_cache_hold(ncp);
		while (ncp) {
			nnp = LIST_NEXT(ncp, nc_hash);
			if (nnp)
				_cache_hold(nnp);
			if (ncp->nc_mount == mp) {
				_cache_lock(ncp);
				ncp = cache_zap(ncp);
				if (ncp)
					_cache_drop(ncp);
			} else {
				_cache_drop(ncp);
			}
			ncp = nnp;
		}
		spin_unlock_wr(&nchpp->spin); XXX
	}
}

#endif

static int disablecwd;
SYSCTL_INT(_debug, OID_AUTO, disablecwd, CTLFLAG_RW, &disablecwd, 0, "");

static u_long numcwdcalls; STATNODE(CTLFLAG_RD, numcwdcalls, &numcwdcalls);
static u_long numcwdfail1; STATNODE(CTLFLAG_RD, numcwdfail1, &numcwdfail1);
static u_long numcwdfail2; STATNODE(CTLFLAG_RD, numcwdfail2, &numcwdfail2);
static u_long numcwdfail3; STATNODE(CTLFLAG_RD, numcwdfail3, &numcwdfail3);
static u_long numcwdfail4; STATNODE(CTLFLAG_RD, numcwdfail4, &numcwdfail4);
static u_long numcwdfound; STATNODE(CTLFLAG_RD, numcwdfound, &numcwdfound);

/*
 * MPALMOSTSAFE
 */
int
sys___getcwd(struct __getcwd_args *uap)
{
	int buflen;
	int error;
	char *buf;
	char *bp;

	if (disablecwd)
		return (ENODEV);

	buflen = uap->buflen;
	if (buflen == 0)
		return (EINVAL);
	if (buflen > MAXPATHLEN)
		buflen = MAXPATHLEN;

	buf = kmalloc(buflen, M_TEMP, M_WAITOK);
	get_mplock();
	bp = kern_getcwd(buf, buflen, &error);
	rel_mplock();
	if (error == 0)
		error = copyout(bp, uap->buf, strlen(bp) + 1);
	kfree(buf, M_TEMP);
	return (error);
}

char *
kern_getcwd(char *buf, size_t buflen, int *error)
{
	struct proc *p = curproc;
	char *bp;
	int i, slash_prefixed;
	struct filedesc *fdp;
	struct nchandle nch;

	numcwdcalls++;
	bp = buf;
	bp += buflen - 1;
	*bp = '\0';
	fdp = p->p_fd;
	slash_prefixed = 0;

	nch = fdp->fd_ncdir;
	while (nch.ncp && (nch.ncp != fdp->fd_nrdir.ncp || 
	       nch.mount != fdp->fd_nrdir.mount)
	) {
		/*
		 * While traversing upwards if we encounter the root
		 * of the current mount we have to skip to the mount point
		 * in the underlying filesystem.
		 */
		if (nch.ncp == nch.mount->mnt_ncmountpt.ncp) {
			nch = nch.mount->mnt_ncmounton;
			continue;
		}

		/*
		 * Prepend the path segment
		 */
		for (i = nch.ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				numcwdfail4++;
				*error = ERANGE;
				return(NULL);
			}
			*--bp = nch.ncp->nc_name[i];
		}
		if (bp == buf) {
			numcwdfail4++;
			*error = ERANGE;
			return(NULL);
		}
		*--bp = '/';
		slash_prefixed = 1;

		/*
		 * Go up a directory.  This isn't a mount point so we don't
		 * have to check again.
		 */
		nch.ncp = nch.ncp->nc_parent;
	}
	if (nch.ncp == NULL) {
		numcwdfail2++;
		*error = ENOENT;
		return(NULL);
	}
	if (!slash_prefixed) {
		if (bp == buf) {
			numcwdfail4++;
			*error = ERANGE;
			return(NULL);
		}
		*--bp = '/';
	}
	numcwdfound++;
	*error = 0;
	return (bp);
}

/*
 * Thus begins the fullpath magic.
 */

#undef STATNODE
#define STATNODE(name)							\
	static u_int name;						\
	SYSCTL_UINT(_vfs_cache, OID_AUTO, name, CTLFLAG_RD, &name, 0, "")

static int disablefullpath;
SYSCTL_INT(_debug, OID_AUTO, disablefullpath, CTLFLAG_RW,
    &disablefullpath, 0, "");

STATNODE(numfullpathcalls);
STATNODE(numfullpathfail1);
STATNODE(numfullpathfail2);
STATNODE(numfullpathfail3);
STATNODE(numfullpathfail4);
STATNODE(numfullpathfound);

int
cache_fullpath(struct proc *p, struct nchandle *nchp, char **retbuf, char **freebuf)
{
	struct nchandle fd_nrdir;
	struct nchandle nch;
	struct namecache *ncp;
	lwkt_tokref nlock;
	struct mount *mp;
	char *bp, *buf;
	int slash_prefixed;
	int error = 0;
	int i;

	atomic_add_int(&numfullpathcalls, -1);
	lwkt_gettoken(&nlock, &vfs_token);

	*retbuf = NULL; 
	*freebuf = NULL;

	buf = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	bp = buf + MAXPATHLEN - 1;
	*bp = '\0';
	if (p != NULL)
		fd_nrdir = p->p_fd->fd_nrdir;
	else
		fd_nrdir = rootnch;
	slash_prefixed = 0;
	cache_copy(nchp, &nch);
	ncp = nch.ncp;
	mp = nch.mount;

	while (ncp && (ncp != fd_nrdir.ncp || mp != fd_nrdir.mount)) {
		/*
		 * While traversing upwards if we encounter the root
		 * of the current mount we have to skip to the mount point.
		 */
		if (ncp == mp->mnt_ncmountpt.ncp) {
			cache_drop(&nch);
			cache_copy(&mp->mnt_ncmounton, &nch);
			ncp = nch.ncp;
			mp = nch.mount;
			continue;
		}

		/*
		 * Prepend the path segment
		 */
		for (i = nch.ncp->nc_nlen - 1; i >= 0; i--) {
			if (bp == buf) {
				numfullpathfail4++;
				kfree(buf, M_TEMP);
				error = ENOMEM;
				goto done;
			}
			*--bp = nch.ncp->nc_name[i];
		}
		if (bp == buf) {
			numfullpathfail4++;
			kfree(buf, M_TEMP);
			error = ENOMEM;
			goto done;
		}
		*--bp = '/';
		slash_prefixed = 1;

		/*
		 * Go up a directory.  This isn't a mount point so we don't
		 * have to check again.
		 *
		 * We need the ncp's spinlock to safely access nc_parent.
		 */
		if ((nch.ncp = ncp->nc_parent) != NULL)
			_cache_hold(nch.ncp);
		_cache_drop(ncp);
		ncp = nch.ncp;
	}
	if (nch.ncp == NULL) {
		numfullpathfail2++;
		kfree(buf, M_TEMP);
		error = ENOENT;
		goto done;
	}

	if (!slash_prefixed) {
		if (bp == buf) {
			numfullpathfail4++;
			kfree(buf, M_TEMP);
			error = ENOMEM;
			goto done;
		}
		*--bp = '/';
	}
	numfullpathfound++;
	*retbuf = bp; 
	*freebuf = buf;
	error = 0;
done:
	cache_drop(&nch);
	lwkt_reltoken(&nlock);
	return(error);
}

int
vn_fullpath(struct proc *p, struct vnode *vn, char **retbuf, char **freebuf) 
{
	struct namecache *ncp;
	struct nchandle nch;
	int error;

	atomic_add_int(&numfullpathcalls, 1);
	if (disablefullpath)
		return (ENODEV);

	if (p == NULL)
		return (EINVAL);

	/* vn is NULL, client wants us to use p->p_textvp */
	if (vn == NULL) {
		if ((vn = p->p_textvp) == NULL)
			return (EINVAL);
	}
	spin_lock_wr(&vn->v_spinlock);
	TAILQ_FOREACH(ncp, &vn->v_namecache, nc_vnode) {
		if (ncp->nc_nlen)
			break;
	}
	if (ncp == NULL) {
		spin_unlock_wr(&vn->v_spinlock);
		return (EINVAL);
	}
	_cache_hold(ncp);
	spin_unlock_wr(&vn->v_spinlock);

	atomic_add_int(&numfullpathcalls, -1);
	nch.ncp = ncp;;
	nch.mount = vn->v_mount;
	error = cache_fullpath(p, &nch, retbuf, freebuf);
	_cache_drop(ncp);
	return (error);
}
