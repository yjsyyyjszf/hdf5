/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdf.ncsa.uiuc.edu/HDF5/doc/Copyright.html.  If you do not have     *
 * access to either file, you may request a copy from hdfhelp@ncsa.uiuc.edu. *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:		H5HF.c
 *			Feb 24 2006
 *			Quincey Koziol <koziol@ncsa.uiuc.edu>
 *
 * Purpose:		Implements a "fractal heap" for storing variable-
 *                      length objects in a file.
 *
 *                      Please see the documentation in:
 *                      doc/html/TechNotes/FractalHeap.html for a full description
 *                      of how they work, etc.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5HF_PACKAGE		/*suppress error about including H5HFpkg  */

/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5FOprivate.h"        /* File objects                         */
#include "H5HFpkg.h"		/* Fractal heaps			*/
#include "H5MFprivate.h"	/* File memory management		*/

/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Declare a free list to manage the H5HF_t struct */
H5FL_DEFINE_STATIC(H5HF_t);



/*-------------------------------------------------------------------------
 * Function:	H5HF_create
 *
 * Purpose:	Creates a new empty fractal heap in the file.
 *
 * Return:	Pointer to heap wrapper on success
 *              NULL on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Feb 24 2006
 *
 *-------------------------------------------------------------------------
 */
H5HF_t *
H5HF_create(H5F_t *f, hid_t dxpl_id, const H5HF_create_t *cparam)
{
    H5HF_t *fh = NULL;          /* Pointer to new fractal heap */
    H5HF_hdr_t *hdr = NULL;     /* The new fractal heap header information */
    haddr_t hdr_addr;           /* Heap header address */
    H5HF_t *ret_value;          /* Return value */

    FUNC_ENTER_NOAPI(H5HF_create, NULL)

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(cparam);

    /* Allocate & basic initialization for the shared header */
    if(NULL == (hdr = H5HF_hdr_alloc(f)))
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate space for shared heap info")

    /* Allocate space for the header on disk */
    if(HADDR_UNDEF == (hdr_addr = H5MF_alloc(f, H5FD_MEM_FHEAP_HDR, dxpl_id, (hsize_t)H5HF_HEADER_SIZE(hdr))))
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "file allocation failed for fractal heap header")

    /* Initialize shared fractal heap header */
    /* (This routine is only called for newly created heaps) */
    if(H5HF_hdr_init(hdr, hdr_addr, cparam) < 0)
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't initialize shared fractal heap header")

#ifdef QAK
HDfprintf(stderr, "%s: hdr->id_len = %Zu\n", FUNC, hdr->id_len);
#endif /* QAK */

    /* Cache the new fractal heap header */
    if(H5AC_set(f, dxpl_id, H5AC_FHEAP_HDR, hdr_addr, hdr, H5AC__NO_FLAGS_SET) < 0)
	HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, "can't add fractal heap header to cache")

    /* Create fractal heap wrapper */
    if(NULL == (fh = H5FL_MALLOC(H5HF_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed for fractal heap info")

    /* Lock the heap header into memory */
    if(NULL == (hdr = H5AC_protect(f, dxpl_id, H5AC_FHEAP_HDR, hdr_addr, NULL, NULL, H5AC_WRITE)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, NULL, "unable to load fractal heap header")

    /* Point fractal heap wrapper at header and bump it's ref count */
    fh->hdr = hdr;
    if(H5HF_hdr_incr(fh->hdr) < 0)
	HGOTO_ERROR(H5E_HEAP, H5E_CANTINC, NULL, "can't increment reference count on shared heap header")

    /* Unlock heap header, now pinned */
    if(H5AC_unprotect(f, dxpl_id, H5AC_FHEAP_HDR, hdr_addr, hdr, H5AC__NO_FLAGS_SET) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, NULL, "unable to release fractal heap header")
    hdr = NULL;

    /* Add heap to list of open objects in file */
    if(H5FO_insert(f, fh->hdr->heap_addr, fh, FALSE) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTINSERT, NULL, "can't insert heap into list of open objects")

    /* Set open object count */
    fh->fo_count = 1;

    /* Set the return value */
    ret_value = fh;

done:
    if(!ret_value) {
        if(fh)
            (void)H5HF_close(fh, dxpl_id);
	else if(hdr)
            (void)H5HF_cache_hdr_dest(f, hdr);
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_create() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_open
 *
 * Purpose:	Opens an existing fractal heap in the file.
 *
 * Return:	Pointer to heap wrapper on success
 *              NULL on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Apr 18 2006
 *
 *-------------------------------------------------------------------------
 */
H5HF_t *
H5HF_open(H5F_t *f, hid_t dxpl_id, haddr_t fh_addr)
{
    H5HF_t *fh = NULL;          /* Pointer to new fractal heap */
    H5HF_hdr_t *hdr = NULL;     /* The fractal heap header information */
    H5HF_t *ret_value;          /* Return value */

    FUNC_ENTER_NOAPI(H5HF_open, NULL)

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(H5F_addr_defined(fh_addr));

    /* Check if group was already open */
    if((fh = H5FO_opened(f, fh_addr)) == NULL) {

        /* Clear any errors from H5FO_opened() */
        H5E_clear_stack(NULL);

        /* Load the heap header into memory */
#ifdef QAK
HDfprintf(stderr, "%s: fh_addr = %a\n", FUNC, fh_addr);
#endif /* QAK */
        if(NULL == (hdr = H5AC_protect(f, dxpl_id, H5AC_FHEAP_HDR, fh_addr, NULL, NULL, H5AC_READ)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTLOAD, NULL, "unable to load fractal heap header")
#ifdef QAK
HDfprintf(stderr, "%s: hdr->rc = %u\n", FUNC, hdr->rc);
#endif /* QAK */

        /* Create fractal heap info */
        if(NULL == (fh = H5FL_MALLOC(H5HF_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed for fractal heap info")

        /* Point fractal heap wrapper at header */
        fh->hdr = hdr;
        if(H5HF_hdr_incr(fh->hdr) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINC, NULL, "can't increment reference count on shared heap header")

        /* Add heap to list of open objects in file */
        if(H5FO_insert(f, fh->hdr->heap_addr, fh, FALSE) < 0)
            HGOTO_ERROR(H5E_SYM, H5E_CANTINSERT, NULL, "can't insert heap into list of open objects")

        /* Set open object count */
        fh->fo_count = 1;
    } /* end if */
    else {
        /* Increment shared reference count */
        fh->fo_count++;
    } /* end else */

    /* Set the return value */
    ret_value = fh;

done:
    if(hdr && H5AC_unprotect(f, dxpl_id, H5AC_FHEAP_HDR, fh_addr, hdr, H5AC__NO_FLAGS_SET) < 0)
        HDONE_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, NULL, "unable to release fractal heap header")
    if(!ret_value) {
        if(fh)
            (void)H5HF_close(fh, dxpl_id);
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_open() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_get_id_len
 *
 * Purpose:	Get the size of IDs for entries in a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Apr 17 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_get_id_len(H5HF_t *fh, size_t *id_len_p)
{
    FUNC_ENTER_NOAPI_NOFUNC(H5HF_get_id_len)

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(id_len_p);

    /* Retrieve the ID length for entries in this heap */
    *id_len_p = fh->hdr->id_len;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5HF_get_id_len() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_get_heap_addr
 *
 * Purpose:	Get the address of a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Apr 18 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_get_heap_addr(H5HF_t *fh, haddr_t *heap_addr_p)
{
    FUNC_ENTER_NOAPI_NOFUNC(H5HF_get_heap_addr)

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(heap_addr_p);

    /* Retrieve the heap header address for this heap */
    *heap_addr_p = fh->hdr->heap_addr;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5HF_get_heap_addr() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_insert
 *
 * Purpose:	Insert a new object into a fractal heap.
 *
 * Return:	Non-negative on success (with heap ID of new object
 *              filled in), negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Feb 24 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_insert(H5HF_t *fh, hid_t dxpl_id, size_t size, const void *obj,
    void *id/*out*/)
{
    H5HF_hdr_t *hdr = NULL;                  /* The fractal heap header information */
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5HF_insert, FAIL)
#ifdef QAK
HDfprintf(stderr, "%s: size = %Zu\n", FUNC, size);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(size > 0);
    HDassert(obj);
    HDassert(id);

    /* Get the fractal heap header */
    hdr = fh->hdr;

    /* Check if object is large enough to be standalone */
    if(size > hdr->standalone_size) {
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "standalone blocks not supported yet")
    } /* end if */
    else {
        /* Check if we are in "append only" mode, or if there's enough room for the object */
        if(hdr->write_once) {
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "'write once' managed blocks not supported yet")
        } /* end if */
        else {
            H5HF_free_section_t *sec_node;         /* Pointer to free space section */

            /* Find free space in heap */
            if(H5HF_man_find(hdr, dxpl_id, size, &sec_node) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "can't allocate space in fractal heap")

            /* Use free space for allocating object */
            if(H5HF_man_insert(hdr, dxpl_id, sec_node, size, obj, id) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "can't allocate space for object in fractal heap")
        } /* end else */
    } /* end else */

done:
#ifdef QAK
HDfprintf(stderr, "%s: Leaving, ret_value = %d\n", FUNC, ret_value);
#endif /* QAK */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_insert() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_get_obj_len
 *
 * Purpose:	Get the size of an entry in a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		May  9 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_get_obj_len(H5HF_t *fh, const void *_id, size_t *obj_len_p)
{
    const uint8_t *id = (const uint8_t *)_id;   /* Object ID */
    uint8_t id_flags;                   /* Heap ID flag bits */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI(H5HF_get_obj_len, FAIL)

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(id);
    HDassert(obj_len_p);

    /* Get the ID flags */
    id_flags = *id++;

    /* Check for correct heap ID version */
    if((id_flags & H5HF_ID_VERS_MASK) != H5HF_ID_VERS_CURR)
        HGOTO_ERROR(H5E_HEAP, H5E_VERSION, FAIL, "incorrect heap ID version")

    /* Check for managed object */
    if((id_flags & H5HF_ID_TYPE_MASK) == H5HF_ID_TYPE_MAN) {
        /* Skip over object offset */
        id += fh->hdr->heap_off_size;

        /* Retrieve the entry length */
        UINT64DECODE_VAR(id, *obj_len_p, fh->hdr->heap_len_size);
    } /* end if */
    else {
HDfprintf(stderr, "%s: Heap ID type not supported yet!\n", FUNC);
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "heap ID type not supported yet")
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_get_obj_len() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_read
 *
 * Purpose:	Read an object from a fractal heap into a buffer
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 18 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_read(H5HF_t *fh, hid_t dxpl_id, const void *_id, void *obj/*out*/)
{
    const uint8_t *id = (const uint8_t *)_id;   /* Object ID */
    uint8_t id_flags;                   /* Heap ID flag bits */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI(H5HF_read, FAIL)

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(id);
    HDassert(obj);

    /* Get the ID flags */
    id_flags = *id++;

    /* Check for correct heap ID version */
    if((id_flags & H5HF_ID_VERS_MASK) != H5HF_ID_VERS_CURR)
        HGOTO_ERROR(H5E_HEAP, H5E_VERSION, FAIL, "incorrect heap ID version")

    /* Check for managed object */
    if((id_flags & H5HF_ID_TYPE_MASK) == H5HF_ID_TYPE_MAN) {
        hsize_t obj_off;                    /* Object's offset in heap */
        size_t obj_len;                     /* Object's length in heap */

        /* Decode the object offset within the heap & it's length */
        UINT64DECODE_VAR(id, obj_off, fh->hdr->heap_off_size);
        UINT64DECODE_VAR(id, obj_len, fh->hdr->heap_len_size);
#ifdef QAK
HDfprintf(stderr, "%s: obj_off = %Hu, obj_len = %Zu\n", FUNC, obj_off, obj_len);
#endif /* QAK */

        /* Read object from managed heap blocks */
        if(H5HF_man_read(fh->hdr, dxpl_id, obj_off, obj_len, obj) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTGET, FAIL, "can't read object from fractal heap")
    } /* end if */
    else {
HDfprintf(stderr, "%s: Heap ID type not supported yet!\n", FUNC);
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "heap ID type not supported yet")
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_read() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_remove
 *
 * Purpose:	Remove an object from a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		May 15 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_remove(H5HF_t *fh, hid_t dxpl_id, const void *_id)
{
    const uint8_t *id = (const uint8_t *)_id;   /* Object ID */
    uint8_t id_flags;                   /* Heap ID flag bits */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI(H5HF_remove, FAIL)

    /*
     * Check arguments.
     */
    HDassert(fh);
    HDassert(fh->hdr);
    HDassert(id);

    /* Get the ID flags */
    id_flags = *id++;

    /* Check for correct heap ID version */
    if((id_flags & H5HF_ID_VERS_MASK) != H5HF_ID_VERS_CURR)
        HGOTO_ERROR(H5E_HEAP, H5E_VERSION, FAIL, "incorrect heap ID version")

    /* Check for managed object */
    if((id_flags & H5HF_ID_TYPE_MASK) == H5HF_ID_TYPE_MAN) {
        hsize_t obj_off;                    /* Object's offset in heap */
        size_t obj_len;                     /* Object's length in heap */

        /* Decode the object offset within the heap & it's length */
#ifdef QAK
HDfprintf(stderr, "%s: fh->hdr->heap_off_size = %u, fh->hdr->heap_len_size = %u\n", FUNC, (unsigned)fh->hdr->heap_off_size, (unsigned)fh->hdr->heap_len_size);
#endif /* QAK */
        UINT64DECODE_VAR(id, obj_off, fh->hdr->heap_off_size);
        UINT64DECODE_VAR(id, obj_len, fh->hdr->heap_len_size);
#ifdef QAK
HDfprintf(stderr, "%s: obj_off = %Hu, obj_len = %Zu\n", FUNC, obj_off, obj_len);
#endif /* QAK */

        /* Sanity check parameters */
        HDassert(obj_off);
        HDassert(obj_len);

        /* Remove object from managed heap blocks */
        if(H5HF_man_remove(fh->hdr, dxpl_id, obj_off, obj_len) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTREMOVE, FAIL, "can't remove object from fractal heap")
    } /* end if */
    else {
HDfprintf(stderr, "%s: Heap ID type not supported yet!\n", FUNC);
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "heap ID type not supported yet")
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_remove() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_close
 *
 * Purpose:	Close a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Apr 17 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_close(H5HF_t *fh, hid_t dxpl_id)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5HF_close, FAIL)

    /*
     * Check arguments.
     */
    HDassert(fh);

    /* Decrement shared object count */
    --fh->fo_count;

    /* Check if this is the last reference to the shared heap wrapper */
    if(0 == fh->fo_count) {
        /* Remove the heap from the list of opened objects in the file */
        if(H5FO_delete(fh->hdr->f, H5AC_dxpl_id, fh->hdr->heap_addr) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, "can't remove group from list of open objects")

        /* Close the free space information */
        /* (Can't put this in header "destroy" routine, because it has
         *      pointers to indirect blocks in the heap, which would create
         *      a reference loop and the objects couldn't be removed from
         *      the metadata cache - QAK)
         */
        if(H5HF_space_close(fh->hdr, dxpl_id) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, "can't release free space info")

        /* Reset the block iterator */
        /* (Can't put this in header "destroy" routine, because it has
         *      pointers to indirect blocks in the heap, which would create
         *      a reference loop and the objects couldn't be removed from
         *      the metadata cache - QAK)
         */
#ifdef QAK
HDfprintf(stderr, "%s; fh->hdr->man_iter_off = %Hu\n", FUNC, fh->hdr->man_iter_off);
HDfprintf(stderr, "%s; fh->hdr->man_size = %Hu\n", FUNC, fh->hdr->man_size);
HDfprintf(stderr, "%s; fh->hdr->rc = %Zu\n", FUNC, fh->hdr->rc);
#endif /* QAK */
        /* Reset block iterator, if necessary */
        if(H5HF_man_iter_ready(&fh->hdr->next_block)) {
            if(H5HF_man_iter_reset(&fh->hdr->next_block) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, "can't reset block iterator")
        } /* end if */
#ifdef QAK
HDfprintf(stderr, "%s; After iterator reset fh->hdr->rc = %Zu\n", FUNC, fh->hdr->rc);
#endif /* QAK */

        /* Decrement the reference count on the heap header */
        if(H5HF_hdr_decr(fh->hdr) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTDEC, FAIL, "can't decrement reference count on shared heap header")

        /* Release the fractal heap wrapper */
        H5FL_FREE(H5HF_t, fh);
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_close() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_delete
 *
 * Purpose:	Delete a fractal heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Aug  4 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_delete(H5F_t *f, hid_t dxpl_id, haddr_t fh_addr)
{
    H5HF_hdr_t *hdr = NULL;             /* The fractal heap header information */
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(H5HF_delete, FAIL)

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(H5F_addr_defined(fh_addr));

    /* Check if group was already open */
    if(H5FO_opened(f, fh_addr) != NULL)
        HGOTO_ERROR(H5E_HEAP, H5E_OBJOPEN, FAIL, "heap still open")

    /* Clear any errors from H5FO_opened() */
    H5E_clear_stack(NULL);

    /* Load the heap header into memory */
#ifdef QAK
HDfprintf(stderr, "%s: fh_addr = %a\n", FUNC, fh_addr);
#endif /* QAK */
    if(NULL == (hdr = H5AC_protect(f, dxpl_id, H5AC_FHEAP_HDR, fh_addr, NULL, NULL, H5AC_WRITE)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTLOAD, FAIL, "unable to load fractal heap header")

    /* Check for free space manager for heap */
    if(H5F_addr_defined(hdr->fs_addr)) {
#ifdef QAK
HDfprintf(stderr, "%s: hdr->fs_addr = %a\n", FUNC, hdr->fs_addr);
#endif /* QAK */
        /* Delete free space manager for heap */
        if(H5HF_space_delete(hdr, dxpl_id) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to release fractal heap free space manager")
    } /* end if */

    /* Check for root direct/indirect block */
    if(H5F_addr_defined(hdr->man_dtable.table_addr)) {
#ifdef QAK
HDfprintf(stderr, "%s: hdr->man_dtable.table_addr = %a\n", FUNC, hdr->man_dtable.table_addr);
#endif /* QAK */
        if(hdr->man_dtable.curr_root_rows == 0) {
            /* Delete root direct block */
            if(H5HF_man_dblock_delete(f, dxpl_id, hdr->man_dtable.table_addr, (hsize_t)hdr->man_dtable.cparam.start_block_size) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to release fractal heap root direct block")
        } /* end if */
        else {
            /* Delete root indirect block */
            if(H5HF_man_iblock_delete(hdr, dxpl_id, hdr->man_dtable.table_addr, hdr->man_dtable.curr_root_rows, NULL, 0) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to release fractal heap root indirect block")
        } /* end else */
    } /* end if */

    /* Release header's disk space */
    if(H5MF_xfree(f, H5FD_MEM_FHEAP_HDR, dxpl_id, fh_addr, (hsize_t)H5HF_HEADER_SIZE(hdr)) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to release fractal heap header")

    /* Finished deleting header */
    if(H5AC_unprotect(f, dxpl_id, H5AC_FHEAP_HDR, fh_addr, hdr, H5AC__DIRTIED_FLAG|H5AC__DELETED_FLAG) < 0)
        HDONE_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap header")
    hdr = NULL;

done:
    /* Unprotect the header, if an error occurred */
    if(hdr && H5AC_unprotect(f, dxpl_id, H5AC_FHEAP_HDR, fh_addr, hdr, H5AC__NO_FLAGS_SET) < 0)
        HDONE_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap header")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_delete() */

